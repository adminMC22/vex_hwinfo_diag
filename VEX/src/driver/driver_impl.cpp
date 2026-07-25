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

namespace sky::driver {

    // ---- HWiNFO64 device interface ----
    //
    // HWiNFO64 must be running BEFORE this cheat launches.
    // The cheat opens the existing HWiNFO device and uses it.
    // No driver loading, no service creation.
    //
    // IOCTL codes for physical memory access (hwinfo64.sys):
    //   Read:  0x9C40259C  (CTL_CODE(0x22, 0x118, 0, 0))
    //   Write: 0x9C4025A0  (CTL_CODE(0x22, 0x119, 0, 0))

    #define CTLCODE_READ  0x9C40259C
    #define CTLCODE_WRITE 0x9C4025A0

    // Page table bits for manual VA -> PA translation
    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    static const char* DEVICE_PATHS[] = {
        "\\\\.\\HWiNFO",
        "\\\\.\\HWiNFO32",
        "\\\\.\\HWiNFO64",
        "\\\\.\\HWiNFO_0",
        "\\\\.\\HWiNFO_V2",
        "\\\\.\\HWiNFOMap",
        "\\\\.\\HWiNFO_CORE",
        nullptr
    };

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ---- Driver auto-load helper ----
    // Tries to start or load the HWiNFO kernel driver via SCM
    static bool ensure_hwinfo_driver_loaded() {
        // Search multiple locations for HWiNFO driver file
        const char* SEARCH_DIRS[] = {
            "C:\\Program Files\\HWiNFO64\\",
            "C:\\Program Files (x86)\\HWiNFO\\",
            "C:\\Program Files\\HWiNFO\\",
            "C:\\Users\\Public\\",
            nullptr
        };

        std::string driver_path;

        // First: check %TEMP% for HWiNFO*.sys (modern HWiNFO versions)
        char temp_path[MAX_PATH + 1] = { 0 };
        if (GetTempPathA(MAX_PATH, temp_path) > 0) {
            std::string search_path = std::string(temp_path) + "HWiNFO*.sys";
            WIN32_FIND_DATAA find_data;
            HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
            if (hFind != INVALID_HANDLE_VALUE) {
                driver_path = std::string(temp_path) + find_data.cFileName;
                LOG_INFO(std::string("Found driver in TEMP: ") + driver_path);
                FindClose(hFind);
            }
        }

        // Second: search Program Files for HWiNFO*.sys
        if (driver_path.empty()) {
            for (int i = 0; SEARCH_DIRS[i]; i++) {
                std::string search_path = std::string(SEARCH_DIRS[i]) + "HWiNFO*.sys";
                WIN32_FIND_DATAA find_data;
                HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
                if (hFind != INVALID_HANDLE_VALUE) {
                    driver_path = std::string(SEARCH_DIRS[i]) + find_data.cFileName;
                    LOG_INFO(std::string("Found driver: ") + driver_path);
                    FindClose(hFind);
                    break;
                }
            }
        }

        // Third: try exact known filenames
        if (driver_path.empty()) {
            const char* DRIVER_FILES[] = {
                "hwinfo64.sys",
                "hwinfo.sys",
                nullptr
            };
            for (int d = 0; DRIVER_FILES[d]; d++) {
                for (int i = 0; SEARCH_DIRS[i]; i++) {
                    std::string test_path = std::string(SEARCH_DIRS[i]) + DRIVER_FILES[d];
                    DWORD attr = GetFileAttributesA(test_path.c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                        driver_path = test_path;
                        break;
                    }
                }
                if (!driver_path.empty()) break;
            }
        }

        if (driver_path.empty()) {
            LOG_ERROR("HWiNFO driver file not found anywhere on system");
            return false;
        }

        // Step 1: Try to start existing HWiNFO service
        SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (sc_manager) {
            const char* SERVICE_NAMES[] = {
                "HWiNFO_SERVICE",
                "HWiNFO64",
                "HWiNFO",
                nullptr
            };
            for (int s = 0; SERVICE_NAMES[s]; s++) {
                SC_HANDLE sc_service = OpenServiceA(sc_manager, SERVICE_NAMES[s], SERVICE_START | SERVICE_QUERY_STATUS);
                if (sc_service) {
                    SERVICE_STATUS status;
                    if (QueryServiceStatus(sc_service, &status) && status.dwCurrentState == SERVICE_STOPPED) {
                        StartService(sc_service, 0, nullptr);
                        Sleep(1000);
                    }
                    CloseServiceHandle(sc_service);
                    CloseServiceHandle(sc_manager);
                    return true;
                }
            }
            CloseServiceHandle(sc_manager);
        }

        // Step 2: No service found - install+start driver manually
        sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (!sc_manager) {
            LOG_ERROR("Failed to open SCM (need admin)");
            return false;
        }

        SC_HANDLE sc_service = CreateServiceA(sc_manager,
            "HWiNFO_SERVICE", "HWiNFO Kernel Driver",
            SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            driver_path.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

        if (!sc_service) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_EXISTS || err == ERROR_DUPLICATE_SERVICE_NAME) {
                sc_service = OpenServiceA(sc_manager, "HWiNFO_SERVICE", SERVICE_START);
            }
        }

