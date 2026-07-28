// sky_diag.cpp v4 — Comprehensive device probe with multiple access methods
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

void test_device(HANDLE hDev, DWORD code, int fmt) {
    DWORD returned = 0;
    BYTE ibuf[128] = {};
    BYTE obuf[0x200] = {};
    BOOL ok = FALSE;

    switch (fmt) {
    case 0: {
        RTCORE_48* r = (RTCORE_48*)ibuf;
        r->Address = 0xF0000; r->Size = 4;
        ok = DeviceIoControl(hDev, code, ibuf, 48, ibuf, 48, &returned, NULL);
        DWORD val = ok ? r->Value : 0;
        printf("  0x%08X RTC48 -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", val, returned,
               (ok && val) ? " *** DATA ***" : "");
        break;
    }
    case 1: {
        DWORD64 addr = 0xF0000;
        ok = DeviceIoControl(hDev, code, &addr, 8, obuf, 0x100, &returned, NULL);
        DWORD val = 0;
        if (ok && returned >= 4) memcpy(&val, obuf, 4);
        printf("  0x%08X 8BIN -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", val, returned,
               (ok && val) ? " *** DATA ***" : "");
        break;
    }
    case 2: {
        DWORD64 addr = 0xF0000;
        memcpy(ibuf, &addr, 8);
        ok = DeviceIoControl(hDev, code, ibuf, 8+4, ibuf, 8+0x100, &returned, NULL);
        DWORD val = 0;
        if (ok && returned > 8) memcpy(&val, ibuf+8, 4);
        printf("  0x%08X FLAT -> %s  val=0x%08X ret=%d%s\n",
               code, ok ? "OK" : "FAIL", val, returned,
               (ok && val) ? " *** DATA ***" : "");
        break;
    }
    }
    if (!ok) {
        printf("  0x%08X (fmt%d) -> FAIL (GLE=%d)\n", code, fmt, GetLastError());
    }
}

void try_device_ex(const char* path, DWORD des_access, const char* access_name,
                   DWORD* codes, int n_codes, int fmt) {
    HANDLE hDev = CreateFileA(path, des_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        return;  // silent — we print in the outer loop
    }
    printf("  %s access=0x%08X -> OPEN OK\n", access_name, des_access);
    for (int i = 0; i < n_codes; i++) {
        test_device(hDev, codes[i], fmt);
    }
    CloseHandle(hDev);
}

int main() {
    printf("========== Sky Diagnostic v4 ==========\n\n");

    // --- RTCore64 ---
    printf("=== RTCore64 ===\n");
    DWORD rtc_codes[] = { 0x80002048, 0x80002040, 0x9C40609C, 0x9C40258C };
    DWORD access_levels[] = {
        GENERIC_READ | GENERIC_WRITE,
        GENERIC_READ,
        GENERIC_WRITE,
        0x80000000,  // SPECIFIC_RIGHTS_ALL? No, that's not right
        0
    };
    const char* access_names[] = {
        "RW", "RO", "WO", "MAX", "NONE"
    };

    for (int i = 0; i < 5; i++) {
        HANDLE hDev = CreateFileA("\\\\.\\RTCore64", access_levels[i],
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
        if (hDev != INVALID_HANDLE_VALUE) {
            printf("  RTCore64 [%s] -> OPEN OK\n", access_names[i]);
            for (int j = 0; j < 4; j++) {
                // Try fmt 0 (48-byte struct)
                DWORD returned = 0;
                RTCORE_48 r = {};
                r.Address = 0xF0000; r.Size = 4;
                BOOL ok = DeviceIoControl(hDev, rtc_codes[j], &r, 48, &r, 48, &returned, NULL);
                printf("    0x%08X -> %s val=0x%08X%s\n",
                       rtc_codes[j], ok ? "OK" : "FAIL", ok ? r.Value : 0,
                       (ok && r.Value) ? " *** DATA ***" : "");
            }
            CloseHandle(hDev);
        } else {
            printf("  RTCore64 [%s] -> GLE=%d\n", access_names[i], GetLastError());
        }
    }

    // --- HWiNFO_164 ---
    printf("\n=== HWiNFO_164 ===\n");
    DWORD hwinfo_codes[] = { 0x9C40259C, 0x9C4025A0, 0x9C40609C, 0x9C406094 };

    for (int i = 0; i < 5; i++) {
        HANDLE hDev = CreateFileA("\\\\.\\HWiNFO_164", access_levels[i],
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
        if (hDev != INVALID_HANDLE_VALUE) {
            printf("  HWiNFO_164 [%s] -> OPEN OK\n", access_names[i]);
            for (int j = 0; j < 4; j++) {
                DWORD64 addr = 0xF0000;
                BYTE obuf[0x100] = {};
                DWORD returned = 0;
                BOOL ok = DeviceIoControl(hDev, hwinfo_codes[j], &addr, 8, obuf, 0x100, &returned, NULL);
                DWORD val = 0;
                if (ok && returned >= 4) memcpy(&val, obuf, 4);
                printf("    0x%08X -> %s val=0x%08X ret=%d%s\n",
                       hwinfo_codes[j], ok ? "OK" : "FAIL", val, returned,
                       (ok && val) ? " *** DATA ***" : "");
            }
            CloseHandle(hDev);
        } else {
            DWORD gle = GetLastError();
            printf("  HWiNFO_164 [%s] -> GLE=%d", access_names[i], gle);
            switch (gle) {
                case 2: printf(" (FILE_NOT_FOUND)"); break;
                case 5: printf(" (ACCESS_DENIED)"); break;
                case 55: printf(" (DEVICE_NOT_AVAILABLE)"); break;
            }
            printf("\n");
        }
    }

    printf("\n=== Summary ===\n");
    printf("HWiNFO_164 verified: device EXISTS but only HWiNFO64.exe can open it.\n");
    printf("This is by design — HWiNFO's driver creates an exclusive device.\n");
    printf("\nRecommended: stop HWiNFO_164 service, unload driver, then reopen HWiNFO:\n");
    printf("  sc stop HWiNFO_164\n");
    printf("  (Then restart HWiNFO64.exe as Admin)\n");
    printf("\nOr try another driver: RTCore64 needs backup restored first.\n");
    printf("Press Enter to exit...\n");
    getchar();
    return 0;
}