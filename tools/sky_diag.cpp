// sky_diag.cpp — Comprehensive device + IOCTL diagnostic tool (v3)
// Tries ALL HWiNFO numbered devices (200-215) + RTCore64 + others
// Run as Admin with MSI Afterburner and HWiNFO64 running
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma pack(push, 1)
struct RTCORE_48 {
    BYTE    pad0[8];
    DWORD64 Address;
    BYTE    pad1[8];
    DWORD   Size;
    DWORD   Value;
    BYTE    pad2[16];
};
#pragma pack(pop)

void test_device(HANDLE hDev, const char* label, DWORD code, int fmt) {
    DWORD gle = 999, returned = 0;
    BYTE ibuf[128] = {};
    BYTE obuf[0x200] = {};
    BOOL ok = FALSE;

    switch (fmt) {
    case 0: {  // RTC48 struct
        RTCORE_48* r = (RTCORE_48*)ibuf;
        r->Address = 0xF0000;
        r->Size = 4;
        ok = DeviceIoControl(hDev, code, ibuf, 48, ibuf, 48, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        printf("  0x%08X RTC48 -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", ok ? r->Value : 0, returned,
               (ok && r->Value) ? " *** DATA ***" : "");
        break;
    }
    case 1: {  // 8-byte address in, data out
        DWORD64 addr = 0xF0000;
        ok = DeviceIoControl(hDev, code, &addr, 8, obuf, 0x100, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        DWORD val = 0;
        if (ok && returned >= 4) memcpy(&val, obuf, 4);
        printf("  0x%08X 8BIN -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", val, returned,
               (ok && val) ? " *** DATA ***" : "");
        break;
    }
    case 2: {  // flat: [8addr+data]
        DWORD64 addr = 0xF0000;
        memcpy(ibuf, &addr, 8);
        ok = DeviceIoControl(hDev, code, ibuf, 8+4, ibuf, 8+0x100, &returned, NULL);
        if (ok) gle = 0; else gle = GetLastError();
        DWORD val = 0;
        if (ok && returned > 8) memcpy(&val, ibuf+8, 4);
        printf("  0x%08X FLAT -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", val, returned,
               (ok && val) ? " *** DATA ***" : "");
        if (!ok) {
            memcpy(ibuf, &addr, 8);
            ok = DeviceIoControl(hDev, code, ibuf, 8+4, obuf, 8+4, &returned, NULL);
            if (ok) gle = 0; else gle = GetLastError();
            val = 0;
            if (ok && returned >= 4) memcpy(&val, obuf, 4);
            printf("  0x%08X FLATEQ -> %s  val=0x%08X ret=%d%s\n",
                   code, ok ? "OK" : "FAIL", val, returned,
                   (ok && val) ? " *** DATA ***" : "");
        }
        break;
    }
    }
    if (!ok) {
        printf("  0x%08X (fmt%d) -> FAIL (GLE=%d)\n", code, fmt, gle);
    }
}

void try_device(const char* path, const char* label, DWORD* codes, int n_codes, int fmt) {
    HANDLE hDev = CreateFileA(path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        DWORD gle = GetLastError();
        // Only print failure for first attempt, silently skip others
        static char last_label[64] = "";
        if (strcmp(label, last_label) != 0) {
            printf("%-50s -> GLE=%d", label, gle);
            switch (gle) {
                case 2: printf(" (FILE_NOT_FOUND)"); break;
                case 5: printf(" (ACCESS_DENIED)"); break;
                case 6: printf(" (INVALID_HANDLE)"); break;
                case 55: printf(" (DEVICE_NOT_AVAILABLE - try running as Admin)"); break;
            }
            printf("\n");
            strncpy(last_label, label, 63);
            last_label[63] = 0;
        }
        return;
    }

    printf("%-50s -> OPEN OK\n", label);
    for (int i = 0; i < n_codes; i++) {
        test_device(hDev, label, codes[i], fmt);
    }
    CloseHandle(hDev);
}

int main() {
    printf("========== Sky Diagnostic v3: Device + IOCTL Probe ==========\n");
    printf("(Safe: only known IOCTL codes, no brute-force)\n\n");

    printf("=== RTCore64 ===\n");
    DWORD rtc_codes[] = { 0x80002048, 0x80002040, 0x9C40609C, 0x9C40258C, 0x80002000 };
    try_device("\\\\.\\RTCore64", "RTCore64 (48-byte)", rtc_codes, 5, 0);
    try_device("\\\\.\\RTCore64", "RTCore64 (8-byte)", rtc_codes, 5, 1);
    try_device("\\\\.\\RTCore64", "RTCore64 (flat)", rtc_codes, 5, 2);

    printf("\n=== HWiNFO (numbered devices 215-150 + generic) ===\n");
    DWORD hwinfo_codes[] = { 0x9C40259C, 0x9C4025A0, 0x9C40609C, 0x9C406094 };
    for (int ver = 215; ver >= 150; ver--) {
        char path[64], label[64];
        snprintf(path, sizeof(path), "\\\\.\\HWiNFO_%d", ver);
        snprintf(label, sizeof(label), "HWiNFO_%d", ver);
        try_device(path, label, hwinfo_codes, 4, 1);
        snprintf(path, sizeof(path), "\\\\.\\HWiNFO64_%d", ver);
        snprintf(label, sizeof(label), "HWiNFO64_%d", ver);
        try_device(path, label, hwinfo_codes, 4, 1);
    }
    try_device("\\\\.\\HWiNFO",   "HWiNFO (8-byte)",  hwinfo_codes, 4, 1);
    try_device("\\\\.\\HWiNFO",   "HWiNFO (flat)",   hwinfo_codes, 4, 2);
    try_device("\\\\.\\HWiNFO64", "HWiNFO64 (8-byte)", hwinfo_codes, 4, 1);
    try_device("\\\\.\\HWiNFO64", "HWiNFO64 (flat)",  hwinfo_codes, 4, 2);
    try_device("\\\\.\\HWiNFO32", "HWiNFO32",          hwinfo_codes, 4, 1);

    printf("\n=== Other drivers ===\n");
    DWORD gio_codes[] = { 0xC3502002, 0xC3502006, 0x222000, 0x222004 };
    try_device("\\\\.\\GIO",  "GIO",     gio_codes, 4, 2);
    try_device("\\\\.\\WinRing0_1_2_0", "WinRing0",  rtc_codes, 2, 1);
    try_device("\\\\.\\RyzenMaster",    "RyzenMaster", rtc_codes, 2, 1);

    printf("\n============================================\n");
    printf("\nIf RTCore64 GLE=2: restore original RTCore64.sys from .bak then restart Afterburner\n");
    printf("  copy /y \"D:\\Program Files\\MSI Afterburner\\RTCore64.sys.bak\" \"D:\\Program Files\\MSI Afterburner\\RTCore64.sys\"\n");
    printf("\nIf HWiNFO all GLE=2: driver not loaded. Check HWiNFO settings have 'Enable Kernel Driver' checked\n");
    printf("\nAny line with *** DATA *** means the IOCTL works.\n");
    printf("Press Enter to exit...\n");
    getchar();
    return 0;
}