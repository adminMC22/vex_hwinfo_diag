#include "../../include/driver/driver_context.hpp"
#include "../../include/driver/driver_data.h"
#include "../../include/utils/logger.hpp"
#include <Windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <ntstatus.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>

#pragma comment(lib, "ntdll.lib")

// Module information structures for NtQuerySystemInformation
typedef struct _SYSTEM_MODULE_ENTRY {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} SYSTEM_MODULE_ENTRY, *PSYSTEM_MODULE_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG Count;
    SYSTEM_MODULE_ENTRY Module[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

// NT APIs for driver loading
extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);

#define SE_LOAD_DRIVER_PRIVILEGE 10L

namespace sky::driver {

    // --- ThrottleStop IOCTL ---
    // Device: \\.\ThrottleStop
    // IOCTL: 0x80006498 — read 1 byte from physical address
    // Input:  UINT64 (8 bytes) = physical address
    // Output: BYTE  (1 byte)   = value at address
    #define TS_IOCTL_READ  0x80006498

    // --- Backend selection ---
    static enum { BACKEND_NONE, BACKEND_THROTTLESTOP } g_backend = BACKEND_NONE;
    // struct RTCPhysMem {
    //     UINT64 phys_address;  // Physical address to read/write
    //     UINT32 size;          // Size in bytes
    //     BYTE   data[];        // For write: source data, for read: dest buffer
    // };

    // RTCore64 actual IOCTL codes (from reverse-engineering RTCore64.sys)
    // Read physical memory: IOCTL 0x9C40258C, Method::Buffered
    // Write physical memory: IOCTL 0x9C402590, Method::Buffered
    //
    // The real structure for reads:
    //   Input:  struct { UINT64 phys_addr; UINT32 size; } (12 bytes input)
    //   Output: BYTE[size] containing the read data
    // OR
    //   Single buffer with structured format:
    //   [0..7]   = 0 (padding/reserved)
    //   [8..15]  = physical address
    //   [16..19] = size to read
    //   [20..]   = output buffer (must be large enough for 'size' bytes)

    #define RTC_IOCTL_READ  0x9C40258C
    #define RTC_IOCTL_WRITE 0x9C402590

    // Alternative IOCTL codes (some versions use different codes)
    #define RTC_IOCTL_READ_ALT  0x9C406000
    #define RTC_IOCTL_WRITE_ALT 0x9C406004

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ============================================================
    // Load and connect ThrottleStop driver
    // ============================================================
    static std::string write_embedded_driver() {
        // Write embedded ThrottleStop driver bytes to a random temp path
        char temp_dir[MAX_PATH + 1] = { 0 };
        if (!GetTempPathA(MAX_PATH, temp_dir)) {
            // Fallback to Windows\\Temp
            strcpy(temp_dir, "C:\\Windows\\Temp\\");
        }

        // Generate random filename (no "throttlestop" in the name)
        char filename[MAX_PATH + 1] = { 0 };
        srand(GetTickCount() ^ (DWORD)(uintptr_t)&filename);
        snprintf(filename, sizeof(filename), "%sdrv_%08x.tmp",
            temp_dir, rand() ^ (DWORD)GetTickCount());

        LOG_INFO("Extracting driver to: " + std::string(filename));

        HANDLE hFile = CreateFileA(filename,
            GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            LOG_ERROR("Failed to create temp driver file: GLE=" +
                std::to_string(GetLastError()));
            return "";
        }

        DWORD written = 0;
        BOOL ok = WriteFile(hFile, THROTTLESTOP_SYS_DATA,
            THROTTLESTOP_SYS_SIZE, &written, NULL);
        CloseHandle(hFile);

        if (!ok || written != THROTTLESTOP_SYS_SIZE) {
            LOG_ERROR("Failed to write embedded driver");
            DeleteFileA(filename);
            return "";
        }

        LOG_INFO("Embedded driver extracted (" +
            std::to_string(written) + " bytes)");
        return std::string(filename);
    }

