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

// NT APIs for driver loading (fallback path)
extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);

#define SE_LOAD_DRIVER_PRIVILEGE 10L

namespace sky::driver {

    // IOCTL codes for HWiNFO physical memory access
    //   Read:  0x9C40259C  (CTL_CODE(0x22, 0x118, 0, 0))
    //   Write: 0x9C4025A0  (CTL_CODE(0x22, 0x119, 0, 0))
    #define CTLCODE_READ  0x9C40259C
    #define CTLCODE_WRITE 0x9C4025A0

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ============================================================
    // Step 1: Enumerate all DOS devices and find HWiNFO
    // ============================================================
    // HWiNFO64 creates a device when it runs. The device name
    // appears in the DOS devices namespace. We enumerate ALL
    // device names and look for any containing "HWiNFO".
    // This works regardless of what the actual name is.
    static bool find_hwinfo_device_via_dos_enum() {
        char buf[65536] = { 0 };
        DWORD ret = QueryDosDeviceA(nullptr, buf, sizeof(buf));
        if (ret == 0) {
            LOG_ERROR("QueryDosDevice failed: " + std::to_string(GetLastError()));
            return false;
        }

        // Collect all device names containing "HWiNFO" (case-insensitive)
        std::vector<std::string> hwinfo_devices;
        for (char* p = buf; *p; p += strlen(p) + 1) {
            std::string name(p);
            std::string upper;
            for (char c : name) upper += (char)toupper((unsigned char)c);
            if (upper.find("HWINFO") != std::string::npos) {
                hwinfo_devices.push_back(name);
            }
        }

        if (hwinfo_devices.empty()) {
            LOG_INFO("No HWiNFO devices found in DOS namespace");
            return false;
        }

        // Try to open each found device
        for (const auto& dev : hwinfo_devices) {
            std::string path = "\\\\.\\" + dev;
            LOG_INFO("Trying device: " + path);
            g_hwinfo_device = CreateFileA(path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO("SUCCESS: Opened device: " + path);
                return true;
            }
            LOG_ERROR("  Failed: err=" + std::to_string(GetLastError()));
        }
        return false;
    }

    // ============================================================
    // Step 2: Try hardcoded device paths
    // ============================================================
    static bool try_hardcoded_device_paths() {
        static const char* paths[] = {
            "\\\\.\\HWiNFO",
            "\\\\.\\HWiNFO64",
            "\\\\.\\HWiNFO32",
            "\\\\.\\HWiNFO_0",
            "\\\\.\\HWiNFO_V2",
            "\\\\.\\HWiNFOMap",
            "\\\\.\\HWiNFO_CORE",
            nullptr
        };
        for (int i = 0; paths[i]; i++) {
            g_hwinfo_device = CreateFileA(paths[i],
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO(std::string("Opened: ") + paths[i]);
                return true;
            }
        }
        return false;
    }

