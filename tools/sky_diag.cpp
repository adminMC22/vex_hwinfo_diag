// sky_diag.cpp v5 — Full-range HWiNFO device probe (0-215) + RTCore64 + more
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma pack(push, 1)
struct RTCORE_48 { BYTE pad0[8]; DWORD64 Address; BYTE pad1[8]; DWORD Size; DWORD Value; BYTE pad2[16]; };
#pragma pack(pop)

void test_ioctl(HANDLE hDev, DWORD code, const char* label) {
    DWORD returned = 0;
    BYTE obuf[0x200] = {};
    // Format 1: 8-byte address in -> data out (HWiNFO style)
    DWORD64 addr = 0xF0000;
    BOOL ok = DeviceIoControl(hDev, code, &addr, 8, obuf, 0x100, &returned, NULL);
    DWORD val = 0;
    if (ok && returned >= 4) memcpy(&val, obuf, 4);
    printf("    %-12s 0x%08X -> %s val=0x%08X ret=%d%s\n",
           label, code, ok ? "OK" : "FAIL", val, returned,
           (ok && val) ? " *** DATA ***" : "");

    // Format 0: 48-byte struct (RTCore64 style)
    RTCORE_48 r = {};
    r.Address = 0xF0000; r.Size = 4;
    ok = DeviceIoControl(hDev, code, &r, 48, &r, 48, &returned, NULL);
    printf("    %-12s 0x%08X -> %s val=0x%08X ret=%d%s\n",
           "RTC48", code, ok ? "OK" : "FAIL", ok ? r.Value : 0, returned,
           (ok && r.Value) ? " *** DATA ***" : "");

    // Format 2: flat buffer
    BYTE flat[8+4+0x100] = {};
    memcpy(flat, &addr, 8);
    *(DWORD*)(flat+8) = 4; // size
    ok = DeviceIoControl(hDev, code, flat, 12, flat, sizeof(flat), &returned, NULL);
    DWORD val2 = 0;
    if (ok && returned > 8) memcpy(&val2, flat+8, 4);
    printf("    %-12s 0x%08X -> %s val=0x%08X ret=%d%s\n",
           "FLAT", code, ok ? "OK" : "FAIL", val2, returned,
           (ok && val2) ? " *** DATA ***" : "");
}

void try_device(const char* path, const char* label, DWORD* codes, int n_codes) {
    for (int access = 0; access < 3; access++) {
        DWORD dwDesiredAccess;
        const char* access_name;
        switch (access) {
            case 0: dwDesiredAccess = GENERIC_READ | GENERIC_WRITE; access_name = "RW"; break;
            case 1: dwDesiredAccess = GENERIC_READ; access_name = "RO"; break;
            case 2: dwDesiredAccess = 0; access_name = "NONE"; break;
            default: continue;
        }

        HANDLE hDev = CreateFileA(path, dwDesiredAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hDev == INVALID_HANDLE_VALUE) {
            if (access == 0) {
                // Only print on first attempt
                DWORD gle = GetLastError();
                printf("%-22s [%s] -> GLE=%d", label, access_name, gle);
                if (gle == 2) printf(" (FILE_NOT_FOUND)");
                else if (gle == 5) printf(" (ACCESS_DENIED)");
                printf("\n");
            }
            continue;
        }

        printf("%-22s [%s] -> OPEN OK\n", label, access_name);
        for (int i = 0; i < n_codes; i++) {
            test_ioctl(hDev, codes[i], "8BIN");
        }
        CloseHandle(hDev);
        return; // don't try other access levels once one works
    }
}

int main() {
    printf("========== Sky Diagnostic v5 ==========\n");
    printf("Probes HWiNFO_0 through HWiNFO_215 + RTCore64 + others\n\n");

    DWORD rtc_codes[] = { 0x80002048, 0x80002040, 0x9C40609C, 0x9C40258C };
    DWORD hwinfo_codes[] = { 0x9C40259C, 0x9C4025A0, 0x9C40609C, 0x9C406094 };

    // --- RTCore64 ---
    printf("=== RTCore64 ===\n");
    try_device("\\\\.\\RTCore64", "RTCore64", rtc_codes, 4);

    // --- HWiNFO numbered devices ---
    printf("\n=== HWiNFO (0-215) ===\n");
    for (int ver = 215; ver >= 0; ver--) {
        char path[64], label[64];
        snprintf(path, sizeof(path), "\\\\.\\HWiNFO_%d", ver);
        snprintf(label, sizeof(label), "HWiNFO_%d", ver);
        try_device(path, label, hwinfo_codes, 4);
    }

    // --- HWiNFO64 numbered ---
    printf("\n=== HWiNFO64 (0-215) ===\n");
    for (int ver = 215; ver >= 0; ver--) {
        char path[64], label[64];
        snprintf(path, sizeof(path), "\\\\.\\HWiNFO64_%d", ver);
        snprintf(label, sizeof(label), "HWiNFO64_%d", ver);
        try_device(path, label, hwinfo_codes, 4);
    }

    // --- Generic HWiNFO ---
    printf("\n=== Generic HWiNFO ===\n");
    try_device("\\\\.\\HWiNFO", "HWiNFO", hwinfo_codes, 4);
    try_device("\\\\.\\HWiNFO64", "HWiNFO64", hwinfo_codes, 4);
    try_device("\\\\.\\HWiNFO32", "HWiNFO32", hwinfo_codes, 4);

    // --- Other drivers ---
    printf("\n=== Other drivers ===\n");
    DWORD gio_codes[] = { 0xC3502002, 0xC3502006, 0x222000, 0x222004 };
    try_device("\\\\.\\GIO", "GIO", gio_codes, 4);
    try_device("\\\\.\\WinRing0_1_2_0", "WinRing0", rtc_codes, 2);
    try_device("\\\\.\\RyzenMaster", "RyzenMaster", rtc_codes, 2);

    printf("\n============================================\n");
    printf("If HWiNFO_XXX [RW] -> OPEN OK shows up, we found the device.\n");
    printf("If all GLE=2: neither driver is loaded. Check HWiNFO is running.\n");
    printf("Press Enter to exit...\n");
    getchar();
    return 0;
}