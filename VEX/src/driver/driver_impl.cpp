#include "../../include/driver/driver_context.hpp"
#include "../../include/utils/logger.hpp"
#include <Windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <intrin.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")

extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);

#define SE_LOAD_DRIVER_PRIVILEGE 10L

namespace sky::driver {

    // IOCTL codes for physical memory access (hwinfo64.sys):
    //   Read:  0x9C40259C  (CTL_CODE(0x22, 0x118, 0, 0))
    //   Write: 0x9C4025A0  (CTL_CODE(0x22, 0x119, 0, 0))
    #define CTLCODE_READ  0x9C40259C
    #define CTLCODE_WRITE 0x9C4025A0

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ---- Find the HWiNFO device by enumerating DOS device names ----
    // HWiNFO64 creates a device name like "HWiNFO", "HWiNFO32", etc.
    // in the DOS devices namespace. We enumerate all and find any with "HWiNFO".
    static bool find_and_open_hwinfo_device() {
        // QueryDosDevice(NULL) returns all DOS device names separated by \0,
        // with double \0 at the end. Each name can be used as \\.\<name>.
        char buf[65536] = { 0 };
        DWORD ret = QueryDosDeviceA(nullptr, buf, sizeof(buf));
        if (ret == 0) {
            LOG_ERROR("QueryDosDevice failed: " + std::to_string(GetLastError()));
            return false;
        }

        // Parse the multi-string
        std::vector<std::string> hwinfo_names;
        for (char* p = buf; *p; p += strlen(p) + 1) {
            std::string name(p);
            // Look for any name containing "HWiNFO" (case-insensitive comparison)
            std::string upper;
            for (char c : name) upper += (char)toupper((unsigned char)c);
            if (upper.find("HWINFO") != std::string::npos) {
                hwinfo_names.push_back(name);
            }
        }

        // Try each found name as \\.\<name>
        for (const auto& nm : hwinfo_names) {
            std::string path = "\\\\.\\" + nm;
            LOG_INFO("Trying device: " + path);
            g_hwinfo_device = CreateFileA(path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO("Opened device: " + path);
                return true;
            }
            LOG_ERROR("  Failed: " + path + " (err=" + std::to_string(GetLastError()) + ")");
        }

        return false;
    }

    // ---- Fallback: try loading the HWiNFO driver via SCM ----
    static bool load_hwinfo_driver_via_scm() {
        // Find the driver .sys file on disk
        std::string driver_file;

        // 1) %TEMP%
        char temp_path[MAX_PATH + 1] = { 0 };
        if (GetTempPathA(MAX_PATH, temp_path) > 0) {
            std::string search = std::string(temp_path) + "HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    driver_file = std::string(temp_path) + fd.cFileName;
                    LOG_INFO(std::string("Found driver in TEMP: ") + driver_file);
                    break; // take first match
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }

        // 2) Program Files
        if (driver_file.empty()) {
            const char* dirs[] = {
                "C:\\Program Files\\HWiNFO64\\",
                "C:\\Program Files (x86)\\HWiNFO\\",
                "C:\\Program Files\\HWiNFO\\",
                "C:\\HWiNFO64\\",
                nullptr
            };
            for (int i = 0; dirs[i] && driver_file.empty(); i++) {
                std::string search = std::string(dirs[i]) + "HWiNFO*.sys";
                WIN32_FIND_DATAA fd;
                HANDLE h = FindFirstFileA(search.c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        driver_file = std::string(dirs[i]) + fd.cFileName;
                        LOG_INFO(std::string("Found driver: ") + driver_file);
                        break;
                    } while (FindNextFileA(h, &fd));
                    FindClose(h);
                }
            }
        }

        if (driver_file.empty()) {
            MessageBoxA(0,
                "HWiNFO driver .sys file not found.\n\n"
                "Searched:\n"
                "  %TEMP%\\HWiNFO*.sys\n"
                "  C:\\Program Files\\HWiNFO*\\\n\n"
                "Run HWiNFO64 at least once to extract the driver.",
                "Sky Driver", MB_OK | MB_ICONERROR);
            return false;
        }

