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
    static enum { BACKEND_NONE, BACKEND_THROTTLESTOP, BACKEND_ASMMAP64 } g_backend = BACKEND_NONE;

    // RTCore64 (corrected): read IOCTL is 0x80002048, ReadSize is hard-capped
    // to 1/2/4 bytes — NOT bulk. 0x9C40258C is NOT an RTCore64 code; it is
    // asmmap64's UNMAP IOCTL (see below). The old "0x9C40258C = RTCore64
    // bulk read" comment was wrong — that constant belongs to ASMMAP64.

    #define PAGE_MASK_4KB  0xFFFFFFFFFFFFF000ULL
    #define PAGE_MASK_2MB  0xFFFFFFFFFFE00000ULL
    #define PAGE_MASK_1GB  0xFFFFFC0000000000ULL

    // --- ASMMAP64 backend (ASUS 2009, section-map class) ---
    // Device: \\.\ASMMAP64  (symlink \DosDevices\ASMMAP64)
    // Opens \Device\PhysicalMemory (ZwOpenSection, rights 0xF001F) and
    // ZwMapViewOfSection maps physical RAM directly into USER space — the
    // same class as WinIo/Phymem. Holes are NOT in the section, so a hole
    // can never be mapped and can never machine-check; 64-bit phys reaches
    // all installed RAM (this box: 8GB), which ThrottleStop cannot do
    // (its MmMapIoSpace MCEs above ~427MB).
    //
    // Map IOCTL 0x9C402580 (METHOD_BUFFERED, handler verified by disasm):
    //   Input struct (>= 0x18 bytes):
    //     +0x00 u32 phys_lo
    //     +0x04 u32 phys_hi      -> 64-bit physical base
    //     +0x10 u32 size         (input length checked >= 0x18; passed as
    //                             view length)
    //   Mapped user VA written back into input[0x08..0x0C] (lo/hi dwords).
    // Unmap IOCTL 0x9C40258C: tears down the current view.
    #define ASMMAP_IOCTL_MAP   0x9C402580
    #define ASMMAP_IOCTL_UNMAP 0x9C40258C

    HANDLE g_hwinfo_device = INVALID_HANDLE_VALUE;

    // ============================================================
    // Load and connect ThrottleStop driver
    // ============================================================
    static std::string write_embedded_driver(const unsigned char* data, size_t size) {
        // Write embedded driver bytes to a random temp path
        char temp_dir[MAX_PATH + 1] = { 0 };
        if (!GetTempPathA(MAX_PATH, temp_dir)) {
            // Fallback to Windows\\Temp
            strcpy(temp_dir, "C:\\Windows\\Temp\\");
        }

        // Generate random filename (no driver name in it)
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
        BOOL ok = WriteFile(hFile, data, (DWORD)size, &written, NULL);
        CloseHandle(hFile);

        if (!ok || written != size) {
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
        std::string embedded = write_embedded_driver(THROTTLESTOP_SYS_DATA, THROTTLESTOP_SYS_SIZE);
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

    // Shared driver loading helper (defined below; forward-declared for
    // connect_throttlestop which runs before its definition).
    static HANDLE load_driver_service(const char* svc_name, const char* device_path,
                                      const std::string& sys_path);

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
            sky::driver::write_state_log("backend=THROTTLESTOP mode=ioctl");
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

        h = load_driver_service("ThrottleStop", "\\\\.\\ThrottleStop", sys_path);
        if (h == INVALID_HANDLE_VALUE) {
            LOG_ERROR("Cannot open \\\\.\\ThrottleStop after loading");
            return false;
        }

        g_hwinfo_device = h;
        g_backend = BACKEND_THROTTLESTOP;
        LOG_INFO("ThrottleStop backend ready");
        return true;
    }

    // Shared driver loading: write the .sys, create+start a service via SC
    // Manager, fall back to NtLoadDriver, then open the device.
    static HANDLE load_driver_service(const char* svc_name, const char* device_path,
                                      const std::string& sys_path) {
        // Load via SC Manager (reliable method)
        SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (!scm) {
            LOG_WARNING("OpenSCManager failed — trying NtLoadDriver");
        } else {
            // Remove any stale service
            SC_HANDLE svc = OpenServiceA(scm, svc_name, SERVICE_ALL_ACCESS);
            if (svc) {
                SERVICE_STATUS ss;
                ControlService(svc, SERVICE_CONTROL_STOP, &ss);
                DeleteService(svc);
                CloseServiceHandle(svc);
            }

            std::string nt_path = "\\??\\" + sys_path;
            svc = CreateServiceA(scm, svc_name, svc_name,
                SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                nt_path.c_str(), NULL, NULL, NULL, NULL, NULL);
            if (svc) {
                BOOL ok = StartServiceA(svc, 0, NULL);
                CloseServiceHandle(svc);
                if (ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
                    LOG_INFO(std::string(svc_name) + " driver loaded via SC Manager");
                }
            }
            CloseServiceHandle(scm);
        }

        // If SC Manager failed, try NtLoadDriver
        if (CreateFileA(device_path, GENERIC_READ, 0, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE) {
            BOOLEAN priv_old = FALSE;
            RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &priv_old);

            std::wstring wreg = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
            for (size_t i = 0; svc_name[i]; i++) wreg += (wchar_t)svc_name[i];
            std::string img_path = "\\??\\" + sys_path;

            // Create registry entries
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                    (std::string("SYSTEM\\CurrentControlSet\\Services\\") + svc_name).c_str(),
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
            LOG_INFO("NtLoadDriver(" + std::string(svc_name) + "): 0x" +
                std::format("{:08x}", (unsigned long)st));
        }

        // Try opening the device again
        Sleep(500);
        return CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    static bool connect_asmmap64() {
        LOG_INFO("=== Trying ASMMAP64 backend (section-map, all RAM) ===");

        // Try to open existing device first (kdmapper-loaded)
        HANDLE h = CreateFileA("\\\\.\\ASMMAP64",
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            LOG_INFO("ASMMAP64 device already open");
            g_hwinfo_device = h;
            g_backend = BACKEND_ASMMAP64;
            sky::driver::write_state_log("backend=ASMMAP64 mode=map");
            return true;
        }
        LOG_INFO("Device not open — trying to load driver");

        // Extract embedded asmmap64.sys and load it
        std::string sys_path = write_embedded_driver(ASMMAP64_SYS_DATA, ASMMAP64_SYS_SIZE);
        if (sys_path.empty()) {
            LOG_WARNING("asmmap64.sys extraction failed");
            return false;
        }
        LOG_INFO("Extracted asmmap64.sys at: " + sys_path);

        h = load_driver_service("ASMMAP64", "\\\\.\\ASMMAP64", sys_path);
        if (h == INVALID_HANDLE_VALUE) {
            LOG_ERROR("Cannot open \\\\.\\ASMMAP64 after loading");
            return false;
        }

        g_hwinfo_device = h;
        g_backend = BACKEND_ASMMAP64;
        sky::driver::write_state_log("backend=ASMMAP64 mode=map");
        LOG_INFO("ASMMAP64 backend ready — reads map PhysicalMemory into user space");
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

        // ASMMAP64 first: it maps \Device\PhysicalMemory into USER space, so
        // reads above the ThrottleStop WHEA ceiling (~427MB) are possible —
        // this is the fix for the NO_PT4K verdict (tables live above 416MB).
        LOG_INFO("=== Trying ASMMAP64, then ThrottleStop ===");
        if (connect_asmmap64()) {
            LOG_INFO("Connected to ASMMAP64 backend");
            return true;
        }
        LOG_INFO("ASMMAP64 unavailable — falling back to ThrottleStop");
        if (connect_throttlestop()) {
            LOG_INFO("Connected to ThrottleStop backend");
            return true;
        }

        LOG_ERROR("No driver available — cannot proceed");
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
    // NOTE: 0x9C40258C is NOT a ThrottleStop bulk-read code — it is
    // ASMMAP64's UNMAP IOCTL (see the ASMMAP64 block above). The probe
    // below therefore always reports "NOT supported" on TS, which is the
    // honest verdict (TS reads 1/2/4/8 bytes per call only).

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

    // Highest physical address the current backend may safely read
    // (exclusive). ThrottleStop machine-checks above ~416MB on this box;
    // ASMMAP64 maps \Device\PhysicalMemory and can reach all installed RAM.
    uintptr_t phys_read_cap() {
        if (g_backend == BACKEND_ASMMAP64) return (uintptr_t)get_total_phys();
        return 0x1A000000ULL;  // TS WHEA ceiling
    }

    // Is this physical address range inside installed RAM?
    // ThrottleStop's driver uses MmMapIoSpace internally; probing an
    // address beyond RAM (or in an MMIO hole) can bugcheck the system
    // with MEMORY_MANAGEMENT / WHEA 0x124. ASMMAP64 maps
    // \Device\PhysicalMemory — but reading a hole (e.g. the ~427MB iGPU
    // stolen region on this box) raises an UNCORRECTABLE machine check
    // that NO SEH handler can catch: #MC is fatal and Windows bugchecks
    // WHEA_UNCORRECTABLE_ERROR immediately, the __try/__except around
    // the memcpy never sees it. So reads are allowed ONLY inside
    // physical ranges the OS memory manager lists as real RAM; anything
    // else is refused BEFORE any IOCTL or map is issued.
    struct PhysRange { uintptr_t start, end; };         // [start, end)
    static std::vector<PhysRange> s_ram_ranges;
    static bool s_ram_ranges_ready = false;

    // EFI memory map (UEFI): every physical region with its type.
    // EfiConventionalMemory=7 is RAM; stolen/MMIO regions come back as
    // EfiReservedMemoryType(0)/EfiUnusableMemory(10)/EfiMemoryMappedIO(11)
    // and are excluded. This is a firmware table read (GetSystemFirmwareTable)
    // — it does NOT go through NtQuerySystemInformation, so anti-cheat
    // hooks that filter info class 80 (memory list layout is exactly what
    // Vanguard hides) cannot falsify it.
    static bool enum_ram_ranges_efi() {
        UINT32 sz = GetSystemFirmwareTable('ACPI', 'FpMf', nullptr, 0);
        if (sz < sizeof(uint64_t) * 5) return false;
        std::vector<uint8_t> buf(sz);
        UINT32 got = GetSystemFirmwareTable('ACPI', 'FpMf', buf.data(), sz);
        if (got < sizeof(uint64_t) * 5) return false;
        // EFI_MEMORY_DESCRIPTOR (40 bytes on x64):
        //   Type(u32) Pad(u32) PhysicalStart(u64) VirtualStart(u64)
        //   NumberOfPages(u64) Attribute(u64)
        const uint8_t* p = buf.data();
        const size_t desc_sz = 40;
        std::vector<PhysRange> out;
        for (size_t off = 0; off + desc_sz <= got; off += desc_sz) {
            uint32_t type = *(const uint32_t*)(p + off);
            if (type < 1 || type > 7) continue;   // 1-7 = RAM types only
            uint64_t start = *(const uint64_t*)(p + off + 8);
            uint64_t pages = *(const uint64_t*)(p + off + 24);
            uint64_t bytes = pages << 12;
            if (!start || !bytes || start + bytes < start) continue;
            if (start + bytes > 0x1000000000ULL) continue;  // 64GB sanity
            out.push_back({ (uintptr_t)start, (uintptr_t)(start + bytes) });
        }
        if (out.empty()) return false;
        s_ram_ranges = std::move(out);
        return true;
    }

    // Primary source: SystemMemoryListInformation (info class 80) — the
    // memory manager's page lists (zeroed/free/standby/modified/transition/
    // active/pool/cache...). EVERY real RAM page is in exactly one list;
    // MMIO and stolen regions are not pages and never appear. Union of all
    // runs == all readable physical RAM. Fails on machines whose anti-cheat
    // hooks NtQuerySystemInformation (Vanguard filters class 80) — the EFI
    // fallback above covers those.
    static bool enum_ram_ranges_list() {
        ULONG need = 0;
        // Sizing call: returns STATUS_INFO_LENGTH_MISMATCH (not success)
        // while filling `need` with the required buffer size.
        NTSTATUS st0 = NtQuerySystemInformation(
            (SYSTEM_INFORMATION_CLASS)80, nullptr, 0, &need);
        if (need <= sizeof(ULONG_PTR) * 13) {
            write_state_log("ram_ranges=list_fail st0=0x" +
                std::format("{:x}", (uint32_t)st0) + " need=0x" +
                std::format("{:x}", need));
            return false;
        }
        std::vector<uint8_t> buf(need + 0x1000);
        ULONG got = (ULONG)buf.size();
        NTSTATUS st = NtQuerySystemInformation(
            (SYSTEM_INFORMATION_CLASS)80, buf.data(), got, &got);
        if (!NT_SUCCESS(st) || got <= sizeof(ULONG_PTR) * 13) {
            write_state_log("ram_ranges=list_fail st=0x" +
                std::format("{:x}", (uint32_t)st) + " got=0x" +
                std::format("{:x}", got));
            return false;
        }
        // Header: 13 ULONG_PTR counters, then a variable-length array of
        // SYSTEM_MEMORY_LIST_ENTRY {NextPage, PageCount} runs (one pair per
        // contiguous run, all lists in order).
        const size_t header = sizeof(ULONG_PTR) * 13;
        const size_t n = (got - header) / (sizeof(ULONG_PTR) * 2);
        const ULONG_PTR* p = (const ULONG_PTR*)(buf.data() + header);
        std::vector<PhysRange> out;
        for (size_t i = 0; i < n; i++) {
            ULONG_PTR pfn = p[i * 2];
            ULONG_PTR cnt = p[i * 2 + 1];
            if (!pfn || !cnt) continue;
            uintptr_t start = (uintptr_t)pfn << 12;
            uintptr_t end = start + ((uintptr_t)cnt << 12);
            if (end > start) out.push_back({ start, end });
        }
        if (out.empty()) return false;
        s_ram_ranges = std::move(out);
        return true;
    }

    static void enum_ram_ranges() {
        s_ram_ranges.clear();
        // Prefer the page-list source; fall back to the EFI memory map.
        // Vanguard hooks class 80 on some builds — the EFI path exists
        // specifically for that.
        bool used_list = enum_ram_ranges_list();
        if (!used_list) {
            if (!enum_ram_ranges_efi()) {
                write_state_log("ram_ranges=EFI_FAIL");
            }
        }
        // Debug: log raw ranges from whichever source before validation
        if (!s_ram_ranges.empty()) {
            std::string src = used_list ? "list" : "efi";
            std::string msg = "ram_ranges_raw=" + src + " count=" + std::to_string(s_ram_ranges.size());
            uintptr_t raw_total = 0;
            for (auto& r : s_ram_ranges) raw_total += r.end - r.start;
            msg += " total=0x" + std::format("{:x}", raw_total);
            write_state_log(msg);
        }

        // Sort + merge overlapping/adjacent ranges.
        std::sort(s_ram_ranges.begin(), s_ram_ranges.end(),
            [](const PhysRange& a, const PhysRange& b) { return a.start < b.start; });
        std::vector<PhysRange> merged;
        for (auto& r : s_ram_ranges) {
            if (merged.empty() || r.start > merged.back().end)
                merged.push_back(r);
            else if (r.end > merged.back().end)
                merged.back().end = r.end;
        }
        s_ram_ranges.swap(merged);

        // Sane-parse validation: if the header layout drifted (build
        // differences), the union won't look like installed RAM. Reject
        // the parse rather than risk treating a hole as readable. Note:
        // the lists sum to ALL managed PFN pages (~installed RAM), while
        // get_total_phys() reports usable RAM (stolen region excluded).
        // On some builds the list includes duplicate/ghost entries (40GB
        // on an 8GB box) that may not cover low memory; we accept any
        // total >= expect/2 and rely on pa_valid's per-range check to
        // gate reads.
        uintptr_t total = 0;
        for (auto& r : s_ram_ranges) total += r.end - r.start;
        uint64_t expect = get_total_phys();
        write_state_log("ram_ranges_sane total=0x" + std::format("{:x}", total) +
            " expect=0x" + std::format("{:x}", expect));
        // Log individual ranges for diagnosis
        std::string ranges_log = "ram_ranges_detail";
        for (auto& r : s_ram_ranges) {
            ranges_log += " 0x" + std::format("{:x}", r.start) +
                         "-0x" + std::format("{:x}", r.end);
        }
        write_state_log(ranges_log);
        bool sane = !s_ram_ranges.empty() && total >= expect / 2;

        if (!sane) {
            // Fallback: only the band proven safe by 100+ ThrottleStop
            // runs (reads below 416MB never MCE'd on this box).
            s_ram_ranges.clear();
            s_ram_ranges.push_back({ 0x1000, 0x1A000000 });
            write_state_log("ram_ranges=ENUM_FAIL cap=0x1a000000");
        } else {
            std::string msg = "ram_ranges=" + std::to_string(s_ram_ranges.size());
            for (auto& r : s_ram_ranges) {
                msg += " 0x" + std::format("{:x}", r.start) +
                       "-0x" + std::format("{:x}", r.end);
            }
            write_state_log(msg);
        }
        s_ram_ranges_ready = true;
    }

    static void ensure_ram_ranges() {
        if (!s_ram_ranges_ready) enum_ram_ranges();
    }

    static bool pa_valid(uintptr_t pa, size_t size) {
        if (size == 0) return false;
        if (pa + size < pa) return false;              // overflow
        ensure_ram_ranges();
        // Backend ceiling for ThrottleStop only: it machine-checks above
        // ~416MB even inside enumerated RAM (verified by disasm + 100+
        // runs), so a garbage walk entry must never push a TS read that
        // high. ASMMAP64 maps \Device\PhysicalMemory — the enumerated
        // ranges below ARE the safety gate for it.
        if (g_backend == BACKEND_THROTTLESTOP &&
            (pa >= 0x1A000000ULL || pa + size > 0x1A000000ULL))
            return false;
        for (auto& r : s_ram_ranges) {
            if (pa >= r.start && pa + size <= r.end) return true;
        }
        return false;
    }

    static size_t bulk_max_chunk() {
        // ASMMAP64: windowed map — any size served from a mapped view
        // (physscan uses 64KB chunks; each chunk is a map+memcpy).
        if (g_backend == BACKEND_ASMMAP64) return 0x10000;
        // ThrottleStop: reads are 1/2/4/8 bytes per IOCTL, no bulk path
        // (verified by disasm; the old 0x9C40258C probe was asmmap64's
        // UNMAP code and always failed on TS — NO_BULK was correct).
        return 0;
    }

    // ============================================================
    // ASMMAP64 map-based read path
    // ============================================================
    // One map IOCTL (0x9C402580) maps a physical range into USER space;
    // reads are then plain memcpy from the mapped VA — zero per-read
    // IOCTLs. A 4MB window is cached so sequential page-walk reads stay
    // inside it. NOTE: the \Device\PhysicalMemory view is NOT hole-proof —
    // the section spans the whole physical space and touching a hole page
    // (e.g. the ~427MB iGPU stolen region) raises an UNCORRECTABLE machine
    // check that SEH CANNOT catch (WHEA bugchecks immediately). Safety comes
    // from pa_valid() above: no request ever leaves this module unless it is
    // fully inside an enumerated RAM range, and windows are clamped to the
    // containing range so a window never spans a hole in the first place.
    static uintptr_t s_map_base = 0;
    static size_t    s_map_size = 0;
    static uintptr_t s_map_va   = 0;
    static bool      s_map_valid = false;

    static bool asmmap_map(uintptr_t pa, size_t size, uintptr_t& out_va) {
        // Input (>= 0x18): {u32 phys_lo@0, u32 phys_hi@4, u32 size@0x10}
        // Mapped user VA written back into input[0x08..0x0C] (lo/hi dwords).
        struct {
            uint32_t phys_lo;
            uint32_t phys_hi;
            uint32_t va_lo;
            uint32_t va_hi;
            uint32_t size;
            uint32_t pad;
        } in = {};
        in.phys_lo = (uint32_t)(pa & 0xFFFFFFFF);
        in.phys_hi = (uint32_t)(pa >> 32);
        in.size    = (uint32_t)size;
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(g_hwinfo_device, ASMMAP_IOCTL_MAP,
            &in, sizeof(in), &in, sizeof(in), &returned, nullptr);
        if (!ok) return false;
        out_va = ((uintptr_t)in.va_hi << 32) | in.va_lo;
        return out_va != 0;
    }

    static void asmmap_unmap() {
        DWORD returned = 0;
        DeviceIoControl(g_hwinfo_device, ASMMAP_IOCTL_UNMAP,
            nullptr, 0, nullptr, 0, &returned, nullptr);
        s_map_valid = false;
    }

    static bool read_physical_asmmap(uintptr_t phys_addr, void* buffer, size_t size) {
        if (size == 0) return false;

        // Serve from the cached window when possible.
        if (s_map_valid && phys_addr >= s_map_base &&
            phys_addr + size <= s_map_base + s_map_size) {
            __try {
                memcpy(buffer, (void*)(s_map_va + (phys_addr - s_map_base)), size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Hole page faulted; drop the window.
                s_map_valid = false;
                return false;
            }
        }

        // New window: 4MB default, page-aligned, must cover the request.
        uintptr_t base = phys_addr & ~0xFFFULL;
        uintptr_t need = phys_addr + size;
        size_t win = 0x400000;
        if (base + win < need) win = (size_t)(need - base);
        win = (win + 0xFFF) & ~0xFFFULL;
        uint64_t total = get_total_phys();
        if (base + win > total) win = (size_t)(total - base);
        if (win < (size_t)(need - base)) return false;  // cannot cover

        // Clamp the window to the containing RAM range so a window NEVER
        // spans a hole (the map may succeed over a hole but touching the
        // hole bytes machine-checks — WHEA, not SEH-catchable).
        ensure_ram_ranges();
        for (auto& r : s_ram_ranges) {
            if (base >= r.start && base < r.end) {
                if (base + win > r.end) win = (size_t)(r.end - base);
                break;
            }
        }
        if (win < (size_t)(need - base)) return false;  // cannot cover

        if (s_map_valid) asmmap_unmap();
        uintptr_t va = 0;
        if (!asmmap_map(base, win, va)) {
            // The 4MB window may straddle a reserved hole (iGPU stolen
            // region etc.) that ZwMapViewOfSection refuses. Fall back to a
            // single-page map covering just this read — never skip a valid
            // page because of window granularity.
            uintptr_t pg = phys_addr & ~0xFFFULL;
            size_t pg_sz = (size_t)(((phys_addr + size + 0xFFF) & ~0xFFFULL) - pg);
            if (!asmmap_map(pg, pg_sz, va)) return false;
            base = pg;
            win = pg_sz;
        }
        s_map_base = base;
        s_map_size = win;
        s_map_va   = va;
        s_map_valid = true;
        __try {
            memcpy(buffer, (void*)(va + (phys_addr - base)), size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_map_valid = false;
            return false;
        }
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

        // === ASMMAP64 backend: windowed PhysicalMemory map ===
        if (g_backend == BACKEND_ASMMAP64)
            return read_physical_asmmap(phys_addr, buffer, size);

        // === ThrottleStop backend ===
        if (g_backend == BACKEND_THROTTLESTOP) {
            // TS reads 1/2/4/8 bytes per IOCTL (verified by disasm) — no
            // bulk path, so every multi-byte read is chunked below.
            uint8_t* dst = (uint8_t*)buffer;
            size_t remaining = size;
            uintptr_t addr = phys_addr;

            while (remaining > 0) {
                // TS read IOCTL (0x80006498) accepts 1/2/4/8-byte transfers
                // — the driver MmMapIoSpace()s the address and copies the
                // requested length back (verified by disasm of the IOCTL
                // jump table: size mask 0x116 = bits 1,2,4,7). Use the
                // largest chunk that fits remaining AND stays within one
                // 4KB page (MmMapIoSpace crossing a page boundary needs a
                // second map — keep reads page-local to be safe).
                size_t chunk = remaining;
                if (chunk > 8) chunk = 8;
                size_t to_page = 0x1000 - (addr & 0xFFF);
                if (chunk > to_page) chunk = to_page;
                if (chunk >= 8) chunk = 8;
                else if (chunk >= 4) chunk = 4;
                else if (chunk >= 2) chunk = 2;
                else chunk = 1;

                uint64_t ts_addr = addr;
                uint8_t tmp[8] = { 0 };
                DWORD returned = 0;

                BOOL ok = DeviceIoControl(g_hwinfo_device, TS_IOCTL_READ,
                    &ts_addr, sizeof(ts_addr),   // input: physical address
                    tmp, (DWORD)chunk,           // output: 1/2/4/8 bytes
                    &returned, nullptr);

                // Transient IOCTL failures happen on the TS driver (seen in
                // the wild as intermittent b=0 walk failures at valid RAM
                // addresses). Retry ONCE on failure only — the normal path
                // is untouched, so detection shape is unchanged.
                if (!ok && GetLastError() != ERROR_SUCCESS) {
                    ok = DeviceIoControl(g_hwinfo_device, TS_IOCTL_READ,
                        &ts_addr, sizeof(ts_addr), tmp, (DWORD)chunk, &returned, nullptr);
                }
                if (!ok) {
                    // Multi-byte transfer rejected by this driver version?
                    // Fall back to a single-byte read so nothing regresses.
                    ok = DeviceIoControl(g_hwinfo_device, TS_IOCTL_READ,
                        &ts_addr, sizeof(ts_addr), tmp, 1, &returned, nullptr);
                    if (!ok) {
                        LOG_ERROR("TS read IOCTL failed at phys=0x" +
                            std::format("{:x}", addr) + " GLE=" + std::to_string(GetLastError()));
                        return false;
                    }
                    chunk = 1;
                }
                memcpy(dst, tmp, chunk);
                dst += chunk;
                addr += chunk;
                remaining -= chunk;

                // One-shot proof the multi-byte path engaged (first chunk>1).
                static bool s_chunk_logged = false;
                if (chunk > 1 && !s_chunk_logged) {
                    s_chunk_logged = true;
                    write_state_log("ts_chunk_read=OK n=" + std::to_string(chunk));
                }

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

        // Reserved-bit check shared by all four levels: entry bits 52-62
        // must be 0 for a REAL table entry (frame bits 12-51 are free; NX
        // bit 63 is free). Garbage candidates produce garbage frames whose
        // reserved bits are set — rejecting them here keeps the walk from
        // following nonsense pointers (which can point INTO a hole).
        auto entry_ok = [](uintptr_t e) -> bool {
            return (e & 1) && (((e >> 52) & 0x7FF) == 0);
        };
        auto finish = [&](uintptr_t phys) -> uintptr_t {
            if (!pa_valid(phys, 1)) {        // final phys must be real RAM
                walk_log(0, vaddr, phys);
                return 0;
            }
            walk_success_log(vaddr, dirbase, phys);
            return phys;
        };

        uintptr_t pml4e = 0, pml4i = (vaddr >> 39) & 0x1FF;
        if (!read_physical(dirbase + pml4i * 8, &pml4e, 8) || !entry_ok(pml4e)) {
            walk_log(1, dirbase + pml4i * 8, pml4e);
            return 0;
        }

        uintptr_t pdpte = 0, pdpti = (vaddr >> 30) & 0x1FF;
        if (!read_physical((pml4e & PAGE_MASK_4KB) + pdpti * 8, &pdpte, 8) || !entry_ok(pdpte)) {
            walk_log(2, (pml4e & PAGE_MASK_4KB) + pdpti * 8, pdpte);
            return 0;
        }
        if (pdpte & (1 << 7)) {
            return finish((pdpte & PAGE_MASK_1GB) | (vaddr & 0x3FFFFFFF));
        }

        uintptr_t pde = 0, pdi = (vaddr >> 21) & 0x1FF;
        if (!read_physical((pdpte & PAGE_MASK_4KB) + pdi * 8, &pde, 8) || !entry_ok(pde)) {
            walk_log(3, (pdpte & PAGE_MASK_4KB) + pdi * 8, pde);
            return 0;
        }
        if (pde & (1 << 7)) {
            return finish((pde & PAGE_MASK_2MB) | (vaddr & 0x1FFFFF));
        }

        uintptr_t pte = 0, pti = (vaddr >> 12) & 0x1FF;
        if (!read_physical((pde & PAGE_MASK_4KB) + pti * 8, &pte, 8) || !entry_ok(pte)) {
            walk_log(4, (pde & PAGE_MASK_4KB) + pti * 8, pte);
            return 0;
        }
        return finish((pte & PAGE_MASK_4KB) | (vaddr & 0xFFF));
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
    static uintptr_t ntk_size = 0;
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
        write_state_log("kernel_offset_dbg NtQuery11_null status=0x" + std::format("{:x}", status) + " size=" + std::to_string(size));
        if (size == 0) return false;

        std::vector<uint8_t> buf(size);
        status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, buf.data(), size, &size);
        write_state_log("kernel_offset_dbg NtQuery11_real status=0x" + std::format("{:x}", status));
        if (!NT_SUCCESS(status)) return false;

        auto modules = (PSYSTEM_MODULE_INFORMATION)buf.data();
        write_state_log("kernel_offset_dbg module_count=" + std::to_string(modules->Count));
        bool found_mod = false;
        for (ULONG i = 0; i < modules->Count; ++i) {
            auto& mod = modules->Module[i];
            std::string name((char*)mod.FullPathName + mod.OffsetToFileName);
            if (name == "ntoskrnl.exe") {
                s_kernel_vbase = (uintptr_t)mod.ImageBase;
                ntk_size = mod.ImageSize;
                write_state_log("kernel_offset_dbg ntoskrnl vbase=0x" + std::format("{:x}", s_kernel_vbase) + " size=" + std::to_string(ntk_size));
                found_mod = true;
                break;
            }
        }
        if (!found_mod) {
            write_state_log("kernel_offset_dbg ntoskrnl NOT IN MODULE LIST");
            return false;
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
            write_state_log("kernel_offset_dbg heuristic_guess=0x" + std::format("{:x}", guess));
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

        // 2) Limited physical scan: [16MB, min(2GB, cap, total_phys)] at 2MB
        //    steps. This is the range where the ntoskrnl image is loaded on
        //    Win10 x64. The scan NEVER exceeds the backend cap: on ThrottleStop
        //    that is the 416MB WHEA ceiling (reads above machine-check), on
        //    ASMMAP64 it is total RAM. The heuristic above almost always hits
        //    first, so this is just a safety net.
        if (!found) {
            LOG_INFO("kernel_phys_offset: heuristic miss, scanning [16MB..cap) at 2MB...");
            uint64_t scan_limit = get_total_phys();
            if (scan_limit > 0x80000000ULL) scan_limit = 0x80000000ULL;
            uint64_t cap = phys_read_cap();
            if (scan_limit > cap) scan_limit = cap;
            write_state_log("kernel_offset_dbg scan_limit=0x" + std::format("{:x}", scan_limit) + " cap=0x" + std::format("{:x}", cap));
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

    // ============================================================
    // Attach-bootstrap helpers (used by the PML4 scan)
    // ============================================================
    bool kernel_image_offset(uintptr_t* vbase, uintptr_t* pbase) {
        if (!s_kernel_offset_ready && !init_kernel_phys_offset())
            return false;
        if (!s_kernel_vbase || !s_kernel_pbase) return false;
        if (vbase) *vbase = s_kernel_vbase;
        if (pbase) *pbase = s_kernel_pbase;
        return true;
    }

    // Self-verifying PML4 test: walk the KNOWN kernel image VA through the
    // candidate page table and require phys == known image PA. A random pool
    // page cannot pass this — it is a full 4-level walk with a known answer.
    //
    // SAFETY BOUND: ThrottleStop machine-checks (WHEA 0x124) on physical
    // reads above ~0x1A000000 (416MB) on this box. Every table address the
    // walk touches must stay below the backend's cap, or the candidate is
    // rejected instead of read. ASMMAP64 (the map backend) has no such cap.
    bool verify_pml4_for_kernel(uintptr_t pml4_pa, uintptr_t vbase, uintptr_t pbase) {
        if (!pml4_pa || !vbase || !pbase) return false;
        if (pml4_pa & 0xFFF) return false;                 // must be page-aligned
        const uintptr_t kSafeCeil = phys_read_cap();       // backend-aware

        uintptr_t addr = pml4_pa + ((vbase >> 39) & 0x1FF) * 8;   // PML4E
        for (int level = 0; level < 4; level++) {
            if (addr >= kSafeCeil) return false;           // never read past ceiling
            uint64_t e = 0;
            if (!read_physical(addr, &e, 8) || !(e & 1))
                return false;
            if (level == 0) {
                addr = (e & PAGE_MASK_4KB) + ((vbase >> 30) & 0x1FF) * 8; // PDPT
            } else if (level == 1) {
                if (e & (1 << 7)) {                        // 1GB page
                    return ((e & PAGE_MASK_1GB) | (vbase & 0x3FFFFFFF)) == pbase;
                }
                addr = (e & PAGE_MASK_4KB) + ((vbase >> 21) & 0x1FF) * 8; // PD
            } else if (level == 2) {
                if (e & (1 << 7)) {                        // 2MB page
                    return ((e & PAGE_MASK_2MB) | (vbase & 0x1FFFFF)) == pbase;
                }
                addr = (e & PAGE_MASK_4KB) + ((vbase >> 12) & 0x1FF) * 8; // PT
            } else {
                return ((e & PAGE_MASK_4KB) | (vbase & 0xFFF)) == pbase;  // 4KB page
            }
        }
        return false;
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
            if (g_backend == BACKEND_ASMMAP64 && s_map_valid) {
                asmmap_unmap();
            }
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                CloseHandle(g_hwinfo_device);
                g_hwinfo_device = INVALID_HANDLE_VALUE;
            }
            // Try unloading whichever service we created
            unload_driver_generic("ASMMAP64");
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