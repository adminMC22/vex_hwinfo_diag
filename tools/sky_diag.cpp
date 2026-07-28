// sky_diag.cpp — Comprehensive device + IOCTL diagnostic tool (safe: no brute-force)
// Tries all known devices × known IOCTL codes × known formats
// Compile: cl /O2 sky_diag.cpp /EHsc
// Run as Admin after starting your target tools (Afterburner, HWiNFO, etc.)

#include <windows.h>
#include <stdio.h>
#include <string.h>

// RTCore64 protocol (CVE-2019-16098, OLD vulnerable driver only)
// Device: \\.\RTCore64
// Read: 0x80002048, Write: 0x8000204C
// Format: 48-byte struct [8pad][8addr][8pad][4size][4value][16pad]
#pragma pack(push, 1)
struct RTCORE_48 {
    BYTE   pad0[8];
    DWORD64 Address;
    BYTE   pad1[8];
    DWORD  Size;
    DWORD  Value;
    BYTE   pad2[16];
};
#pragma pack(pop)

// HWiNFO protocol (various versions)
// Device: \\.\HWiNFO, \\.\HWiNFO64, \\.\HWiNFO_xxx
// Format: 8-byte address (LARGE_INTEGER) in, raw data out

// GIO/gdrv protocol (Gigabyte)
// Device: \\.\GIO
// Read: 0xC3502002, Write: 0xC3502006
// Format: [4 size][8 addr][data] (24+data bytes) or similar

struct TestSpec {
    const char* device_path;    // e.g. "\\\\.\\RTCore64"
    const char* label;          // human-readable label
    DWORD ioctl_codes[6];       // up to 6 IOCTL codes to test
    int n_codes;                // how many in the list
    int format_type;            // 0=RTC48, 1=8byte-in/out, 2=flat [8addr+data]
    const char* notes;          // any notes
};

void test_device(HANDLE hDev, const char* label, DWORD code, int fmt) {
    DWORD gle = 999, returned = 0;
    BYTE ibuf[128] = {};
    BYTE obuf[0x200] = {};
    BOOL ok = FALSE;

    switch (fmt) {
    case 0: {  // RTC48 struct: 48 bytes in == out
        RTCORE_48* r = (RTCORE_48*)ibuf;
        r->Address = 0xF0000;
        r->Size = 4;
        ok = DeviceIoControl(hDev, code, ibuf, 48, ibuf, 48, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        printf("  %-40s 0x%08X RTC48    -> %s  val=0x%08X ret=%d\n",
               label, code, ok ? "OK" : "FAIL", ok ? r->Value : 0, returned);
        break;
    }
    case 1: {  // 8-byte address in, data out
        DWORD64 addr = 0xF0000;
        ok = DeviceIoControl(hDev, code, &addr, 8, obuf, 0x100, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        DWORD val = 0;
        if (ok && returned >= 4) memcpy(&val, obuf, 4);
        printf("  %-40s 0x%08X 8B-IN    -> %s  val=0x%08X ret=%d%s\n",
               label, code, ok ? "OK" : "FAIL", val, returned,
               ok && val ? " *** DATA ***" : "");
        break;
    }
    case 2: {  // flat: [8addr+data] combined
        DWORD64 addr = 0xF0000;
        memcpy(ibuf, &addr, 8);
        ok = DeviceIoControl(hDev, code, ibuf, 8+4, ibuf, 8+0x100, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        DWORD val = 0;
        if (ok && returned > 8) memcpy(&val, ibuf+8, 4);
        printf("  %-40s 0x%08X FLAT     -> %s  val=0x%08X ret=%d%s\n",
               label, code, ok ? "OK" : "FAIL", val, returned,
               ok && val ? " *** DATA ***" : "");
        if (!ok) {
            // Try again with exact same size for in/out
            memcpy(ibuf, &addr, 8);
            ok = DeviceIoControl(hDev, code, ibuf, 8+4, obuf, 8+4, &returned, NULL);
            if (ok) gle = 0; else gle = GetLastError();
            val = 0;
            if (ok && returned >= 4) memcpy(&val, obuf, 4);
            printf("  %-40s 0x%08X FLATEQ   -> %s  val=0x%08X ret=%d%s\n",
                   label, code, ok ? "OK" : "FAIL", val, returned,
                   ok && val ? " *** DATA ***" : "");
        }
        break;
    }
    }
    if (!ok) {
        printf("  %-40s 0x%08X (fmt=%d) -> FAIL (GLE=%d)\n", label, code, fmt, gle);
    }
}

void try_device(const char* path, const char* label, DWORD* codes, int n_codes, int fmt) {
    HANDLE hDev = CreateFileA(path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("  %-50s -> OPEN FAILED (GLE=%d)\n", label, GetLastError());
        return;
    }

    printf("  %-50s -> OPEN OK\n", label);
    for (int i = 0; i < n_codes; i++) {
        test_device(hDev, label, codes[i], fmt);
    }
    CloseHandle(hDev);
}

int main() {
    printf("========== Sky Diagnostic: Device + IOCTL Probe ==========\n");
    printf("(Safe: only known IOCTL codes, no brute-force)\n\n");

    printf("=== RTCore64 (MSI Afterburner) ===\n");
    DWORD rtc_codes[] = { 0x80002048, 0x80002040, 0x9C40609C, 0x9C40258C, 0x80002000 };
    try_device("\\\\.\\RTCore64", "RTCore64 (48-byte struct)", rtc_codes, 5, 0);
    try_device("\\\\.\\RTCore64", "RTCore64 (8-byte in/out)", rtc_codes, 5, 1);
    try_device("\\\\.\\RTCore64", "RTCore64 (flat buffer)", rtc_codes, 5, 2);

    printf("\n=== HWiNFO (if installed) ===\n");
    DWORD hwinfo_codes[] = { 0x9C40259C, 0x9C4025A0, 0x9C40609C, 0x9C406094 };
    try_device("\\\\.\\HWiNFO",   "HWiNFO (8-byte in/out)", hwinfo_codes, 4, 1);
    try_device("\\\\.\\HWiNFO",   "HWiNFO (flat buffer)",  hwinfo_codes, 4, 2);
    try_device("\\\\.\\HWiNFO64", "HWiNFO64 (8-byte in/out)", hwinfo_codes, 4, 1);
    try_device("\\\\.\\HWiNFO64", "HWiNFO64 (flat buffer)",  hwinfo_codes, 4, 2);
    try_device("\\\\.\\HWiNFO32", "HWiNFO32 (8-byte in/out)", hwinfo_codes, 4, 1);

    printf("\n=== Gigabyte GIO (if available) ===\n");
    DWORD gio_codes[] = { 0xC3502002, 0xC3502006, 0x222000, 0x222004 };
    try_device("\\\\.\\GIO", "GIO (flat buffer)", gio_codes, 4, 2);

    printf("\n=== Other known devices ===\n");
    // WinRing0 / OpenLibSys
    try_device("\\\\.\\WinRing0_1_2_0", "WinRing0 (8-byte in/out)", rtc_codes, 2, 1);
    // AMD Ryzen Master
    try_device("\\\\.\\RyzenMaster", "RyzenMaster (8-byte in/out)", rtc_codes, 2, 1);

    printf("\n============================================\n");
    printf("Any line with \"*** DATA ***\" means the IOCTL works and returns non-zero data.\n");
    printf("Press Enter to exit...\n");
    getchar();
    return 0;
}
