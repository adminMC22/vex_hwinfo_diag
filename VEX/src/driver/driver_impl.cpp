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
#include <mutex>
#include <format>

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
    // Bulk physical read (RTCore64-style buffered IOCTL; the ThrottleStop
    // driver family also implements it).
    //   Input : struct { UINT64 phys_addr; UINT32 size; }   (12 bytes)
    //   Output: the requested bytes (Method::Buffered)
    #define TS_IOCTL_READ_BULK 0x9C40258C

    static uint64_t s_total_phys = 0;

    static uint64_t get_total_phys() {
        if (s_total_phys == 0) {
            MEMORYSTATUSEX ms{ sizeof(ms) };
            if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0) {
                s_total_phys = ms.ullTotalPhys;
            } else {
                // Conservative fallback: 1GB — never probe above this
                s_total_phys = 0x40000000ULL;
            }
        }
        return s_total_phys;
    }

    // Is this physical address range inside installed RAM?
    // ThrottleStop's driver uses MmMapIoSpace internally; probing an
    // address beyond RAM (or in an MMIO hole) can bugcheck the system
    // with MEMORY_MANAGEMENT. Reject anything outside installed RAM.
    static bool pa_valid(uintptr_t pa, size_t size) {
        uint64_t total = get_total_phys();
        if (size == 0) return false;
        if (pa >= total) return false;
        if (pa + size < pa) return false;              // overflow
        if (pa + size > total) return false;
        return true;
    }

    // Single bulk IOCTL read. Callers must validate pa/size first.
    static bool read_physical_bulk(uintptr_t phys_addr, void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;
        if (size == 0 || size > 0x10000) return false;  // keep IOCTL buffers sane

        struct { uint64_t phys; uint32_t len; } in = { (uint64_t)phys_addr, (uint32_t)size };
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(g_hwinfo_device, TS_IOCTL_READ_BULK,
            &in, sizeof(in), buffer, (DWORD)size, &returned, nullptr);
        if (!ok) return false;
        return returned == size;
    }

    // Bulk capability is probed once with reads of the first page (real-mode
    // IVT / BIOS area — always present and safe). We discover the LARGEST
    // chunk the driver accepts: 64KB when available (4GB scan = 65K IOCTLs,
    // ~seconds), else 4KB (1M IOCTLs, ~a minute), else only 1-byte reads.
    static bool s_bulk_probed = false;
    static size_t s_bulk_max = 0;  // 0 = bulk unsupported

    static size_t bulk_max_chunk() {
        if (!s_bulk_probed) {
            s_bulk_probed = true;
            std::vector<uint8_t> probe(0x10000);
            if (pa_valid(0x1000, 0x10000) && read_physical_bulk(0x1000, probe.data(), 0x10000)) {
                s_bulk_max = 0x10000;
            } else if (pa_valid(0x1000, 0x1000) && read_physical_bulk(0x1000, probe.data(), 0x1000)) {
                s_bulk_max = 0x1000;
            }
            LOG_INFO(s_bulk_max
                ? "bulk read IOCTL: supported (max chunk 0x" + std::format("{:x}", s_bulk_max) + ")"
                : "bulk read IOCTL: NOT supported (1-byte fallback)");
        }
        return s_bulk_max;
    }

    static bool read_physical(uintptr_t phys_addr, void* buffer, size_t size) {
        if (g_hwinfo_device == INVALID_HANDLE_VALUE) return false;
        if (!pa_valid(phys_addr, size)) return false;

        // One-shot log of the FIRST physical read attempt that occurs AT OR
        // INSIDE the page-walk path, so we can verify the IOCTL actually
        // returns sensible bytes for the System PML4 region (the page where
        // we found pml4e[0x1F0] present).
        static bool s_first_phys_logged = false;
        if (!s_first_phys_logged) {
            s_first_phys_logged = true;
            write_state_log("read_phys_first pa=0x" + std::format("{:x}", phys_addr) +
                " sz=" + std::to_string(size));
        }

        // === ThrottleStop backend ===
        if (g_backend == BACKEND_THROTTLESTOP) {
            // Multi-byte reads up to the driver's bulk limit use the bulk
            // IOCTL (probed once). This is what makes physical scans
            // feasible — the 1-byte path below would take hours over a
            // multi-GB range.
            if (size > 1 && size <= bulk_max_chunk()) {
                if (read_physical_bulk(phys_addr, buffer, size))
                    return true;
                // Bulk failed for this range; fall through to the 1-byte
                // loop so small cross-range reads still work.
            }
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
    // Per-stage failure logging for translate_virtual — re-logs each stage
    // every tick (no bitmask suppression) so the user sees what's happening
    // across retries. The 'last_va' tag lets us see if the failing VA changes.
    // Also logs a SUCCESS line once so we know the walk landed.
    static std::mutex s_walk_log_mtx;
    static uintptr_t s_walk_last_vaddr = 0;
    static unsigned s_walk_log_count = 0;
    static bool s_walk_success_logged = false;
    static void walk_log(unsigned stage, uintptr_t a, uintptr_t b) {
        std::lock_guard<std::mutex> lock(s_walk_log_mtx);
        if (a != s_walk_last_vaddr) {
            s_walk_last_vaddr = a;
            s_walk_log_count = 0;
        }
        if (s_walk_log_count >= 4) return;
        s_walk_log_count++;
        std::string msg = "attach=TRY walk_fail stage=" + std::to_string(stage) +
            " a=0x" + std::format("{:x}", a) +
            " b=0x" + std::format("{:x}", b);
        write_state_log(msg);
    }
    static void walk_success_log(uintptr_t vaddr, uintptr_t dirbase, uintptr_t phys) {
        std::lock_guard<std::mutex> lock(s_walk_log_mtx);
        if (s_walk_success_logged) return;
        s_walk_success_logged = true;
        std::string msg = "attach=TRY walk_ok vaddr=0x" + std::format("{:x}", vaddr) +
            " dirbase=0x" + std::format("{:x}", dirbase) +
            " phys=0x" + std::format("{:x}", phys);
        write_state_log(msg);
    }

    static uintptr_t translate_virtual(uintptr_t vaddr, uintptr_t dirbase) {
        if (!dirbase) { walk_log(0, vaddr, dirbase); return 0; }

        uintptr_t pml4e = 0, pml4i = (vaddr >> 39) & 0x1FF;
        if (!read_physical(dirbase + pml4i * 8, &pml4e, 8) || !(pml4e & 1)) {
            walk_log(1, dirbase + pml4i * 8, pml4e);
            return 0;
        }

        uintptr_t pdpte = 0, pdpti = (vaddr >> 30) & 0x1FF;
        if (!read_physical((pml4e & PAGE_MASK_4KB) + pdpti * 8, &pdpte, 8) || !(pdpte & 1)) {
            walk_log(2, (pml4e & PAGE_MASK_4KB) + pdpti * 8, pdpte);
            return 0;
        }
        if (pdpte & (1 << 7)) {
            uintptr_t phys = (pdpte & PAGE_MASK_1GB) | (vaddr & 0x3FFFFFFF);
            walk_success_log(vaddr, dirbase, phys);
            return phys;
        }

        uintptr_t pde = 0, pdi = (vaddr >> 21) & 0x1FF;
        if (!read_physical((pdpte & PAGE_MASK_4KB) + pdi * 8, &pde, 8) || !(pde & 1)) {
            walk_log(3, (pdpte & PAGE_MASK_4KB) + pdi * 8, pde);
            return 0;
        }
        if (pde & (1 << 7)) {
            uintptr_t phys = (pde & PAGE_MASK_2MB) | (vaddr & 0x1FFFFF);
            walk_success_log(vaddr, dirbase, phys);
            return phys;
        }

        uintptr_t pte = 0, pti = (vaddr >> 12) & 0x1FF;
        if (!read_physical((pde & PAGE_MASK_4KB) + pti * 8, &pte, 8) || !(pte & 1)) {
            walk_log(4, (pde & PAGE_MASK_4KB) + pti * 8, pte);
            return 0;
        }
        uintptr_t phys = (pte & PAGE_MASK_4KB) | (vaddr & 0xFFF);
        walk_success_log(vaddr, dirbase, phys);
        return phys;
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

    // Minimal diagnostics: append one line to %TEMP%\app.log AND to
    // <exe dir>\app.log (users look next to the exe first). The log is
    // wiped once per process start so each run is clean and self-evident.
    // Kill switch wipes it on panic.
    void write_state_log(const std::string& line) {
        static std::mutex s_log_mutex;
        std::lock_guard<std::mutex> lock(s_log_mutex);

        static bool s_log_reset = false;
        char tmp[MAX_PATH + 1] = { 0 };
        if (!GetTempPathA(MAX_PATH, tmp)) return;
        std::string temp_path = std::string(tmp) + "app.log";

        char exe[MAX_PATH + 1] = { 0 };
        std::string exe_path;
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
            std::string full(exe);
            auto slash = full.find_last_of('\\');
            if (slash != std::string::npos) {
                exe_path = full.substr(0, slash + 1) + "app.log";
            }
        }

        if (!s_log_reset) {
            s_log_reset = true;
            DeleteFileA(temp_path.c_str());
            if (!exe_path.empty()) DeleteFileA(exe_path.c_str());
        }

        auto append = [](const std::string& path, const std::string& msg) {
            if (path.empty()) return;
            HANDLE h = CreateFileA(path.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            WriteFile(h, msg.c_str(), (DWORD)msg.size(), &written, nullptr);
            CloseHandle(h);
        };

        std::string msg = line + "\r\n";
        append(temp_path, msg);
        append(exe_path, msg);
    }

    // Verify a candidate kernel physical base: read the PE header and
    // check SizeOfImage against ntoskrnl's size from the module list.
    // All reads are within the candidate's first 4KB, validated by
    // read_physical's RAM bound.
    static bool pe_matches(uintptr_t pa, uintptr_t expected_size) {
        uint32_t pe_off = 0;
        if (!read_physical(pa + 0x3C, &pe_off, sizeof(pe_off)) || pe_off >= 0x1000)
            return false;
        uint32_t pe_sig = 0;
        if (!read_physical(pa + pe_off, &pe_sig, sizeof(pe_sig)))
            return false;
        if ((pe_sig & 0xFFFF) != 0x4550) // "PE"
            return false;
        if (expected_size != 0) {
            uint32_t size_of_image = 0;
            // SizeOfImage is at PE+0x50 in the optional header
            if (!read_physical(pa + pe_off + 0x50, &size_of_image, sizeof(size_of_image)))
                return false;
            if (size_of_image == 0) return false;
            // Tolerate a few pages of difference (module list size is
            // the loaded size, SizeOfImage is the image size).
            uintptr_t diff = (uintptr_t)(size_of_image > expected_size
                ? size_of_image - expected_size
                : expected_size - size_of_image);
            if (diff > 0x300000) return false;  // 3MB tolerance for 2MB-section alignment
        }
        return true;
    }

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

        // === Approach: fast heuristic tries, then limited scan ===
        // The kernel image is NOT mapped at identity + phys (identity map
        // is 0xFFFFF78000000000). On Win10 x64 the ntoskrnl image lives in
        // the 0xFFFFF80000000000 region and its physical load base is
        // randomized somewhere in the low physical range (16MB-1GB).
        //
        // Strategy:
        //  1. Try the identity-map heuristic first (a few reads, cheap).
        //  2. If that misses, scan physical memory [16MB, min(1GB,total_phys)]
        //     at 2MB granularity looking for an MZ whose PE SizeOfImage
        //     matches ntoskrnl's size from the module list. All scan
        //     addresses are validated against installed RAM by read_physical,
        //     so this can never probe an invalid physical address.
        uintptr_t ntk_size = 0;
        for (ULONG i = 0; i < modules->Count; ++i) {
            auto& mod = modules->Module[i];
            std::string name((char*)mod.FullPathName + mod.OffsetToFileName);
            if (name == "ntoskrnl.exe") {
                ntk_size = mod.ImageSize;
                break;
            }
        }

        uint8_t verify[2];
        bool found = false;

        // 1) Identity-map heuristic (cheap; works when phys load base
        //    happens to align with the virtual delta). The kernel image
        //    region identity base is 0xFFFFF80000000000 (NOT
        //    0xFFFFF78000000000 — that is KUSER_SHARED_DATA).
        static const int64_t k_heuristic_tweaks[] = {
            0, -0x100000, 0x100000, -0x80000, 0x80000,
        };
        for (int64_t tweak : k_heuristic_tweaks) {
            uintptr_t guess = (uintptr_t)((int64_t)(s_kernel_vbase - 0xFFFFF80000000000ULL) + tweak);
            if (read_physical(guess, verify, 2) && verify[0] == 'M' && verify[1] == 'Z') {
                if (pe_matches(guess, ntk_size)) {
                    s_kernel_pbase = guess;
                    found = true;
                    LOG_INFO("kernel_phys_offset: found via heuristic at phys=0x" +
                        std::format("{:x}", s_kernel_pbase));
                    break;
                }
            }
        }

        // 2) Limited physical scan: [16MB, min(2GB, total_phys)] at 2MB steps.
        //    This is the range where the ntoskrnl image is loaded on Win10 x64.
        if (!found) {
            LOG_INFO("kernel_phys_offset: heuristic miss, scanning [16MB..2GB) at 2MB...");
            uint64_t scan_limit = get_total_phys();
            if (scan_limit > 0x80000000ULL) scan_limit = 0x80000000ULL;
            for (uintptr_t pa = 0x1000000; pa + 0x200000 <= scan_limit; pa += 0x200000) {
                if (read_physical(pa, verify, 2) && verify[0] == 'M' && verify[1] == 'Z') {
                    if (pe_matches(pa, ntk_size)) {
                        s_kernel_pbase = pa;
                        found = true;
                        LOG_INFO("kernel_phys_offset: found via scan at phys=0x" +
                            std::format("{:x}", s_kernel_pbase));
                        break;
                    }
                }
            }
        }

        if (!found) {
            LOG_WARNING("kernel_phys_offset: could not locate kernel in physical memory");
            LOG_WARNING("kernel scan: not found");
            write_state_log("kernel_offset=FAIL");
            return false;
        }

        LOG_INFO("ntoskrnl physical: 0x" + std::format("{:x}", s_kernel_pbase));
        write_state_log("kernel_offset=OK vbase=0x" + std::format("{:x}", s_kernel_vbase) +
            " pbase=0x" + std::format("{:x}", s_kernel_pbase));

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

        void set_attached(uint32_t pid, uintptr_t base) override {
            m_pid = pid;
            m_base = base;
            LOG_INFO("set_attached: pid=" + std::to_string(pid) + " base=0x" +
                std::format("{:x}", base));
        }

        uint32_t get_process_id() const override { return m_pid; }
        uintptr_t get_base_address() const override { return m_base; }
        uintptr_t get_dtb() const override { return m_dtb; }

        bool read_physical(uintptr_t phys_addr, void* buffer, size_t size) override {
            return ::sky::driver::read_physical(phys_addr, buffer, size);
        }

        size_t max_bulk_chunk() const override { return ::sky::driver::bulk_max_chunk(); }

        bool move_mouse(uint32_t, uint32_t, uint16_t) override { return false; }

        bool read_memory(uintptr_t addr, void* buf, size_t sz) override {
            if (!m_init || !buf || !sz) return false;
            // One-shot log of the FIRST read after attach setup so we can
            // verify m_dtb is what we expect at the actual read point.
            static bool s_first_read_logged = false;
            if (!s_first_read_logged) {
                s_first_read_logged = true;
                write_state_log("read_first addr=0x" + std::format("{:x}", addr) +
                    " dtb=0x" + std::format("{:x}", m_dtb) +
                    " dtb_set=" + std::to_string(m_dtb_set ? 1 : 0));
            }
            if (m_dtb_set && m_dtb) {
                uintptr_t phys = translate_virtual(addr, m_dtb);
                if (!phys) return false;
                return read_physical(phys, buf, sz);
            }
            // Kernel virtual address — translate via kernel offset
            if (addr >= 0xFFFF800000000000ULL) {
                uintptr_t phys = kernel_va_to_pa(addr);
                if (phys) return read_physical(phys, buf, sz);
                return false;
            }
            // No DTB and not a kernel address: this is a user-space virtual
            // address that we cannot translate. NEVER pass it to
            // read_physical() as a raw physical address — a game VA like
            // 0x1F9A2C00000 would probe 33GB+ of physical space and
            // bugcheck the system (MEMORY_MANAGEMENT).
            return false;
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
            m_dtb = dir ? (uintptr_t)dir : 0;
            m_dtb_set = (dir != nullptr);
            if (dir) {
                LOG_DEBUG("DTB set: 0x" + std::format("{:x}", m_dtb));
            } else {
                LOG_DEBUG("DTB cleared");
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