        if (sc_service) {
            if (!StartService(sc_service, 0, nullptr)) {
                DWORD err = GetLastError();
                if (err != ERROR_SERVICE_ALREADY_RUNNING) {
                    LOG_ERROR("Failed to start HWiNFO service: " + std::to_string(err));
                    CloseServiceHandle(sc_service);
                    CloseServiceHandle(sc_manager);
                    return false;
                }
            }
            CloseServiceHandle(sc_service);
            CloseServiceHandle(sc_manager);
            Sleep(1000);
            return true;
        }

        CloseServiceHandle(sc_manager);
        return false;
    }

    static bool open_hwinfo_device() {
        // First try all known device paths
        DWORD last_err = 0;
        for (int i = 0; DEVICE_PATHS[i]; i++) {
            g_hwinfo_device = CreateFileA(DEVICE_PATHS[i],
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO(std::string("Opened device: ") + DEVICE_PATHS[i]);
                return true;
            }
            last_err = GetLastError();
            LOG_ERROR(std::string("CreateFile failed for ") + DEVICE_PATHS[i] + " (err=" + std::to_string(last_err) + ")");
        }

        // Build detailed error message
        std::string err_msg = "Failed to open HWiNFO device.\n\n"
            "Last Windows error code: " + std::to_string(last_err) + "\n"
            "(5 = Access Denied, 2 = Not Found, 32 = sharing violation)\n\n"
            "Device paths tried:\n";
        for (int i = 0; DEVICE_PATHS[i]; i++) {
            err_msg += "  " + std::string(DEVICE_PATHS[i]) + "\n";
        }
        err_msg += "\nMake sure HWiNFO64 is running as Admin RIGHT NOW\n"
            "(do not close it before launching Sky.exe)";
        MessageBoxA(0, err_msg.c_str(), "Driver Diagnostics", MB_OK | MB_ICONWARNING);

        // Try to auto-load the HWiNFO driver
        LOG_INFO("Device not found, attempting to load HWiNFO driver...");
        if (ensure_hwinfo_driver_loaded()) {
            Sleep(500);
            for (int i = 0; DEVICE_PATHS[i]; i++) {
                g_hwinfo_device = CreateFileA(DEVICE_PATHS[i],
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                    LOG_INFO(std::string("Opened device (after driver load): ") + DEVICE_PATHS[i]);
                    return true;
                }
            }
        }

        LOG_ERROR("Failed to open any HWiNFO device");
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

        // ---- Setup / management ----

        bool setup() override {
            LOG_INFO("Connecting to HWiNFO64 device...");

            if (!open_hwinfo_device()) {
                LOG_ERROR(
                    "Could not open HWiNFO device.\n"
                    "  Make sure HWiNFO64 is running as Administrator.\n"
                    "  The cheat needs HWiNFO64's kernel driver for memory access.");
                return false;
            }

            m_initialized = true;
            LOG_INFO("HWiNFO64 connection established");
            return true;
        }

        void unload() override {
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                CloseHandle(g_hwinfo_device);
                g_hwinfo_device = INVALID_HANDLE_VALUE;
            }
            m_initialized = false;
            LOG_INFO("HWiNFO device closed");
        }

        bool is_valid() const override {
            return m_initialized && g_hwinfo_device != INVALID_HANDLE_VALUE;
        }

        // ---- Process management ----

        bool attach_process(const std::wstring& process_name) override {
            // HWiNFO driver doesn't auto-attach; the caller must set DTB
            // via set_dir_base. For now, just log and return success.
            LOG_INFO("attach_process called (stub): " + std::string(process_name.begin(), process_name.end()));
            return true;
        }

        bool attach_process(uint32_t process_id) override {
            m_process_id = process_id;
            LOG_INFO("attach_process: PID " + std::to_string(process_id));
            return true;
        }

        uint32_t get_process_id() const override {
            return m_process_id;
        }

        uintptr_t get_base_address() const override {
            return m_base_address;
        }

        uintptr_t get_dtb() const override {
            return m_dtb;
        }

        bool move_mouse(uint32_t x, uint32_t y, uint16_t button_flags) override {
            // HWiNFO driver cannot move the mouse
            (void)x; (void)y; (void)button_flags;
            return false;
        }

        // ---- Memory operations ----

        bool read_memory(uintptr_t address, void* buffer, size_t size) override {
            if (!m_initialized || !buffer || size == 0) return false;

            if (m_dirbase_set && m_dtb != 0) {
                // Virtual address - translate using DTB
                uintptr_t phys = translate_virtual(address, m_dtb);
                if (phys == 0) return false;
                return read_physical(phys, buffer, size);
            }

            // Direct physical read
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

        // ---- System information ----

        uintptr_t get_kernel_base(const std::string& module_name) override {
            // Not easily available via HWiNFO — return 0
            (void)module_name;
            return 0;
        }

        // ---- Utilities ----

        void* find_pattern(uintptr_t base, const char* pattern, const char* mask) override {
            // Not implemented for HWiNFO driver
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