        // Enable SeLoadDriverPrivilege
        BOOLEAN old = FALSE;
        RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &old);

        // Use SCM to load the driver
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (!scm) {
            DWORD err = GetLastError();
            std::string msg = "OpenSCManager failed: " + std::to_string(err) + "\n";
            if (err == ERROR_ACCESS_DENIED) msg += "Not running as Administrator?";
            MessageBoxA(0, msg.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
            return false;
        }

        // Delete any previous SkyHwiNFO service
        SC_HANDLE old_svc = OpenServiceA(scm, "SkyHwiNFO", DELETE);
        if (old_svc) {
            DeleteService(old_svc);
            CloseServiceHandle(old_svc);
        }

        // Create the driver service
        // ImagePath must use NT path format (\??\C:\...) for kernel drivers
        std::string nt_path = "\\??\\" + driver_file;
        SC_HANDLE svc = CreateServiceA(scm,
            "SkyHwiNFO",
            "Sky HWiNFO Driver",
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            nt_path.c_str(),  // NT path format: \??\C:\path\to\driver.sys
            nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!svc) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_EXISTS) {
                svc = OpenServiceA(scm, "SkyHwiNFO", SERVICE_ALL_ACCESS);
            }
        }
        if (!svc) {
            std::string msg = "CreateService failed: " + std::to_string(GetLastError());
            MessageBoxA(0, msg.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
            CloseServiceHandle(scm);
            return false;
        }

        // Start the driver
        if (!StartServiceA(svc, 0, nullptr)) {
            DWORD err = GetLastError();
            if (err != ERROR_SERVICE_ALREADY_RUNNING) {
                std::string msg = "StartService failed: " + std::to_string(err) + "\n\n";
                if (err == ERROR_FILE_NOT_FOUND) msg += "Driver file not found.";
                else if (err == ERROR_PATH_NOT_FOUND) msg += "Driver path invalid.";
                else if (err == ERROR_BAD_DRIVER) msg += "Driver is corrupted or invalid.";
                else if (err == ERROR_DRIVER_BLOCKED) msg += "Driver blocked by security policy.";
                MessageBoxA(0, msg.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
                CloseServiceHandle(svc);
                CloseServiceHandle(scm);
                return false;
            }
        }

        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        Sleep(1500);  // Wait for driver device to be created
        LOG_INFO("Driver loaded via SCM");
        return true;
    }

    static void unload_driver() {
        if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hwinfo_device);
            g_hwinfo_device = INVALID_HANDLE_VALUE;
        }

        // Unload via SCM
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (scm) {
            SC_HANDLE svc = OpenServiceA(scm, "SkyHwiNFO", SERVICE_STOP | DELETE);
            if (svc) {
                SERVICE_STATUS ss;
                ControlService(svc, SERVICE_CONTROL_STOP, &ss);
                DeleteService(svc);
                CloseServiceHandle(svc);
            }
            CloseServiceHandle(scm);
        }
    }

    // ---- Open device: try existing, then load driver ourselves ----
    static bool open_hwinfo_device() {
        // Step 1: Try to find and open HWiNFO device (if already running)
        if (find_and_open_hwinfo_device()) {
            return true;
        }

        // Step 2: Not found — load driver via SCM and try again
        LOG_INFO("Device not found, loading HWiNFO driver...");
        if (!load_hwinfo_driver_via_scm()) {
            return false;
        }

        // Step 3: Retry opening the device
        if (find_and_open_hwinfo_device()) {
            return true;
        }

        // Step 4: Still can't open — diagnostic
        std::string msg = "Driver loaded but device not accessible.\n\n";
        msg += "QueryDosDevice returned no HWiNFO entries.\n";
        msg += "Possible reasons:\n";
        msg += "- Driver has a non-standard device name\n";
        msg += "- Vanguard blocked device creation\n";
        msg += "- Driver failed to initialize\n\n";
        msg += "Try running HWiNFO64 first, then Sky.exe.";
        MessageBoxA(0, msg.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
        unload_driver();
        return false;
    }

    // ---- Physical memory read/write via HWiNFO IOCTL ----

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
            LARGE_INTEGER current_pa;
            current_pa.QuadPart = pa.QuadPart + offset;

            if (!DeviceIoControl(g_hwinfo_device, CTLCODE_READ,
                &current_pa, sizeof(current_pa),
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
            LARGE_INTEGER current_pa;
            current_pa.QuadPart = pa.QuadPart + offset;

            std::vector<BYTE> write_buf(sizeof(LARGE_INTEGER) + chunk);
            memcpy(write_buf.data(), &current_pa, sizeof(LARGE_INTEGER));
            memcpy(write_buf.data() + sizeof(LARGE_INTEGER),
                (BYTE*)buffer + offset, chunk);

            if (!DeviceIoControl(g_hwinfo_device, CTLCODE_WRITE,
                write_buf.data(), (DWORD)write_buf.size(),
                nullptr, 0, &returned, nullptr)) {
                return false;
            }
            remaining -= chunk;
            offset += chunk;
        }
        return true;
    }

    // ---- VA -> PA translation ----

    static uintptr_t translate_virtual(uintptr_t virtual_addr, uintptr_t dirbase) {
        uintptr_t pml4_entry = 0;
        uintptr_t pml4_index = (virtual_addr >> 39) & 0x1FF;
        uintptr_t pml4_addr = dirbase + (pml4_index * 8);

        if (!read_physical(pml4_addr, &pml4_entry, sizeof(pml4_entry)))
            return 0;
        if (!(pml4_entry & 1)) return 0;

        uintptr_t pdpt_entry = 0;
        uintptr_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
        uintptr_t pdpt_addr = (pml4_entry & PAGE_MASK_4KB) + (pdpt_index * 8);

        if (!read_physical(pdpt_addr, &pdpt_entry, sizeof(pdpt_entry)))
            return 0;
        if (!(pdpt_entry & 1)) return 0;

        if (pdpt_entry & (1 << 7)) {
            return (pdpt_entry & PAGE_MASK_1GB) | (virtual_addr & 0x3FFFFFFF);
        }

        uintptr_t pd_entry = 0;
        uintptr_t pd_index = (virtual_addr >> 21) & 0x1FF;
        uintptr_t pd_addr = (pdpt_entry & PAGE_MASK_4KB) + (pd_index * 8);

        if (!read_physical(pd_addr, &pd_entry, sizeof(pd_entry)))
            return 0;
        if (!(pd_entry & 1)) return 0;

        if (pd_entry & (1 << 7)) {
            return (pd_entry & PAGE_MASK_2MB) | (virtual_addr & 0x1FFFFF);
        }

        uintptr_t pt_entry = 0;
        uintptr_t pt_index = (virtual_addr >> 12) & 0x1FF;
        uintptr_t pt_addr = (pd_entry & PAGE_MASK_4KB) + (pt_index * 8);

        if (!read_physical(pt_addr, &pt_entry, sizeof(pt_entry)))
            return 0;
        if (!(pt_entry & 1)) return 0;

        return (pt_entry & PAGE_MASK_4KB) | (virtual_addr & 0xFFF);
    }

    // ---- HWiNFO driver implementation ----

    class HWiNFODriver : public IDriver {
    public:
        HWiNFODriver()
            : m_initialized(false)
            , m_process_id(0)
            , m_base_address(0)
            , m_dtb(0)
            , m_dirbase_set(false) {}

        bool setup() override {
            LOG_INFO("Connecting to HWiNFO device...");

            if (!open_hwinfo_device()) {
                LOG_ERROR("Could not open HWiNFO device.");
                return false;
            }

            m_initialized = true;
            LOG_INFO("HWiNFO connection established");
            return true;
        }

        void unload() override {
            unload_driver();
            m_initialized = false;
            LOG_INFO("HWiNFO device closed and driver unloaded");
        }

        bool is_valid() const override {
            return m_initialized && g_hwinfo_device != INVALID_HANDLE_VALUE;
        }

        bool attach_process(const std::wstring& process_name) override {
            LOG_INFO("attach_process (stub): "
                + std::string(process_name.begin(), process_name.end()));
            return true;
        }

        bool attach_process(uint32_t process_id) override {
            m_process_id = process_id;
            LOG_INFO("attach_process: PID " + std::to_string(process_id));
            return true;
        }

        uint32_t get_process_id() const override { return m_process_id; }
        uintptr_t get_base_address() const override { return m_base_address; }
        uintptr_t get_dtb() const override { return m_dtb; }

        bool move_mouse(uint32_t x, uint32_t y, uint16_t button_flags) override {
            (void)x; (void)y; (void)button_flags;
            return false;
        }

        bool read_memory(uintptr_t address, void* buffer, size_t size) override {
            if (!m_initialized || !buffer || size == 0) return false;

            if (m_dirbase_set && m_dtb != 0) {
                uintptr_t phys = translate_virtual(address, m_dtb);
                if (phys == 0) return false;
                return read_physical(phys, buffer, size);
            }
            return read_physical(address, buffer, size);
        }

        bool write_memory(void* dest, void* src, size_t size) override {
            if (!m_initialized || !dest || !src || size == 0) return false;
            uintptr_t address = reinterpret_cast<uintptr_t>(dest);

            if (m_dirbase_set && m_dtb != 0) {
                uintptr_t phys = translate_virtual(address, m_dtb);
                if (phys == 0) return false;
                return write_physical(phys, src, size);
            }
            return write_physical(address, src, size);
        }

        bool stream_mode(HWND hwnd, uint32_t flag) override {
            (void)hwnd; (void)flag;
            return false;
        }

        uintptr_t get_kernel_base(const std::string& module_name) override {
            (void)module_name;
            return 0;
        }

        void* find_pattern(uintptr_t base, const char* pattern, const char* mask) override {
            (void)base; (void)pattern; (void)mask;
            return nullptr;
        }

        void set_dir_base(void* dir) override {
            if (dir) {
                m_dtb = reinterpret_cast<uintptr_t>(dir);
                m_dirbase_set = true;
                LOG_INFO("DTB set: 0x" + std::to_string(m_dtb));
            }
        }

    private:
        bool        m_initialized;
        uint32_t    m_process_id;
        uintptr_t   m_base_address;
        uintptr_t   m_dtb;
        bool        m_dirbase_set;
    };

    // ---- Factory ----

    std::shared_ptr<IDriver> create_driver() {
        return std::make_shared<HWiNFODriver>();
    }

} // namespace sky::driver
