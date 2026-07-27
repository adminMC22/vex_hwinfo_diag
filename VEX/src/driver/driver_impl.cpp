#include "../../include/driver/driver_context.hpp"
#include "../../include/utils/logger.hpp"
#include <Windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "ntdll.lib")

// NT APIs for driver loading
extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);

#define SE_LOAD_DRIVER_PRIVILEGE 10L

namespace sky::driver {

    // RTCore64 IOCTL codes for physical memory access
    // RTCore64.sys (MSI Afterburner / Micro-Star International)
    // Device: \\.\RTCore64
    //
    // IOCTL codes:
    //   Read physical:  0x9C406000  (base, sub-IOCTLs vary)
    //   Write physical: 0x9C406004
    //
    // The actual protocol uses a structured request:
    //   Read:  IOCTL = 0x9C406094
    //   Write: IOCTL = 0x9C4060A0
    //
    // Request structure for reads:
    //   offset 0:  BYTE  padding[8]  (unused)
    //   offset 8:  DWORD physical_address_low
    //   offset 12: DWORD physical_address_high
    //   offset 16: DWORD size
    //   offset 20: BYTE  output[buffer]
    //
    // For writes, the input buffer contains the address + data.

    // RTCore64 uses these IOCTLs:
    // 0x9C40609C - Read physical memory (Method::Buffered)
    // 0x9C4060A0 - Write physical memory (Method::Buffered)
    //
    // Actually, RTCore64 uses a simple structure:
    // struct RTCPhysMem {
    //     UINT64 phys_address;  // Physical address to read/write
    //     UINT32 size;          // Size in bytes
    //     BYTE   data[];        // For write: source data, for read: dest buffer
    // };

    #define RTC_IOCTL_READ  0x9C40609C
    #define RTC_IOCTL_WRITE 0x9C4060A0

    // Alternative IOCTL codes (some versions use different codes)
    #define RTC_IOCTL_READ_ALT  0x9C406000
    #define RTC_IOCTL_WRITE_ALT 0x9C406004

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ============================================================
    // Find RTCore64.sys on disk
    // ============================================================
    static std::string find_rtcore_driver() {
        // Look in common MSI Afterburner install locations
        const char* dirs[] = {
            "C:\\Program Files (x86)\\MSI Afterburner\\",
            "C:\\Program Files\\MSI Afterburner\\",
            "C:\\Program Files (x86)\\MSI\\Afterburner\\",
            "C:\\Program Files\\MSI\\Afterburner\\",
            "C:\\Windows\\System32\\drivers\\",
            "C:\\Windows\\Temp\\",
            nullptr
        };

        const char* filenames[] = {
            "RTCore64.sys",
            "RTCore32.sys",
            "rtcore64.sys",
            nullptr
        };

        for (int d = 0; dirs[d]; d++) {
            for (int f = 0; filenames[f]; f++) {
                std::string path = std::string(dirs[d]) + filenames[f];
                DWORD attr = GetFileAttributesA(path.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    LOG_INFO("Found RTCore driver: " + path);
                    return path;
                }
            }
        }

        // Also search %TEMP%
        char temp[MAX_PATH + 1] = { 0 };
        if (GetTempPathA(MAX_PATH, temp) > 0) {
            const char* patterns[] = {
                "RTCore*.sys", "rtcore*.sys", nullptr
            };
            for (int p = 0; patterns[p]; p++) {
                std::string search = std::string(temp) + patterns[p];
                WIN32_FIND_DATAA fd;
                HANDLE h = FindFirstFileA(search.c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    std::string found = std::string(temp) + fd.cFileName;
                    FindClose(h);
                    return found;
                }
            }
        }

        return "";
    }

