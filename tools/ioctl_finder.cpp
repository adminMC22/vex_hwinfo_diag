// ioctl_finder.cpp — brute-force RTCore64 IOCTL codes
// Compile: cl /O2 ioctl_finder.cpp /EHsc
// Run as Admin while Valorant+Vanguard + RTCore64.sys loaded
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

#define MAX_IOCTL_READ 0x1000

struct TestResult {
    DWORD ioctl;
    int format;       // 0=same-buf, 1=addr-in/out, 2=header-buf
    DWORD gle;
    bool has_data;
    int data_bits;    // count of non-zero bytes in first 64
};

// Test one IOCTL code with a specific format
TestResult test_ioctl(HANDLE hDev, DWORD code, bool verbose) {
    TestResult r = { code, 0, 0, false, 0 };
    uint8_t ibuf[0x1100] = {};
    uint8_t obuf[0x1100] = {};

    // Try physical addr 0xF0000 (BIOS area — always readable, has BIOS data)
    uint64_t test_addr = 0xF0000;

    // Format 0: Single buffer [8 pad][8 addr] — same buf in/out
    {
        memset(ibuf, 0, sizeof(ibuf));
        *(uint64_t*)(ibuf + 8) = test_addr;

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(hDev, code,
            ibuf, 16 + 0x100,    // input
            ibuf, 16 + 0x100,    // output
            &returned, nullptr);

        if (ok) {
            int bits = 0;
            for (int i = 16; i < 16 + 64; i++)
                if (ibuf[i] != 0 && ibuf[i] != 0xFF) bits++;
            if (bits > 4) {
                r.format = 0; r.has_data = true; r.data_bits = bits;
                return r;
            }
            if (!verbose) return r;
        }
    }

    // Format 1: [8 pad][8 addr] single buf, but with different size handling
    {
        memset(ibuf, 0, sizeof(ibuf));
        *(uint64_t*)(ibuf + 8) = test_addr;

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(hDev, code,
            ibuf, 16,
            obuf, 0x1000,
            &returned, nullptr);

        if (ok) {
            int bits = 0;
            for (int i = 0; i < 64; i++)
                if (obuf[i] != 0 && obuf[i] != 0xFF) bits++;
            if (bits > 4) {
                r.format = 1; r.has_data = true; r.data_bits = bits;
                return r;
            }
            if (!verbose) return r;
        }
    }

    // Format 2: [8 pad][8 addr][4 size] (20 bytes input)
    {
        memset(ibuf, 0, sizeof(ibuf));
        *(uint64_t*)(ibuf + 8) = test_addr;
        *(uint32_t*)(ibuf + 16) = 0x100;

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(hDev, code,
            ibuf, 20,
            obuf, 0x1000,
            &returned, nullptr);

        if (ok) {
            int bits = 0;
            for (int i = 0; i < 64; i++)
                if (obuf[i] != 0 && obuf[i] != 0xFF) bits++;
            if (bits > 4) {
                r.format = 2; r.has_data = true; r.data_bits = bits;
                return r;
            }
            if (!verbose) return r;
        }
    }

    // Format 3: METHOD_NEITHER style — just addr as 8 byte input
    {
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(hDev, code,
            &test_addr, sizeof(test_addr),
            obuf, 0x1000,
            &returned, nullptr);

        if (ok) {
            int bits = 0;
            for (int i = 0; i < 64; i++)
                if (obuf[i] != 0 && obuf[i] != 0xFF) bits++;
            if (bits > 4) {
                r.format = 3; r.has_data = true; r.data_bits = bits;
                return r;
            }
            if (!verbose) return r;
        }
    }

    // Format 4: [4 size][8 addr] (12 bytes)
    {
        memset(ibuf, 0, sizeof(ibuf));
        *(uint32_t*)ibuf = 0x100;
        *(uint64_t*)(ibuf + 4) = test_addr;

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(hDev, code,
            ibuf, 12,
            obuf, 0x1000,
            &returned, nullptr);

        if (ok) {
            int bits = 0;
            for (int i = 0; i < 64; i++)
                if (obuf[i] != 0 && obuf[i] != 0xFF) bits++;
            if (bits > 4) {
                r.format = 4; r.has_data = true; r.data_bits = bits;
                return r;
            }
            if (!verbose) return r;
        }
    }

    r.gle = GetLastError();
    return r;
}

// CTL_CODE decode
struct IOCTLInfo {
    DWORD raw;
    int device_type;
    int function;
    int method;
    int access;
};