    static std::string find_throttlestop_driver() {
        // Phase 1: Extract embedded driver to random temp path
        std::string embedded = write_embedded_driver();
        if (!embedded.empty()) return embedded;

        LOG_INFO("Embedded extraction failed, trying disk lookup...");

        // Phase 2: Look alongside our exe (legacy fallback)
        char module[MAX_PATH + 1] = { 0 };
        GetModuleFileNameA(NULL, module, MAX_PATH);
        char* last_slash = strrchr(module, '\\');
        if (last_slash) {
            size_t dir_len = last_slash - module;
            module[dir_len] = 0;
            std::string candidate = std::string(module) + "\\throttlestop.sys";
            if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
        }
        return "";
    }

    static bool connect_throttlestop() {
        LOG_INFO("=== Trying ThrottleStop backend ===");

        // Try to open existing device first
        HANDLE h = CreateFileA("\\\\.\\ThrottleStop",
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            LOG_INFO("ThrottleStop device already open");
            g_hwinfo_device = h;
            g_backend = BACKEND_THROTTLESTOP;
            return true;
        }
        LOG_INFO("Device not open — trying to load driver");

        // Find the driver file
        std::string sys_path = find_throttlestop_driver();
        if (sys_path.empty()) {
            LOG_WARNING("throttlestop.sys not found on disk");
            return false;
        }
        LOG_INFO("Found throttlestop.sys at: " + sys_path);

        // Load via SC Manager (reliable method)
        SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (!scm) {
            LOG_WARNING("OpenSCManager failed — trying NtLoadDriver");
            // Fall through to NtLoadDriver below
        } else {
            // Remove any stale service
            SC_HANDLE svc = OpenServiceA(scm, "ThrottleStop", SERVICE_ALL_ACCESS);
            if (svc) {
                SERVICE_STATUS ss;
                ControlService(svc, SERVICE_CONTROL_STOP, &ss);
                DeleteService(svc);
                CloseServiceHandle(svc);
            }

            std::string nt_path = "\\??\\" + sys_path;
            svc = CreateServiceA(scm, "ThrottleStop", "ThrottleStop",
                SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                nt_path.c_str(), NULL, NULL, NULL, NULL, NULL);
            if (svc) {
                BOOL ok = StartServiceA(svc, 0, NULL);
                CloseServiceHandle(svc);
                if (ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
                    LOG_INFO("ThrottleStop driver loaded via SC Manager");
                }
            }
            CloseServiceHandle(scm);
        }

        // If SC Manager failed, try NtLoadDriver
        if (CreateFileA("\\\\.\\ThrottleStop", GENERIC_READ, 0, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE) {
            BOOLEAN priv_old = FALSE;
            RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &priv_old);

            std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\ThrottleStop";
            std::string img_path = "\\??\\" + sys_path;

            // Create registry entries
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Services\\ThrottleStop",
                    0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hKey, "ImagePath", 0, REG_SZ,
                    (const BYTE*)img_path.c_str(), (DWORD)(img_path.length() + 1));
                DWORD dwType = 1;
                RegSetValueExA(hKey, "Type", 0, REG_DWORD, (BYTE*)&dwType, sizeof(dwType));
                DWORD dwStart = 3;
                RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&dwStart, sizeof(dwStart));
                DWORD dwErr = 0;
                RegSetValueExA(hKey, "ErrorControl", 0, REG_DWORD, (BYTE*)&dwErr, sizeof(dwErr));
                RegCloseKey(hKey);
            }

            UNICODE_STRING us;
            us.Buffer = (PWSTR)wreg.c_str();
            us.Length = (USHORT)(wreg.length() * sizeof(wchar_t));
            us.MaximumLength = us.Length + sizeof(wchar_t);
            NTSTATUS st = NtLoadDriver(&us);
            LOG_INFO("NtLoadDriver: 0x" + std::format("{:08x}", (unsigned long)st));
        }

        // Try opening the device again
        Sleep(500);
        h = CreateFileA("\\\\.\\ThrottleStop",
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            LOG_ERROR("Cannot open \\\\.\\ThrottleStop after loading");
            return false;
        }

        g_hwinfo_device = h;
        g_backend = BACKEND_THROTTLESTOP;
        LOG_INFO("ThrottleStop backend ready");
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
    // Open ThrottleStop device (only backend)
    // ============================================================
    static bool open_hwinfo_device() {
        // Seed RNG for read pattern jitter
        srand(GetTickCount());

        LOG_INFO("=== ThrottleStop only (no fallback drivers) ===");
        if (connect_throttlestop()) {
            LOG_INFO("Connected to ThrottleStop backend");
            return true;
        }

        // ThrottleStop only — no RTCore, no HWiNFO, no GIO fallbacks.
        // Those drivers are easily detected by Vanguard and increase risk.
        LOG_ERROR("ThrottleStop driver not available — cannot proceed");
        LOG_ERROR("Place throttlestop.sys in Temp/ or run with driver pre-loaded");
        g_backend = BACKEND_NONE;
        g_hwinfo_device = INVALID_HANDLE_VALUE;
        return false;
    }

    // ============================================================
    // Physical memory read (ThrottleStop 1-byte IOCTL)
    // ============================================================
    //
    // IOCTL codes:
    // #define THROTTLE_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x6498, METHOD_BUFFERED, FILE_READ_ACCESS)
    #define TS_IOCTL_READ 0x80006498

    static bool read_physical(uintptr_t phys_addr, void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;

        // === ThrottleStop backend ===
        if (g_backend == BACKEND_THROTTLESTOP) {
            uint8_t* dst = (uint8_t*)buffer;
            size_t remaining = size;
            uintptr_t addr = phys_addr;

            while (remaining > 0) {
                uint64_t ts_addr = addr;
                uint8_t val = 0;
                DWORD returned = 0;

                BOOL ok = DeviceIoControl(g_hwinfo_device, TS_IOCTL_READ,
                    &ts_addr, sizeof(ts_addr),   // input: physical address
                    &val, sizeof(val),            // output: 1 byte
                    &returned, nullptr);

                if (!ok) {
                    LOG_ERROR("TS read IOCTL failed at phys=0x" +
                        std::format("{:x}", addr) + " GLE=" + std::to_string(GetLastError()));
                    return false;
                }
                *dst++ = val;
                addr++;
                remaining--;

                // Anti-pattern jitter: random 0-3ms sleep every 8-16 bytes
                if ((remaining & 0x7) == 0) {
                    DWORD jitter = (rand() % 4);
                    if (jitter) Sleep(jitter);
                }
            }
            return true;
        }

        g_backend = BACKEND_NONE;
        LOG_ERROR("read_physical: no supported backend");
        return false;
    }

    static bool write_physical(uintptr_t phys_addr, const void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;

        // ThrottleStop: no write support
        LOG_ERROR("write_physical not supported by ThrottleStop");
        return false;
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
    // Kernel virtual → physical translation via physical scan
    // ============================================================
    // RTCore64 only reads physical addresses. To read kernel virtual
    // addresses (e.g. vgk.sys data), we need the kernel virtual-to-
    // physical offset. We find it by scanning physical memory for
    // ntoskrnl.exe's PE header signature.
    static uintptr_t s_kernel_vbase = 0;
    static uintptr_t s_kernel_pbase = 0;
    static bool s_kernel_offset_ready = false;

    static bool init_kernel_phys_offset() {
        if (s_kernel_offset_ready) return s_kernel_pbase != 0;
        s_kernel_offset_ready = true;

        // Get ntoskrnl.exe virtual base from system module list
        ULONG size = 0;
        NTSTATUS status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, NULL, 0, &size);
        if (size == 0) return false;

        std::vector<uint8_t> buf(size);
        status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, buf.data(), size, &size);
        if (!NT_SUCCESS(status)) return false;

        auto modules = (PSYSTEM_MODULE_INFORMATION)buf.data();
        for (ULONG i = 0; i < modules->Count; ++i) {
            auto& mod = modules->Module[i];
            std::string name((char*)mod.FullPathName + mod.OffsetToFileName);
            if (name == "ntoskrnl.exe") {
                s_kernel_vbase = (uintptr_t)mod.ImageBase;
                LOG_INFO("ntoskrnl virtual: 0x" + std::format("{:x}", s_kernel_vbase));
                break;
            }
        }
        if (s_kernel_vbase == 0) return false;
        LOG_INFO("kernel_phys_offset: using heuristic to find kernel physical base...");

        // === Heuristic approach: no brute-force physical scan ===
        // On Win10 x64, kernel physical is typically one of these offsets from virtual:
        //   phys = virt - 0xFFFFF78000000000   (common on 22H2+ non-VBS)
        //   phys = virt - 0xFFFFF80000000000   (pre-2020 builds)
        //   phys = virt - 0xFFFFF78000000000 + 0x80000 (slight variation)
        //
        // If the heuristic fails, fall back to a LIMITED scan around the
        // expected area (128 MB range at 2 MB granularity = 64 IOCTLs).

        static const int64_t k_heuristic_tweaks[] = {
            0,                  // baseline: virt - 0xFFFFF78000000000
            -0x100000,          // -1MB
            0x100000,           // +1MB
            -0x80000,           // -512KB
            0x80000,            // +512KB
        };
        static const uint64_t k_base_deduction = 0xFFFFF78000000000ULL;

        uint8_t verify[2];
        bool found = false;

        // Try heuristic offsets first (fast)
        for (int64_t tweak : k_heuristic_tweaks) {
            uintptr_t guess = (uintptr_t)((int64_t)(s_kernel_vbase - k_base_deduction) + tweak);
            if (read_physical(guess, verify, 2) && verify[0] == 'M' && verify[1] == 'Z') {
                // Confirm with PE signature at offset 0x3C
                uint32_t pe_off = 0;
                if (read_physical(guess + 0x3C, &pe_off, sizeof(pe_off)) &&
                    pe_off < 0x1000) {
                    uint32_t pe_sig = 0;
                    if (read_physical(guess + pe_off, &pe_sig, sizeof(pe_sig)) &&
                        (pe_sig & 0xFFFF) == 0x4550) { // "PE" little-endian
                        s_kernel_pbase = guess;
                        found = true;
                        LOG_INFO("kernel_phys_offset: found via heuristic at phys=0x" +
                            std::format("{:x}", s_kernel_pbase));
                        break;
                    }
                }
            }
        }

        // Fallback: limited 2MB-granularity scan around the virtual address (max 64 tries)
        if (!found) {
            LOG_INFO("kernel_phys_offset: heuristic miss, scanning limited range...");
            // Kernel is usually within 64MB of virt - 0xFFFFF78000000000
            uintptr_t base_guess = (uintptr_t)((int64_t)(s_kernel_vbase - k_base_deduction));
            uintptr_t scan_start = (base_guess & ~0x1FFFFFULL); // 2MB align
            for (uintptr_t pa = scan_start; pa < scan_start + 0x4000000; pa += 0x200000) {
                if (read_physical(pa, verify, 2) && verify[0] == 'M' && verify[1] == 'Z') {
                    uint32_t pe_off = 0;
                    if (read_physical(pa + 0x3C, &pe_off, sizeof(pe_off)) &&
                        pe_off < 0x1000) {
                        uint32_t pe_sig = 0;
                        if (read_physical(pa + pe_off, &pe_sig, sizeof(pe_sig)) &&
                            (pe_sig & 0xFFFF) == 0x4550) {
                            s_kernel_pbase = pa;
                            found = true;
                            LOG_INFO("kernel_phys_offset: found via scan at phys=0x" +
                                std::format("{:x}", s_kernel_pbase));
                            break;
                        }
                    }
                }
            }
        }

        if (!found) {
            LOG_WARNING("kernel_phys_offset: could not locate kernel in physical memory");
            LOG_WARNING("kernel scan: not found");
            return false;
        }

        LOG_INFO("ntoskrnl physical: 0x" + std::format("{:x}", s_kernel_pbase));

        // Final verification
        if (read_physical(s_kernel_pbase, verify, 2)) {
            if (verify[0] == 'M' && verify[1] == 'Z') {
                LOG_INFO("ntoskrnl verification: MZ signature confirmed");
            } else {
                LOG_WARNING("ntoskrnl verification: no MZ at derived physical (got 0x" +
                    std::format("{:02x}{:02x}", verify[1], verify[0]) + ")");
            }
        } else {
            LOG_WARNING("ntoskrnl verification: read_physical failed at derived address");
        }
        return true;
    }

    static uintptr_t kernel_va_to_pa(uintptr_t va) {
        if (!s_kernel_offset_ready && !init_kernel_phys_offset())
            return 0;
        if (s_kernel_pbase == 0) return 0;
        return va - s_kernel_vbase + s_kernel_pbase;
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
            // Try unloading ThrottleStop service
            unload_driver_generic("ThrottleStop");
            m_init = false;
            g_backend = BACKEND_NONE;
            LOG_INFO("Kernel driver disconnected");
        }

        bool is_valid() const override {
            return m_init && g_hwinfo_device != INVALID_HANDLE_VALUE;
        }

        bool attach_process(const std::wstring& name) override {
            LOG_INFO("attach_process: " + std::string(name.begin(), name.end()));
            // Enumerate processes to find the target
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) {
                LOG_ERROR("attach_process: CreateToolhelp32Snapshot failed");
                return false;
            }
            PROCESSENTRY32W pe = { sizeof(pe) };
            bool found = false;
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                        m_pid = pe.th32ProcessID;
                        found = true;
                        break;
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            if (!found) {
                LOG_WARNING("attach_process: process \"" + std::string(name.begin(), name.end()) + "\" not found");
                return false;
            }
            LOG_INFO("attach_process: found PID=" + std::to_string(m_pid));

            // Get base address from module snapshot
            HANDLE mod_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
            if (mod_snap != INVALID_HANDLE_VALUE) {
                MODULEENTRY32W me = { sizeof(me) };
                if (Module32FirstW(mod_snap, &me)) {
                    m_base = (uintptr_t)me.modBaseAddr;
                    LOG_INFO("attach_process: base=0x" + std::format("{:x}", m_base));
                }
                CloseHandle(mod_snap);
            }
            if (m_base == 0) {
                LOG_WARNING("attach_process: could not get base address");
            }
            return m_pid != 0;
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
            // Kernel virtual address — translate via kernel offset
            if (addr >= 0xFFFF800000000000ULL) {
                uintptr_t phys = kernel_va_to_pa(addr);
                if (phys) return read_physical(phys, buf, sz);
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
        uintptr_t get_kernel_base(const std::string& module_name) override {
            // Cache kernel base results — this function is called 3x per loop tick
            static std::map<std::string, uintptr_t> s_kernel_cache;
            {
                auto it = s_kernel_cache.find(module_name);
                if (it != s_kernel_cache.end()) return it->second;
            }

            // Enumerate kernel modules via NtQuerySystemInformation
            ULONG size = 0;
            NTSTATUS status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, NULL, 0, &size);
            if (size == 0) {
                return 0;
            }

            std::vector<uint8_t> buf(size);
            status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, buf.data(), size, &size);
            if (!NT_SUCCESS(status)) {
                return 0;
            }

            auto modules = (PSYSTEM_MODULE_INFORMATION)buf.data();
            uintptr_t base = 0;
            for (ULONG i = 0; i < modules->Count; ++i) {
                auto& mod = modules->Module[i];
                std::string name((char*)mod.FullPathName + mod.OffsetToFileName);
                if (_stricmp(name.c_str(), module_name.c_str()) == 0) {
                    base = (uintptr_t)mod.ImageBase;
                    LOG_DEBUG("get_kernel_base: " + module_name + " -> 0x" + std::format("{:x}", base));
                    break;
                }
            }

            if (base == 0) {
                LOG_WARNING("get_kernel_base: " + module_name + " not found");
            }

            s_kernel_cache[module_name] = base;
            return base;
        }
        void* find_pattern(uintptr_t, const char*, const char*) override { return nullptr; }

        void set_dir_base(void* dir) override {
            if (dir) {
                m_dtb = (uintptr_t)dir;
                m_dtb_set = true;
                LOG_DEBUG("DTB set: 0x" + std::format("{:x}", m_dtb));
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