    // ============================================================
    // Step 3: Load driver ourselves via NtLoadDriver (NT path format)
    // ============================================================
    // Uses "\??\C:\path" format which is what NtLoadDriver expects.
    // This is the LAST resort. Normally HWiNFO64 should be running
    // and we find its device via DOS enumeration.
    static std::string find_driver_file() {
        // %TEMP%\HWiNFO*.sys
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
        // Program Files
        const char* dirs[] = {
            "C:\\Program Files\\HWiNFO64\\",
            "C:\\Program Files (x86)\\HWiNFO\\",
            "C:\\HWiNFO64\\",
            nullptr
        };
        for (int i = 0; dirs[i]; i++) {
            std::string search = std::string(dirs[i]) + "HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                std::string found = std::string(dirs[i]) + fd.cFileName;
                FindClose(h);
                return found;
            }
        }
        return "";
    }

    static bool load_driver_nt() {
        std::string drv = find_driver_file();
        if (drv.empty()) {
            MessageBoxA(0,
                "HWiNFO driver .sys not found.\n\n"
                "Run HWiNFO64 at least once first.",
                "Sky", MB_OK | MB_ICONERROR);
            return false;
        }

        // Verify file exists
        if (GetFileAttributesA(drv.c_str()) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxA(0,
                ("Driver file exists in search but not accessible:\n" + drv).c_str(),
                "Sky", MB_OK | MB_ICONERROR);
            return false;
        }

        LOG_INFO("Found driver: " + drv);

        // Enable SeLoadDriverPrivilege
        BOOLEAN priv_old = FALSE;
        NTSTATUS priv_st = RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &priv_old);
        if (priv_st != 0) {
            std::string msg = "RtlAdjustPrivilege failed: 0x" +
                [](NTSTATUS s) {
                    std::stringstream ss; ss << std::hex << (unsigned long)s;
                    return ss.str();
                }(priv_st) + "\nNot running as Admin?";
            MessageBoxA(0, msg.c_str(), "Sky", MB_OK | MB_ICONERROR);
            return false;
        }

        // Create registry service entry
        // Key: HKLM\SYSTEM\CurrentControlSet\Services\SkyHwiNFO
        const char* SVC = "SkyHwiNFO";
        std::string reg_path = "SYSTEM\\CurrentControlSet\\Services\\";
        reg_path += SVC;

        HKEY hKey;
        LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE, reg_path.c_str(), 0,
            nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
        if (rc != ERROR_SUCCESS) {
            std::string msg = "RegCreateKey failed: " + std::to_string(rc) +
                "\nNot running as Admin?";
            MessageBoxA(0, msg.c_str(), "Sky", MB_OK | MB_ICONERROR);
            return false;
        }

        // ImagePath in NT format: \??\C:\path\to\driver.sys
        std::string img_path = "\\??\\" + drv;
        RegSetValueExA(hKey, "ImagePath", 0, REG_SZ,
            (const BYTE*)img_path.c_str(), (DWORD)(img_path.length() + 1));
        DWORD dwType = 1;     // SERVICE_KERNEL_DRIVER
        RegSetValueExA(hKey, "Type", 0, REG_DWORD, (BYTE*)&dwType, sizeof(dwType));
        DWORD dwStart = 3;    // SERVICE_DEMAND_START
        RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&dwStart, sizeof(dwStart));
        DWORD dwErr = 0;      // SERVICE_ERROR_IGNORE
        RegSetValueExA(hKey, "ErrorControl", 0, REG_DWORD, (BYTE*)&dwErr, sizeof(dwErr));
        RegCloseKey(hKey);

        // Build NT registry path for NtLoadDriver
        std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (size_t i = 0; SVC[i]; i++) wreg += (wchar_t)SVC[i];

        UNICODE_STRING us;
        us.Buffer = (PWSTR)wreg.c_str();
        us.Length = (USHORT)(wreg.length() * sizeof(wchar_t));
        us.MaximumLength = us.Length + sizeof(wchar_t);

        NTSTATUS st = NtLoadDriver(&us);

        // STATUS_IMAGE_ALREADY_LOADED (0xC000010E) is OK
        if (st == (NTSTATUS)0xC000010E) {
            LOG_INFO("Driver already loaded");
            Sleep(500);
            return true;
        }

        if (!NT_SUCCESS(st)) {
            // Cleanup
            RegDeleteKeyA(HKEY_LOCAL_MACHINE, reg_path.c_str());

            std::stringstream ss;
            ss << std::hex << (unsigned long)st;
            std::string msg = "NtLoadDriver failed: 0x" + ss.str() + "\n\n";
            msg += "Driver: " + drv + "\n";
            msg += "ImagePath: " + img_path + "\n\n";
            if (st == (NTSTATUS)0xC0000034)
                msg += "STATUS_OBJECT_NAME_NOT_FOUND";
            else if (st == (NTSTATUS)0xC000026C)
                msg += "STATUS_DRIVER_FAILED_TO_LOAD (driver signature/security)";
            else if (st == (NTSTATUS)0xC000010E)
                msg += "STATUS_DRIVER_BLOCKED (Vanguard/HVCI blocking)";
            else if (st == (NTSTATUS)0xC0000022)
                msg += "STATUS_ACCESS_DENIED (not Admin)";
            MessageBoxA(0, msg.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
            return false;
        }

        Sleep(1000);
        LOG_INFO("Driver loaded via NtLoadDriver");
        return true;
    }

    static void unload_driver_nt() {
        const char* SVC = "SkyHwiNFO";
        std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (size_t i = 0; SVC[i]; i++) wreg += (wchar_t)SVC[i];

        UNICODE_STRING us;
        us.Buffer = (PWSTR)wreg.c_str();
        us.Length = (USHORT)(wreg.length() * sizeof(wchar_t));
        us.MaximumLength = us.Length + sizeof(wchar_t);

        NtUnloadDriver(&us);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE,
            ("SYSTEM\\CurrentControlSet\\Services\\" + std::string(SVC)).c_str());
    }

    // ============================================================
    // Main entry: try to connect to HWiNFO device
    // ============================================================
    static bool open_hwinfo_device() {
        // Phase 1: HWiNFO64 is already running — find its device
        LOG_INFO("=== Phase 1: Looking for live HWiNFO device ===");

        if (find_hwinfo_device_via_dos_enum()) {
            return true;
        }

        if (try_hardcoded_device_paths()) {
            return true;
        }

        // Phase 2: No device found. Is HWiNFO64 running?
        LOG_INFO("=== Phase 2: No live device. Checking if HWiNFO64 is running ===");

        HWND hwd = FindWindowA(nullptr, "HWiNFO64");
        if (!hwd) {
            hwd = FindWindowA(nullptr, "HWiNFO");
        }
        bool hwinf_running = (hwd != nullptr);
        LOG_INFO(std::string("HWiNFO64 window: ") + (hwinf_running ? "FOUND" : "NOT FOUND"));

        // Phase 3: Try to load driver ourselves
        LOG_INFO("=== Phase 3: Attempting to load HWiNFO driver ourselves ===");

        std::string user_msg = "Sky could not find a running HWiNFO device.\n\n";
        if (!hwinf_running) {
            user_msg += "HWiNFO64 does not appear to be running.\n";
            user_msg += "Please start HWiNFO64 now, then click OK.\n";
            user_msg += "Sky will retry finding the device.\n\n";
            user_msg += "If HWiNFO64 is already running and you still see this,\n";
            user_msg += "click OK and Sky will try to load the driver itself.";
        } else {
            user_msg += "HWiNFO64 appears to be running but its device\n";
            user_msg += "was not found. Click OK and Sky will try to\n";
            user_msg += "load the driver itself.";
        }

        int choice = MessageBoxA(0,
            user_msg.c_str(), "Sky - HWiNFO Device Not Found",
            MB_OKCANCEL | MB_ICONQUESTION);

        if (choice == IDCANCEL) {
            return false;
        }

        // If HWiNFO wasn't running, user may have just started it — retry
        if (!hwinf_running) {
            Sleep(2000);
            if (find_hwinfo_device_via_dos_enum()) {
                return true;
            }
            if (try_hardcoded_device_paths()) {
                return true;
            }
        }

        // Last resort: load driver ourselves via NtLoadDriver
        if (!load_driver_nt()) {
            return false;
        }

        // Retry device finding after loading
        if (find_hwinfo_device_via_dos_enum()) {
            return true;
        }
        if (try_hardcoded_device_paths()) {
            return true;
        }

        // Failed
        MessageBoxA(0,
            "Sky loaded the driver but still can't find the device.\n\n"
            "This usually means:\n"
            "1. Vanguard/HVCI is blocking the driver\n"
            "2. The driver creates a device with a non-standard name\n\n"
            "Solution: Run HWiNFO64 as Admin, keep it running,\n"
            "then launch Sky.exe as Admin.",
            "Sky - Driver Load Failed", MB_OK | MB_ICONERROR);
        unload_driver_nt();
        return false;
    }

    // ============================================================
    // Physical memory read/write
    // ============================================================
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
            if (!DeviceIoControl(g_hwinfo_device, CTLCODE_READ,
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
            if (!DeviceIoControl(g_hwinfo_device, CTLCODE_WRITE,
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
    // HWiNFO driver implementation
    // ============================================================
    class HWiNFODriver : public IDriver {
    public:
        HWiNFODriver() : m_init(false), m_pid(0), m_base(0), m_dtb(0), m_dtb_set(false) {}

        bool setup() override {
            LOG_INFO("Connecting to HWiNFO device...");
            if (!open_hwinfo_device()) {
                LOG_ERROR("Failed to open HWiNFO device");
                return false;
            }
            m_init = true;
            LOG_INFO("HWiNFO device connected");
            return true;
        }

        void unload() override {
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                CloseHandle(g_hwinfo_device);
                g_hwinfo_device = INVALID_HANDLE_VALUE;
            }
            // Try to unload our loaded driver (harmless if we didn't load it)
            unload_driver_nt();
            m_init = false;
            LOG_INFO("HWiNFO device closed");
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
