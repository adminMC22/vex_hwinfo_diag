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

// Native NT APIs for direct driver loading
extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);
extern "C" NTSTATUS NTAPI RtlFormatCurrentUserKeyPath(PUNICODE_STRING Path);

#define SE_LOAD_DRIVER_PRIVILEGE 10L

namespace sky::driver {

    // Sky loads the HWiNFO driver itself using NtLoadDriver.
    // The driver file is found on disk and loaded into the kernel.
    // No need for HWiNFO64.exe to be running.
    //
    // IOCTL codes for physical memory access (hwinfo64.sys):
    //   Read:  0x9C40259C  (CTL_CODE(0x22, 0x118, 0, 0))
    //   Write: 0x9C4025A0  (CTL_CODE(0x22, 0x119, 0, 0))

    #define CTLCODE_READ  0x9C40259C
    #define CTLCODE_WRITE 0x9C4025A0

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    static const char* DEVICE_PATHS[] = {
        "\\\\.\\HWiNFO",
        "\\\\.\\HWiNFO32",
        "\\\\.\\HWiNFO64",
        "\\\\.\\HWiNFO_0",
        nullptr
    };

    static const char* SKY_SERVICE_NAME = "SkyHwiNFO";

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ---- Find ALL HWiNFO*.sys files on the system ----
    static std::vector<std::string> find_all_driver_files() {
        std::vector<std::string> results;

        // 1) %TEMP% (modern HWiNFO extracts driver here)
        char temp_path[MAX_PATH + 1] = { 0 };
        if (GetTempPathA(MAX_PATH, temp_path) > 0) {
            std::string search = std::string(temp_path) + "HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    results.push_back(std::string(temp_path) + fd.cFileName);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }

        // 2) Program Files trees (walked recursively up to 2 levels)
        const char* dirs[] = {
            "C:\\Program Files\\HWiNFO64\\",
            "C:\\Program Files (x86)\\HWiNFO\\",
            "C:\\Program Files\\HWiNFO\\",
            "C:\\HWiNFO64\\",
            nullptr
        };
        for (int i = 0; dirs[i]; i++) {
            std::string search = std::string(dirs[i]) + "HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    results.push_back(std::string(dirs[i]) + fd.cFileName);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }

        // 3) SystemRoot\\System32\\drivers (some installs put it here)
        char sys_dir[MAX_PATH + 1] = { 0 };
        if (GetSystemDirectoryA(sys_dir, MAX_PATH) > 0) {
            std::string search = std::string(sys_dir) + "\\HWiNFO*.sys";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(search.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    results.push_back(std::string(sys_dir) + "\\" + fd.cFileName);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }

        return results;
    }

    // ---- Load driver ourselves using NtLoadDriver ----
    // Tries every driver file found, one by one.
    static bool load_driver_ourselves() {
        std::vector<std::string> drivers = find_all_driver_files();

        if (drivers.empty()) {
            MessageBoxA(0,
                "No HWiNFO*.sys driver file found anywhere.\n\n"
                "Searched:\n"
                "  %TEMP%\\HWiNFO*.sys\n"
                "  C:\\Program Files\\HWiNFO64\\\n"
                "  C:\\Program Files (x86)\\HWiNFO\\\n"
                "  C:\\Windows\\System32\\drivers\\\n\n"
                "Run HWiNFO64 at least once so it extracts the driver.",
                "Sky Driver", MB_OK | MB_ICONERROR);
            return false;
        }

        // Enable SeLoadDriverPrivilege
        BOOLEAN old = FALSE;
        RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &old);

        std::string reg_path = "SYSTEM\\CurrentControlSet\\Services\\";
        reg_path += SKY_SERVICE_NAME;

        // Try each driver file until one works
        for (const auto& driver_file : drivers) {
            LOG_INFO(std::string("Trying driver: ") + driver_file);

            // Verify file exists and is readable
            DWORD attr = GetFileAttributesA(driver_file.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) {
                LOG_ERROR("  File not accessible, skipping");
                continue;
            }

            // Create registry service entry
            HKEY hKey;
            LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE, reg_path.c_str(), 0, nullptr,
                0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
            if (rc != ERROR_SUCCESS) {
                LOG_ERROR("  Failed to create registry key");
                continue;
            }

            // Set ImagePath as NT-style path: \??\C:\path\to\driver.sys
            // Must use REG_SZ (not REG_EXPAND_SZ) because \??\ is NT path, not DOS
            std::string img_path = "\\??\\" + driver_file;
            RegSetValueExA(hKey, "ImagePath", 0, REG_SZ,
                (const BYTE*)img_path.c_str(),
                (DWORD)(img_path.length() + 1));
            DWORD type = 1;  // SERVICE_KERNEL_DRIVER
            RegSetValueExA(hKey, "Type", 0, REG_DWORD,
                (const BYTE*)&type, sizeof(type));
            DWORD start = 3;  // SERVICE_DEMAND_START
            RegSetValueExA(hKey, "Start", 0, REG_DWORD,
                (const BYTE*)&start, sizeof(start));
            DWORD err_ctrl = 0;
            RegSetValueExA(hKey, "ErrorControl", 0, REG_DWORD,
                (const BYTE*)&err_ctrl, sizeof(err_ctrl));
            RegCloseKey(hKey);

            // Build the full registry path NtLoadDriver wants
            std::wstring wsvc = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
            wsvc += std::wstring(SKY_SERVICE_NAME,
                SKY_SERVICE_NAME + strlen(SKY_SERVICE_NAME));

            UNICODE_STRING u;
            u.Buffer = (PWSTR)wsvc.c_str();
            u.Length = (USHORT)(wsvc.length() * sizeof(wchar_t));
            u.MaximumLength = u.Length + sizeof(wchar_t);

            NTSTATUS st = NtLoadDriver(&u);

            // STATUS_IMAGE_ALREADY_LOADED = 0xC000010E (driver already in memory)
            // That's actually fine - means it loaded previously
            if (NT_SUCCESS(st) || st == (NTSTATUS)0xC000010E) {
                Sleep(1000);
                LOG_INFO("  Driver loaded OK");
                return true;
            }

            // Failed - show diagnostic with exact paths
            std::stringstream ss;
            ss << std::hex << (unsigned long)st;
            std::string msg = "NtLoadDriver failed: 0x" + ss.str() + "\n\n";
            msg += "Driver file:\n  " + driver_file + "\n\n";
            msg += "ImagePath (registry):\n  " + img_path + "\n\n";
            msg += "Service path (NT):\n  ";
            // Convert wsvc to string for display
            std::string svc_str(wsvc.begin(), wsvc.end());
            msg += svc_str + "\n\n";
            msg += "Reg key:\n  HKLM\\" + reg_path + "\n\n";
            if (st == (NTSTATUS)0xC0000034) {
                msg += "STATUS_OBJECT_NAME_NOT_FOUND\n";
                msg += "Registry key or ImagePath not found.\n";
                msg += "Check the ImagePath value above.";
            } else if (st == (NTSTATUS)0xC000026C) {
                msg += "STATUS_DRIVER_FAILED_TO_LOAD\n";
                msg += "Driver signature/security issue.";
            } else if (st == (NTSTATUS)0xC000010E) {
                msg += "STATUS_DRIVER_BLOCKED\n";
                msg += "Vanguard or HVCI blocking this driver.";
            } else if (st == (NTSTATUS)0xC0000022) {
                msg += "STATUS_ACCESS_DENIED\n";
                msg += "Not running as Administrator.";
            }
            MessageBoxA(0, msg.c_str(), "Sky Driver Diagnostic",
                MB_OK | MB_ICONERROR);
            LOG_ERROR("  NtLoadDriver failed: 0x" + ss.str());

            // Clean up registry for next attempt
            RegDeleteKeyA(HKEY_LOCAL_MACHINE, reg_path.c_str());

            // Continue to next driver file
        }

        // All attempts failed - build detailed error message
        std::string msg = "NtLoadDriver failed for all driver files found.\n\n";
        msg += "Files tried:\n";
        for (const auto& f : drivers) {
            msg += "  ";
            msg += f;
            msg += "\n";
        }
        msg += "\nLast error: 0x";
        std::stringstream ss;
        ss << std::hex << 0xC0000034;
        msg += ss.str();
        msg += "\n\nTroubleshooting:\n";
        msg += "- Make sure Sky.exe is Run as Administrator\n";
        msg += "- Check if Vanguard is blocking driver loading\n";
        msg += "- Try running HWiNFO64 as Admin first to test the driver\n";
        MessageBoxA(0, msg.c_str(), "Sky Driver Load Failed",
            MB_OK | MB_ICONERROR);
        return false;
    }

    // ---- Unload driver we loaded ----
    static void unload_driver_ourselves() {
        std::wstring wsvc = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        wsvc += std::wstring(SKY_SERVICE_NAME, SKY_SERVICE_NAME + strlen(SKY_SERVICE_NAME));
        UNICODE_STRING u;
        u.Buffer = (PWSTR)wsvc.c_str();
        u.Length = (USHORT)(wsvc.length() * sizeof(wchar_t));
        u.MaximumLength = u.Length + sizeof(wchar_t);

        NtUnloadDriver(&u);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE,
            ("SYSTEM\\CurrentControlSet\\Services\\" + std::string(SKY_SERVICE_NAME)).c_str());
    }

    // ---- Open device: try live, else load driver ourselves ----
    static bool open_hwinfo_device() {
        // Try opening the device. If HWiNFO64 is already running,
        // the device might already exist (created by HWiNFO itself).
        DWORD last_err = 0;
        for (int i = 0; DEVICE_PATHS[i]; i++) {
            g_hwinfo_device = CreateFileA(DEVICE_PATHS[i],
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO(std::string("Opened device (live): ") + DEVICE_PATHS[i]);
                return true;
            }
            last_err = GetLastError();
        }

        // Device not found - load the driver ourselves
        LOG_INFO("Device not found, loading HWiNFO driver ourselves...");
        if (!load_driver_ourselves()) {
            return false;
        }

        // Retry opening device now that driver is loaded
        for (int i = 0; DEVICE_PATHS[i]; i++) {
            g_hwinfo_device = CreateFileA(DEVICE_PATHS[i],
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO(std::string("Opened device (after NtLoadDriver): ") + DEVICE_PATHS[i]);
                return true;
            }
        }

        // Driver loaded successfully but device still not accessible.
        // This usually means the driver creates a different device name
        // than the ones we tried.
        std::string err = "Driver loaded but device not accessible.\n";
        err += "Last CreateFile error: " + std::to_string(GetLastError()) + "\n\n";
        err += "Device names tried:\n";
        for (int i = 0; DEVICE_PATHS[i]; i++) {
            err += "  ";
            err += DEVICE_PATHS[i];
            err += "\n";
        }
        err += "\nNeed to reverse engineer HWiNFO_x64_215.sys\n";
        err += "to find the real device name.";
        MessageBoxA(0, err.c_str(), "Sky Driver", MB_OK | MB_ICONERROR);
        unload_driver_ourselves();
        return false;
    }

    // ---- Physical memory read via HWiNFO IOCTL ----

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
                LOG_ERROR("Could not open HWiNFO device.");
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
            unload_driver_ourselves();
            m_initialized = false;
            LOG_INFO("HWiNFO device closed and driver unloaded");
        }

        bool is_valid() const override {
            return m_initialized && g_hwinfo_device != INVALID_HANDLE_VALUE;
        }

        // ---- Process management ----

        bool attach_process(const std::wstring& process_name) override {
            LOG_INFO("attach_process called (stub): "
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

        // ---- Memory operations ----

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

        // ---- System info ----

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
