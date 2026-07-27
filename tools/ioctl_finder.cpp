// ioctl_finder.cpp — RTCore64 physical memory IOCTL finder (v2)
// Uses the correct 48-byte struct from CVE-2019-16098 PoC
// Compile: cl /O2 ioctl_finder.cpp /EHsc
// Run as Admin while Valorant+Vanguard + RTCore64.sys loaded

#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma pack(push, 1)
struct RTCORE_READ {
    BYTE   pad0[8];   // 0-7
    DWORD64 Address;  // 8-15
    BYTE   pad1[8];   // 16-23
    DWORD  Size;      // 24-27: must be 1,2,4
    DWORD  Value;     // 28-31: read result
    BYTE   pad2[16];  // 32-47
};
#pragma pack(pop)

struct ProbeResult {
    DWORD ioctl;
    int input_size;   // bytes passed as nInBufferSize
    bool ok;          // DeviceIoControl returned TRUE
    DWORD gle;        // GetLastError
    DWORD value;      // Value field after read
    DWORD returned;   // bytes returned
};

// Probe a specific IOCTL code with a specific input buffer size
// Uses the 48-byte struct layout.
ProbeResult probe(HANDLE hDev, DWORD code, int in_size, int out_size) {
    ProbeResult r = { code, in_size, false, 0, 0, 0 };
    BYTE buf[64] = {};

    RTCORE_READ* req = (RTCORE_READ*)buf;
    req->Address = 0xF0000;  // BIOS area — should have non-zero data
    req->Size = 4;

    r.ok = DeviceIoControl(hDev, code,
        buf, in_size,
        buf, out_size,
        &r.returned, NULL);

    if (!r.ok) {
        r.gle = GetLastError();
    } else {
        r.value = req->Value;
    }
    return r;
}

int main() {
    printf("=== RTCore64 IOCTL Brute-Forcer v2 ===\n\n");

    printf("Opening \\\\.\\RTCore64...\n");
    HANDLE hDev = CreateFileA("\\\\.\\RTCore64",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile (GLE=%d)\n", GetLastError());
        printf("Make sure RTCore64.sys is loaded (sc query RTCore64)\n");
        printf("Try: sc start RTCore64  (as Admin)\n");
        printf("\nPress Enter to exit...");
        getchar();
        return 1;
    }
    printf("Device opened OK (handle=%p)\n\n", hDev);

    // Phase 1: Known candidate codes with multiple input sizes
    printf("=== Phase 1: Known IOCTL codes × input sizes ===\n");

    DWORD candidate_codes[] = {
        0x80002048, 0x8000204C,  // CVE-2019-16098 canonical codes
        0x80002040, 0x80002044,  // Alternate codes from some PoCs
        0x9C40258C, 0x9C402590,  // Wrong device type but trying anyway
        0x222400,   0x222004,    // FILE_DEVICE_UNKNOWN variants
        0x80002000, 0x80002004,  // Function 0 codes
        0x80002043, 0x80002047,  // METHOD_NEITHER variants of 0x80002040/44
        0x8000204B, 0x8000204F,  // METHOD_NEITHER variants of 0x80002048/4C
    };

    int input_sizes[] = { 16, 20, 24, 28, 32, 40, 48, 56, 64 };
    // Note: 48 = sizeof(RTCORE_READ), but try other sizes too

    int found = 0;

    for (auto code : candidate_codes) {
        for (auto insz : input_sizes) {
            auto r = probe(hDev, code, insz, insz);
            if (r.ok) {
                printf("  WORKING: 0x%08X in=%d val=0x%08X ret=%d gle=%d\n",
                    r.ioctl, r.input_size, r.value, r.returned, r.gle);
                found++;
            }
        }
    }

    if (found == 0) {
        printf("  No working combination found among candidates\n");
    }
    printf("\n");

    // Phase 2: If nothing found, brute-force device type 0x8000 (most likely)
    if (found == 0) {
        printf("=== Phase 2: Brute-force device type 0x8000 ===\n");
        printf("This tests 0x1000 function codes × 4 methods × 4 access = 65536 codes\n");
        printf("with input sizes 24, 48 (tested as same for in/out)\n");
        printf("Progress dots every 256 codes:\n");

        int tested = 0;
        for (int func = 0; func < 0x1000; func++) {
            for (int method = 0; method < 4; method++) {
                for (int access = 0; access < 4; access++) {
                    DWORD code = (0x8000 << 16) | (access << 14) | (func << 2) | method;

                    // Try 48-byte input size
                    auto r = probe(hDev, code, 48, 48);
                    if (r.ok) {
                        printf("\n  WORKING: 0x%08X [func=0x%03X method=%d access=%d] "
                               "val=0x%08X\n",
                               r.ioctl, func, method, access, r.value);
                        found++;
                    }

                    // Also try 24-byte input size (some driver versions)
                    if (code != 0x80002048 && code != 0x80002040) {
                        r = probe(hDev, code, 24, 24);
                        if (r.ok) {
                            printf("\n  WORKING: 0x%08X [func=0x%03X method=%d access=%d] "
                                   "24-byte val=0x%08X\n",
                                   r.ioctl, func, method, access, r.value);
                            found++;
                        }
                    }

                    tested++;
                }
            }
            if ((func & 0xF) == 0) {  // every 16 functions = 256 codes
                printf(".");
                fflush(stdout);
            }
        }
        printf("\nTested %d combinations\n", tested);
    }

    // Phase 3: Validation
    printf("\n=== RESULTS ===\n");
    if (found > 0) {
        printf("Found %d working IOCTL code(s)!\n", found);
        printf("Use the first working code (0x%08X) in your Sky.exe read_physical.\n",
               candidate_codes[0]);  // Will be updated after run
    } else {
        printf("NO working IOCTL code found.\n");
        printf("Possible causes:\n");
        printf("  1. RTCore64.sys version has the physical memory handler removed\n");
        printf("  2. VBS/HVCI is blocking the IOCTL (returns fake/garbled data)\n");
        printf("  3. Access mask too restrictive (try different CreateFile flags)\n");
    }

    CloseHandle(hDev);
    printf("\nPress Enter to exit...");
    getchar();
    return found > 0 ? 0 : 1;
}