IOCTLInfo decode_ioctl(DWORD code) {
    IOCTLInfo i = { code };
    i.device_type = (code >> 16) & 0xFFFF;
    i.access      = (code >> 14) & 3;
    i.function    = (code >> 2) & 0xFFF;
    i.method      = code & 3;
    return i;
}

int main() {
    printf("=== RTCore64 IOCTL Brute-Forcer ===\n");
    printf("Opening \\\\.\\RTCore64...\n");

    HANDLE hDev = CreateFileA("\\\\.\\RTCore64",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile returned INVALID_HANDLE_VALUE (err=%d)\n", GetLastError());
        printf("Make sure RTCore64.sys is loaded (MSI Afterburner running as Admin)\n");
        return 1;
    }

    printf("Device opened OK (handle=%p)\n", hDev);
    printf("\nStrategy 1: Probe specific known IOCTL codes\n");

    DWORD known_codes[] = {
        0x9C40258C, 0x9C402590, 0x9C402580, 0x9C40609C, 0x9C4060A0,
        0x9C406000, 0x9C406004, 0x9C402400, 0x9C402404,
        0x222400,   0x222004,   0x222003,   0x222403,
        0x80002040, 0x80002044, 0x80002000, 0x80002004,
        0x80002043, 0x80002047, 0x80002003, 0x80002007,
        0x9C40258F, 0x9C402593, 0x9C40609F, 0x9C4060A3,
        0x220000, 0x222000, 0x224000, 0x226000,
    };

    for (auto code : known_codes) {
        auto r = test_ioctl(hDev, code, true);
        if (r.has_data) {
            auto info = decode_ioctl(code);
            printf("  *** WORKING: 0x%08X [dev=0x%04X func=0x%03X "
                   "method=%d access=%d] fmt=%d data_bits=%d\n",
                   code, info.device_type, info.function, info.method,
                   info.access, r.format, r.data_bits);
        }
    }

    printf("\nStrategy 2: Brute-force function codes for known device types\n");
    printf("Trying device types: 0x8000, 0x9C40, 0x22, 0x0000\n");

    int dev_types[] = { 0x8000, 0x9C40, 0x22, 0 };
    int methods[] = { 0, 1, 2, 3 };    // BUFFERED, IN_DIRECT, OUT_DIRECT, NEITHER
    int access_bits[] = { 0, 1, 2, 3 }; // ANY, SPECIFIC, READ, WRITE

    int total = 0;
    int found = 0;

    for (int dt : dev_types) {
        for (int func = 0; func < 0x1000; func++) {
            for (int m : methods) {
                for (int a : access_bits) {
                    DWORD code = (dt << 16) | (a << 14) | (func << 2) | m;
                    total++;
                    auto r = test_ioctl(hDev, code, false);
                    if (r.has_data) {
                        auto info = decode_ioctl(code);
                        printf("  WORKING: 0x%08X [dev=0x%04X func=0x%03X "
                               "method=%d access=%d] fmt=%d bits=%d\n",
                               code, info.device_type, info.function,
                               info.method, info.access, r.format, r.data_bits);
                        found++;
                    }
                }
            }
        }
    }

    printf("\n======= RESULTS =======\n");
    printf("Tested %d IOCTL combinations\n", total);
    printf("Found %d working code(s)\n", found);

    // Re-test working codes with more validation
    printf("\n======= VALIDATION =======\n");
    printf("Reading two different addresses to confirm real data:\n");

    uint8_t buf1[0x200] = {};
    uint8_t buf2[0x200] = {};

    // Read from 0xA0000 (VGA) and 0xF0000 (BIOS) — should be different
    {
        *(uint64_t*)(buf1 + 8) = 0xA0000;
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(hDev, 0x9C40258C,
            buf1, 16 + 0x100, buf1, 16 + 0x100, &ret, nullptr);
        if (ok) printf("  0xA0000 read: %s\n",
            buf1[16] != 0 ? "non-zero data" : "all zeros");
    }

    {
        *(uint64_t*)(buf2 + 8) = 0xF0000;
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(hDev, 0x9C40258C,
            buf2, 16 + 0x100, buf2, 16 + 0x100, &ret, nullptr);
        if (ok) printf("  0xF0000 read: %s\n",
            buf2[16] != 0 ? "non-zero data" : "all zeros");
    }

    // Compare
    if (memcmp(buf1 + 16, buf2 + 16, 0x100) != 0)
        printf("  Data differs — driver reads actual physical memory!\n");
    else
        printf("  Data is identical — driver may return fixed/fake data\n");

    CloseHandle(hDev);
    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}