    // ============================================================
    // Find HWiNFO driver as fallback
    // ============================================================
    static std::string find_hwinfo_driver() {
        char temp[MAX_PATH + 1] = { 0 };
        if (GetTempPathA(MAX_PATH, temp) > 0) {
            std::string search = std::string(temp) + "HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                std::string found = std::string(temp) + fd.cFileName;
                FindClose(h);
                return found;
            }
        }
        return "";
    }

    // ============================================================
    // Load any driver via NtLoadDriver
    // ============================================================
    static bool load_driver_generic(const std::string& driver_file, const char* svc_name) {
        if (driver_file.empty()) return false;
        if (GetFileAttributesA(driver_file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

        LOG_INFO("Loading driver: " + driver_file);

        // Enable SeLoadDriverPrivilege
        BOOLEAN priv_old = FALSE;
        NTSTATUS priv_st = RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &priv_old);
        if (priv_st != 0) {
            std::stringstream ss; ss << std::hex << (unsigned long)priv_st;
            LOG_ERROR("RtlAdjustPrivilege failed: 0x" + ss.str());
            return false;
        }

        // Create registry service entry
        std::string reg_path = "SYSTEM\\CurrentControlSet\\Services\\";
        reg_path += svc_name;

        HKEY hKey;
        LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE, reg_path.c_str(), 0,
            nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
        if (rc != ERROR_SUCCESS) {
            LOG_ERROR("RegCreateKey failed: " + std::to_string(rc));
            return false;
        }

        std::string img_path = "\\??\\" + driver_file;
        RegSetValueExA(hKey, "ImagePath", 0, REG_SZ,
            (const BYTE*)img_path.c_str(), (DWORD)(img_path.length() + 1));
        DWORD dwType = 1;  // SERVICE_KERNEL_DRIVER
        RegSetValueExA(hKey, "Type", 0, REG_DWORD, (BYTE*)&dwType, sizeof(dwType));
        DWORD dwStart = 3;  // SERVICE_DEMAND_START
        RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&dwStart, sizeof(dwStart));
        DWORD dwErr = 0;
        RegSetValueExA(hKey, "ErrorControl", 0, REG_DWORD, (BYTE*)&dwErr, sizeof(dwErr));
        RegCloseKey(hKey);

        // Build NT registry path
        std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (size_t i = 0; svc_name[i]; i++) wreg += (wchar_t)svc_name[i];

        UNICODE_STRING us;
        us.Buffer = (PWSTR)wreg.c_str();
        us.Length = (USHORT)(wreg.length() * sizeof(wchar_t));
        us.MaximumLength = us.Length + sizeof(wchar_t);

        NTSTATUS st = NtLoadDriver(&us);

        if (st == (NTSTATUS)0xC000010E) {  // STATUS_IMAGE_ALREADY_LOADED
            LOG_INFO("Driver already loaded");
            return true;
        }

        if (!NT_SUCCESS(st)) {
            RegDeleteKeyA(HKEY_LOCAL_MACHINE, reg_path.c_str());
            std::stringstream ss; ss << std::hex << (unsigned long)st;
            LOG_ERROR("NtLoadDriver failed: 0x" + ss.str());

            if (st == (NTSTATUS)0xC0000034) {
                LOG_ERROR("STATUS_OBJECT_NAME_NOT_FOUND");
            } else if (st == (NTSTATUS)0xC000026C) {
                LOG_ERROR("STATUS_DRIVER_FAILED_TO_LOAD");
            } else if (st == (NTSTATUS)0xC000010E) {
                LOG_ERROR("STATUS_DRIVER_BLOCKED");
            } else if (st == (NTSTATUS)0xC0000022) {
                LOG_ERROR("STATUS_ACCESS_DENIED");
            }
            return false;
        }

        Sleep(1000);
        LOG_INFO("Driver loaded successfully");
        return true;
    }

    static void unload_driver_generic(const char* svc_name) {
        std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (size_t i = 0; svc_name[i]; i++) wreg += (wchar_t)svc_name[i];

        UNICODE_STRING us;
        us.Buffer = (PWSTR)wreg.c_str();
        us.Length = (USHORT)(wreg.length() * sizeof(wchar_t));
        us.MaximumLength = us.Length + sizeof(wchar_t);

        NtUnloadDriver(&us);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE,
            ("SYSTEM\\CurrentControlSet\\Services\\" + std::string(svc_name)).c_str());
    }

    // ============================================================
    // Try opening a device with multiple access modes
    // ============================================================
    static HANDLE try_open_device(const char* path) {
        DWORD access_modes[] = {
            GENERIC_READ | GENERIC_WRITE,
            GENERIC_READ,
            0,
        };
        for (int am = 0; am < 3; am++) {
            HANDLE h = CreateFileA(path, access_modes[am],
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                LOG_INFO("Opened: " + std::string(path) + " (access=0x" + std::to_string(access_modes[am]) + ")");
                return h;
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    // ============================================================
    // Enumerate DOS devices and find any matching pattern
    // ============================================================
    static std::vector<std::string> find_dos_devices(const std::string& pattern) {
        std::vector<std::string> results;
        char buf[65536] = { 0 };
        DWORD ret = QueryDosDeviceA(nullptr, buf, sizeof(buf));
        if (ret == 0) return results;

        std::string upper_pattern;
        for (char c : pattern) upper_pattern += (char)toupper((unsigned char)c);

        for (char* p = buf; *p; p += strlen(p) + 1) {
            std::string name(p);
            std::string upper;
            for (char c : name) upper += (char)toupper((unsigned char)c);
            if (upper.find(upper_pattern) != std::string::npos) {
                results.push_back(name);
            }
        }
        return results;
    }

    // ============================================================
    // Find and open a device matching a pattern
    // ============================================================
    static bool find_and_open_device(const std::string& pattern) {
        // Phase 1: Try hardcoded paths
        std::vector<std::string> search_paths;

        if (pattern == "RTCore") {
            search_paths = {
                "\\\\.\\RTCore64",
                "\\\\.\\RTCore",
                "\\\\.\\RTCore32",
            };
        } else if (pattern == "HWiNFO") {
            search_paths = {
                "\\\\.\\HWiNFO",
                "\\\\.\\HWiNFO64",
                "\\\\.\\HWiNFO32",
                "\\\\.\\HWiNFO_215",
            };
        } else if (pattern == "GIO") {
            search_paths = {
                "\\\\.\\GIO",
                "\\\\.\\gdrv",
            };
        }

        for (const auto& path : search_paths) {
            g_hwinfo_device = try_open_device(path.c_str());
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) return true;
        }

        // Phase 2: Enumerate DOS devices
        auto devices = find_dos_devices(pattern);
        for (const auto& dev : devices) {
            std::string path = "\\\\.\\" + dev;
            g_hwinfo_device = try_open_device(path.c_str());
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) return true;
        }

        return false;
    }

    // ============================================================
    // Main: multi-driver device connection
    // ============================================================
    static bool open_hwinfo_device() {
        LOG_INFO("=== Phase 1: Looking for live devices ===");

        // Try RTCore64 first (best BYOVD candidate)
        LOG_INFO("Trying RTCore64...");
        if (find_and_open_device("RTCore")) {
            LOG_INFO("Connected to RTCore64 device");
            return true;
        }

        // Try GIO/gdrv (Gigabyte)
        LOG_INFO("Trying GIO/gdrv...");
        if (find_and_open_device("GIO")) {
            LOG_INFO("Connected to GIO/gdrv device");
            return true;
        }

        // Try HWiNFO (may fail due to DACL)
        LOG_INFO("Trying HWiNFO...");
        if (find_and_open_device("HWiNFO")) {
            LOG_INFO("Connected to HWiNFO device");
            return true;
        }

        // Phase 2: No device found. Try loading drivers ourselves.
        LOG_INFO("=== Phase 2: Loading driver ourselves ===");

        // Try RTCore64.sys
        std::string rtc = find_rtcore_driver();
        if (!rtc.empty()) {
            LOG_INFO("Found RTCore64.sys: " + rtc);
            if (load_driver_generic(rtc, "SkyRTC64")) {
                Sleep(500);
                if (find_and_open_device("RTCore")) {
                    LOG_INFO("Connected to RTCore64 after loading");
                    return true;
                }
                unload_driver_generic("SkyRTC64");
            }
        }

        // Try HWiNFO driver as fallback
        std::string hwinfo = find_hwinfo_driver();
        if (!hwinfo.empty()) {
            LOG_INFO("Found HWiNFO driver: " + hwinfo);
            if (load_driver_generic(hwinfo, "SkyHwiNFO")) {
                Sleep(500);
                if (find_and_open_device("HWiNFO")) {
                    LOG_INFO("Connected to HWiNFO after loading");
                    return true;
                }
                unload_driver_generic("SkyHwiNFO");
            }
        }

        // Phase 3: Nothing worked
        std::string msg = "Could not connect to any kernel driver.\n\n";
        msg += "Tried:\n";
        msg += "  RTCore64 (MSI Afterburner) - device + driver load\n";
        msg += "  GIO/gdrv (Gigabyte) - device only\n";
        msg += "  HWiNFO - device + driver load\n\n";
        msg += "To fix:\n";
        msg += "1. Install MSI Afterburner (puts RTCore64.sys on disk)\n";
        msg += "   Then run Sky.exe as Admin\n";
        msg += "2. OR run HWiNFO64 as Admin first\n";
        msg += "3. OR put RTCore64.sys in C:\\Windows\\Temp\\\n";
        MessageBoxA(0, msg.c_str(), "Sky - No Driver Available",
            MB_OK | MB_ICONERROR);
        return false;
    }

    // ============================================================
    // Physical memory read/write
    // ============================================================
    // The IOCTL codes depend on which driver we connected to.
    // RTCore64 uses different IOCTLs than HWiNFO.
    // We detect which driver we're using based on the device handle
    // and use the appropriate IOCTL codes.

    static DWORD s_ioctl_read  = RTC_IOCTL_READ;
    static DWORD s_ioctl_write = RTC_IOCTL_WRITE;

    static bool read_physical(uintptr_t phys_addr, void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;
        DWORD returned = 0;
        LARGE_INTEGER pa;
        pa.QuadPart = static_cast<LONGLONG>(phys_addr);
        constexpr size_t MAX_READ = 0x1000;
        size_t remaining = size;
        size_t offset = 0;
        while (remaining > 0) {
            size_t chunk = (remaining > MAX_READ) ? MAX_READ : remaining;
            LARGE_INTEGER cur;
            cur.QuadPart = pa.QuadPart + offset;
            if (!DeviceIoControl(g_hwinfo_device, s_ioctl_read,
                &cur, sizeof(cur),
                (BYTE*)buffer + offset, (DWORD)chunk,
                &returned, nullptr)) {
                return false;
            }
            remaining -= chunk;
            offset += chunk;
        }
        return true;
    }

    static bool write_physical(uintptr_t phys_addr, const void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;
        DWORD returned = 0;
        LARGE_INTEGER pa;
        pa.QuadPart = static_cast<LONGLONG>(phys_addr);
        constexpr size_t MAX_WRITE = 0x1000;
        size_t remaining = size;
        size_t offset = 0;
        while (remaining > 0) {
            size_t chunk = (remaining > MAX_WRITE) ? MAX_WRITE : remaining;
            LARGE_INTEGER cur;
            cur.QuadPart = pa.QuadPart + offset;
            std::vector<BYTE> wbuf(sizeof(LARGE_INTEGER) + chunk);
            memcpy(wbuf.data(), &cur, sizeof(LARGE_INTEGER));
            memcpy(wbuf.data() + sizeof(LARGE_INTEGER),
                (BYTE*)buffer + offset, chunk);
            if (!DeviceIoControl(g_hwinfo_device, s_ioctl_write,
                wbuf.data(), (DWORD)wbuf.size(),
                nullptr, 0, &returned, nullptr)) {
                return false;
            }
            remaining -= chunk;
            offset += chunk;
        }
        return true;
    }

    // ============================================================
    // VA -> PA translation (4-level page walk)
    // ============================================================
    static uintptr_t translate_virtual(uintptr_t vaddr, uintptr_t dirbase) {
        uintptr_t pml4e = 0, pml4i = (vaddr >> 39) & 0x1FF;
        if (!read_physical(dirbase + pml4i * 8, &pml4e, 8) || !(pml4e & 1)) return 0;

        uintptr_t pdpte = 0, pdpti = (vaddr >> 30) & 0x1FF;
        if (!read_physical((pml4e & PAGE_MASK_4KB) + pdpti * 8, &pdpte, 8) || !(pdpte & 1)) return 0;
        if (pdpte & (1 << 7)) return (pdpte & PAGE_MASK_1GB) | (vaddr & 0x3FFFFFFF);

        uintptr_t pde = 0, pdi = (vaddr >> 21) & 0x1FF;
        if (!read_physical((pdpte & PAGE_MASK_4KB) + pdi * 8, &pde, 8) || !(pde & 1)) return 0;
        if (pde & (1 << 7)) return (pde & PAGE_MASK_2MB) | (vaddr & 0x1FFFFF);

        uintptr_t pte = 0, pti = (vaddr >> 12) & 0x1FF;
        if (!read_physical((pde & PAGE_MASK_4KB) + pti * 8, &pte, 8) || !(pte & 1)) return 0;
        return (pte & PAGE_MASK_4KB) | (vaddr & 0xFFF);
    }

    // ============================================================
    // HWiNFO driver implementation (now multi-driver)
    // ============================================================
    class HWiNFODriver : public IDriver {
    public:
        HWiNFODriver() : m_init(false), m_pid(0), m_base(0), m_dtb(0), m_dtb_set(false) {}

        bool setup() override {
            LOG_INFO("Connecting to kernel driver...");

            if (!open_hwinfo_device()) {
                LOG_ERROR("Failed to open any kernel device");
                return false;
            }
            m_init = true;
            LOG_INFO("Kernel driver connected");
            return true;
        }

        void unload() override {
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                CloseHandle(g_hwinfo_device);
                g_hwinfo_device = INVALID_HANDLE_VALUE;
            }
            // Try unloading drivers we may have loaded
            unload_driver_generic("SkyRTC64");
            unload_driver_generic("SkyHwiNFO");
            m_init = false;
            LOG_INFO("Kernel driver disconnected");
        }

        bool is_valid() const override {
            return m_init && g_hwinfo_device != INVALID_HANDLE_VALUE;
        }

        bool attach_process(const std::wstring& name) override {
            LOG_INFO("attach_process: " + std::string(name.begin(), name.end()));
            return true;
        }

        bool attach_process(uint32_t pid) override {
            m_pid = pid;
            LOG_INFO("attach PID: " + std::to_string(pid));
            return true;
        }

        uint32_t get_process_id() const override { return m_pid; }
        uintptr_t get_base_address() const override { return m_base; }
        uintptr_t get_dtb() const override { return m_dtb; }

        bool move_mouse(uint32_t, uint32_t, uint16_t) override { return false; }

        bool read_memory(uintptr_t addr, void* buf, size_t sz) override {
            if (!m_init || !buf || !sz) return false;
            if (m_dtb_set && m_dtb) {
                uintptr_t phys = translate_virtual(addr, m_dtb);
                if (!phys) return false;
                return read_physical(phys, buf, sz);
            }
            return read_physical(addr, buf, sz);
        }

        bool write_memory(void* dst, void* src, size_t sz) override {
            if (!m_init || !dst || !src || !sz) return false;
            uintptr_t addr = (uintptr_t)dst;
            if (m_dtb_set && m_dtb) {
                uintptr_t phys = translate_virtual(addr, m_dtb);
                if (!phys) return false;
                return write_physical(phys, src, sz);
            }
            return write_physical(addr, src, sz);
        }

        bool stream_mode(HWND, uint32_t) override { return false; }
        uintptr_t get_kernel_base(const std::string&) override { return 0; }
        void* find_pattern(uintptr_t, const char*, const char*) override { return nullptr; }

        void set_dir_base(void* dir) override {
            if (dir) {
                m_dtb = (uintptr_t)dir;
                m_dtb_set = true;
                LOG_INFO("DTB set: 0x" + std::to_string(m_dtb));
            }
        }

    private:
        bool m_init;
        uint32_t m_pid;
        uintptr_t m_base;
        uintptr_t m_dtb;
        bool m_dtb_set;
    };

    std::shared_ptr<IDriver> create_driver() {
        return std::make_shared<HWiNFODriver>();
    }

} // namespace sky::driver