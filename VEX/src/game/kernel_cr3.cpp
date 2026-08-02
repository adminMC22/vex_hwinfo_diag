#include "../../include/common.hpp"
#include "../../include/driver/driver_context.hpp"
#include "../../include/driver/idriver.hpp"
#include "../../include/game/kernel_cr3.hpp"
#include "../../include/utils/logger.hpp"

namespace sky::game {

    // Max physical address derived from the system RAM size.
    static uintptr_t max_physical() {
        static uintptr_t s_max = 0;
        if (s_max == 0) {
            MEMORYSTATUSEX ms{ sizeof(ms) };
            if (GlobalMemoryStatusEx(&ms)) {
                s_max = static_cast<uintptr_t>(ms.ullTotalPhys);
            }
            if (s_max < 0x100000000ULL) s_max = 0x100000000ULL; // >= 4GB
        }
        return s_max;
    }

    bool is_plausible_cr3(uintptr_t cr3) {
        if (!cr3 || cr3 < 0x1000) return false;
        if ((cr3 & 0xFFF) != 0) return false;          // must be page-aligned
        return cr3 < max_physical();                   // must be a physical address
    }

    // Scan low physical RAM for a PML4 whose self-referencing entry
    // (PML4E[0x1ED], PTE_BASE 0xFFFFF68000000000) points to the page
    // itself. See the declaration in kernel_cr3.hpp for the rationale.
    // Two passes: 1MB-64MB, then 64MB-256MB if the first misses.
    // Scan low physical RAM for the SYSTEM process PML4 page. The System
    // (PID 4) PML4 has unique traits vs. user process PML4s:
    //   - empty user half (PML4E[0..255] all zero) — System has no user VA.
    //   - kernel half populated, including PML4E[0x1F0]/[0x1F8] (the ranges
    //     that map ntoskrnl.exe itself — required for the export walk to
    //     work on the page we pick).
    // Filter pipeline, all 1-byte-friendly:
    //   page+6 (high 2 bytes of PML4E[0]) must NOT be 0xFFFF (otherwise a
    //     user PML4 with random high bits would pass).
    //   The kernel half first entry PML4E[256] (=page+2048) must be a
    //     canonical kernel VA (high 16 = 0xFFFF) and present.
    //   The SYSTEM-PML4 gate: read 2 user-half entries; both must be 0.
    //   Then verify the self-referencing entry (scanning all 512 indices).
    //   Finally reject if PML4E[0x1F0] (the entry used to read ntoskrnl)
    //     is absent — so we can't pick a PML4 the export walk can't use.
    // Find ANY PML4 page (System or user — both work for kernel VA
    // translation because user PML4s inherit the kernel-half mappings
    // from the System PML4).
    //
    // Filter (operates at offsets within the page; cost = ~4 IOCTLs/page):
    //   PML4E[0x1F0] (offset 0xF80) must be present and canonical kernel
    //     — this is the entry that maps ntoskrnl.exe at vbase 0xfffff805..
    //   PML4E[0x1F8] (offset 0xFC0) must be present and canonical kernel
    //     — adjacent kernel entry; random pages almost never have BOTH set
    //     to canonical kernel present pointers at the right offsets.
    //
    // We don't need a self-referencing entry — we never read the page's
    // self-mapping; we just walk the page tables through this DTB.
    // Self-ref OR not, System or user PML4 — all translate kernel VAs.
    uintptr_t find_kernel_pml4() {
        auto drv = sky::driver::g_driver;

        // Three-tier scan ranges (System PML4 usually in tier 0).
        static constexpr uintptr_t kRanges[][2] = {
            { 0x100000, 0x800000 },    // 1MB-8MB
            { 0x800000,  0x4000000 },  // 8MB-64MB
            { 0x4000000, 0x10000000 }, // 64MB-256MB
        };
        static std::chrono::steady_clock::time_point s_last_prog{};

        for (int range = 0; range < 3; range++) {
            uintptr_t scan_start = kRanges[range][0];
            uintptr_t pass_end = kRanges[range][1];
            sky::driver::write_state_log("attach=TRY pml4_scan range=" +
                std::to_string(range) +
                " 0x" + std::format("{:x}", scan_start) +
                "-0x" + std::format("{:x}", pass_end));

            for (uintptr_t pa = scan_start; pa + 0x1000 <= pass_end; pa += 0x1000) {
                // Progress heartbeat (~every 4s).
                auto pnow = std::chrono::steady_clock::now();
                if (pnow - s_last_prog >= std::chrono::seconds(4)) {
                    s_last_prog = pnow;
                    sky::driver::write_state_log("attach=TRY pml4_scan "
                        "at=0x" + std::format("{:x}", pa) +
                        "/0x" + std::format("{:x}", pass_end));
                }

                // Pre-filter: 2-byte read at PML4E[0x1F0] high bytes.
                // Real kernel PML4s have this entry canonical (high 16 = 0xFFFF)
                // AND present. The 2-byte read at offset 0xF80+6 gives high
                // bits; we compare to 0xFFFF (kernel canonical). Random pages
                // almost never satisfy this at exactly position 0xF86.
                uint16_t hi16_1F0 = 0;
                if (!drv->read_physical(pa + 0xF80 + 6, &hi16_1F0, sizeof(hi16_1F0)))
                    continue;
                if (hi16_1F0 != 0xFFFF) continue;

                // Verify PML4E[0x1F0]'s PRESENT bit at offset 0xF80 low byte.
                uint8_t p0 = 0;
                if (!drv->read_physical(pa + 0xF80, &p0, sizeof(p0)))
                    continue;
                if (!(p0 & 1)) continue;

                // Strong second check: PML4E[0x1F8] at offset 0xFC0 — high
                // 2 bytes are also 0xFFFF, and present bit set. Random
                // pages rarely have BOTH 0x1F0 and 0x1F8 set correctly.
                uint16_t hi16_1F8 = 0;
                if (!drv->read_physical(pa + 0xFC0 + 6, &hi16_1F8, sizeof(hi16_1F8)))
                    continue;
                if (hi16_1F8 != 0xFFFF) continue;
                uint8_t p1 = 0;
                if (!drv->read_physical(pa + 0xFC0, &p1, sizeof(p1)))
                    continue;
                if (!(p1 & 1)) continue;

                // Found a real PML4 page (System or user — both work).
                sky::driver::write_state_log("kernel_pml4=0x" +
                    std::format("{:x}", pa));
                LOG_INFO("kernel PML4 found at phys=0x" + std::format("{:x}", pa) +
                    " (PML4E[0x1F0]=0x" + std::format("{:x}", hi16_1F0) +
                    ", PML4E[0x1F8]=0x" + std::format("{:x}", hi16_1F8) + ")");
                return pa;
            }
        }
        sky::driver::write_state_log("kernel_pml4=NOT_FOUND");
        LOG_WARNING("find_kernel_pml4: no PML4 with ntoskrnl mapping in low RAM");
        return 0;
    }

    // One-shot state-log line for the exact stage where the ntoskrnl export
    // parse bailed, with the values that caused it — makes the next app.log
    // diagnostic without more guessing.
    static void log_export_fail(const std::string& stage, uintptr_t a = 0, uintptr_t b = 0, uintptr_t c = 0) {
        static std::string s_last;
        std::string msg = "attach=TRY nt_export=fail stage=" + stage;
        if (a) msg += " a=0x" + std::format("{:x}", a);
        if (b) msg += " b=0x" + std::format("{:x}", b);
        if (c) msg += " c=0x" + std::format("{:x}", c);
        if (msg != s_last) {
            s_last = msg;
            sky::driver::write_state_log(msg);
        }
    }

    uintptr_t find_ntoskrnl_export(const std::string& name) {
        static std::string s_cached_name;
        static uintptr_t s_cached_va = 0;
        if (s_cached_name == name && s_cached_va) {
            return s_cached_va;
        }

        auto drv = sky::driver::g_driver;
        auto base = drv->get_kernel_base("ntoskrnl.exe");
        if (!base) {
            LOG_WARNING("nt export: ntoskrnl.exe not in module list");
            log_export_fail("base", base);
            return 0;
        }

        // PE headers
        auto pe_off = drv->read<uint32_t>(base + 0x3C);
        if (!pe_off || pe_off > 0x1000) {
            log_export_fail("pe_off", base, pe_off);
            return 0;
        }
        auto pe_sig = drv->read<uint32_t>(base + pe_off);
        if ((pe_sig & 0xFFFF) != 0x4550) {           // "PE\0\0"
            log_export_fail("pe_sig", base + pe_off, pe_sig);
            return 0;
        }

        auto opt = base + pe_off + 24;                 // optional header
        auto magic = drv->read<uint16_t>(opt);
        if (magic != 0x20B) {                          // PE32+ only
            log_export_fail("magic", opt, magic);
            return 0;
        }

        // Data directory 0 = exports: (VirtualAddress, Size) at opt+0x70
        auto exp_rva = drv->read<uint32_t>(opt + 0x70);
        auto exp_size = drv->read<uint32_t>(opt + 0x74);
        if (!exp_rva || !exp_size) {
            log_export_fail("expdir", exp_rva, exp_size);
            return 0;
        }

        auto exp = base + exp_rva;
        auto num_names  = drv->read<uint32_t>(exp + 24);
        auto addr_funcs = drv->read<uint32_t>(exp + 28);   // RVA of EAT
        auto addr_names = drv->read<uint32_t>(exp + 32);   // RVA of name pointers
        auto addr_ords  = drv->read<uint32_t>(exp + 36);   // RVA of name ordinals
        if (!num_names || num_names > 0x10000 || !addr_names) {
            log_export_fail("names", num_names, addr_names);
            return 0;
        }

        // Fast path: 8-byte prefix compare before reading the whole name.
        uint64_t want_prefix = 0;
        for (int c = 0; c < 8 && c < (int)name.size(); c++) {
            want_prefix |= (uint64_t)(uint8_t)name[c] << (c * 8);
        }

        for (uint32_t i = 0; i < num_names; i++) {
            auto name_rva = drv->read<uint32_t>(base + addr_names + i * 4);
            if (!name_rva) continue;
            auto name_va = base + name_rva;
            auto prefix = drv->read<uint64_t>(name_va);
            if (prefix != want_prefix) continue;
            // Read the name (bounded scan for NUL, max 64 chars)
            char buf[64] = { 0 };
            for (int c = 0; c < 63; c++) {
                auto ch = drv->read<uint8_t>(name_va + c);
                if (!ch) break;
                buf[c] = (char)ch;
            }
            if (name == std::string(buf)) {
                auto ord = drv->read<uint16_t>(base + addr_ords + i * 2);
                auto func_rva = drv->read<uint32_t>(base + addr_funcs + ord * 4);
                if (func_rva) {
                    s_cached_name = name;
                    s_cached_va = base + func_rva;
                    return s_cached_va;
                }
            }
        }
        log_export_fail("notfound", num_names);
        LOG_WARNING("nt export: \"" + name + "\" not found");
        return 0;
    }

    uintptr_t find_process_cr3(uint32_t pid) {
        if (!pid) return 0;
        auto drv = sky::driver::g_driver;

        // PsInitialSystemProcess is a pointer variable (VA of EPROCESS*)
        auto psi_ptr = find_ntoskrnl_export("PsInitialSystemProcess");
        if (!psi_ptr) return 0;

        auto sys_eproc = drv->read<uintptr_t>(psi_ptr);
        if (!sys_eproc || sys_eproc < 0xFFFF000000000000ULL) {
            LOG_WARNING("kproc: PsInitialSystemProcess value invalid 0x" +
                std::format("{:x}", sys_eproc));
            return 0;
        }

        // Win10 1904x KPROCESS layout
        constexpr uintptr_t kUniqueProcessId  = 0x440;
        constexpr uintptr_t kActiveProcessLinks = 0x448;
        constexpr uintptr_t kDirectoryTableBase = 0x28;

        auto link = sys_eproc + kActiveProcessLinks;
        for (int i = 0; i < 4096; i++) {
            auto flink = drv->read<uintptr_t>(link);
            if (!flink || flink < 0xFFFF000000000000ULL) return 0;
            auto eproc = flink - kActiveProcessLinks;
            if (i > 0 && eproc == sys_eproc) break;      // wrapped around
            auto current_pid = drv->read<uintptr_t>(eproc + kUniqueProcessId);
            if (current_pid == pid) {
                auto cr3 = drv->read<uintptr_t>(eproc + kDirectoryTableBase);
                cr3 &= ~0xFFFULL;                          // drop PCID/ASID flags
                if (is_plausible_cr3(cr3)) {
                    LOG_INFO("kproc: found CR3=0x" + std::format("{:x}", cr3) + " for PID " + std::to_string(pid));
                    return cr3;
                }
                LOG_WARNING("kproc: PID match but CR3 implausible 0x" + std::format("{:x}", cr3));
                return 0;
            }
            link = eproc + kActiveProcessLinks;
        }
        LOG_WARNING("kproc: PID " + std::to_string(pid) + " not found in EPROCESS list");
        return 0;
    }

    bool verify_game_pe(uintptr_t game_base) {
        if (!game_base) return false;
        auto drv = sky::driver::g_driver;
        auto pe_off = drv->read<uint32_t>(game_base + 0x3C);
        if (!pe_off || pe_off > 0x1000) return false;
        auto pe_sig = drv->read<uint32_t>(game_base + pe_off);
        if ((pe_sig & 0xFFFF) != 0x4550) return false;
        auto mz = drv->read<uint16_t>(game_base);
        return mz == 0x5A4D;                              // 'MZ'
    }

    bool find_game_process(GameProcessInfo& out, const char* name_prefix) {
        out = {};
        if (!name_prefix) return false;
        auto drv = sky::driver::g_driver;

        auto psi_ptr = find_ntoskrnl_export("PsInitialSystemProcess");
        if (!psi_ptr) {
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                sky::driver::write_state_log("attach=TRY nt_export_fail");
            }
            return false;
        }
        auto sys_eproc = drv->read<uintptr_t>(psi_ptr);
        if (!sys_eproc || sys_eproc < 0xFFFF000000000000ULL) {
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                sky::driver::write_state_log("attach=TRY sys_eproc_invalid");
            }
            return false;
        }

        // Win10 1904x EPROCESS layout
        constexpr uintptr_t kUniqueProcessId    = 0x440;
        constexpr uintptr_t kActiveProcessLinks = 0x448;
        constexpr uintptr_t kDirectoryTableBase = 0x28;
        constexpr uintptr_t kPeb                = 0x3B8;
        constexpr uintptr_t kImageFileName      = 0x5A8;

        // 8-byte prefix compare ("VALORANT-Win64" fits in the first 8 chars
        // as "VALORANT"; full 14-byte compare below on candidate entries).
        uint64_t want_first = 0;
        for (int c = 0; c < 8 && name_prefix[c]; c++) {
            want_first |= (uint64_t)(uint8_t)name_prefix[c] << (c * 8);
        }

        auto link = sys_eproc + kActiveProcessLinks;
        static int s_samples_written = 0;
        for (int i = 0; i < 4096; i++) {
            auto flink = drv->read<uintptr_t>(link);
            if (!flink || flink < 0xFFFF000000000000ULL) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY walk_abort");
                }
                return false;
            }
            auto eproc = flink - kActiveProcessLinks;
            if (i > 0 && eproc == sys_eproc) break;      // wrapped around

            // Cheap pre-filter: first 8 chars of the image name.
            auto name_first = drv->read<uint64_t>(eproc + kImageFileName);
            if (s_samples_written < 8) {
                // One-shot sample of the first entries (max 8 lines ever):
                // proves the walk is live and the ImageFileName offset is
                // right, without flooding the log.
                char nm[9] = { 0 };
                for (int c = 0; c < 8; c++) nm[c] = (char)(uint8_t)(name_first >> (c * 8));
                sky::driver::write_state_log(std::string("attach=TRY sample[") +
                    std::to_string(s_samples_written) + "]=" + nm);
                s_samples_written++;
            }
            if (name_first != want_first) {
                link = eproc + kActiveProcessLinks;
                continue;
            }
            // Full prefix compare (chars 8..13).
            auto name_rest = drv->read<uint64_t>(eproc + kImageFileName + 8);
            bool match = true;
            for (int c = 8; c < 14 && name_prefix[c]; c++) {
                if ((uint8_t)(name_rest >> ((c - 8) * 8)) != (uint8_t)name_prefix[c]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                link = eproc + kActiveProcessLinks;
                continue;
            }

            auto pid = drv->read<uintptr_t>(eproc + kUniqueProcessId);
            if (pid < 4 || pid > 0xFFFF) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY reject stage=pid");
                }
                link = eproc + kActiveProcessLinks;
                continue;
            }
            auto cr3 = drv->read<uintptr_t>(eproc + kDirectoryTableBase) & ~0xFFFULL;
            if (!is_plausible_cr3(cr3)) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY reject stage=cr3");
                }
                link = eproc + kActiveProcessLinks;
                continue;
            }
            auto peb = drv->read<uintptr_t>(eproc + kPeb);
            if (!peb || peb > 0x7FFFFFFFFFFFULL) {       // user-mode VA
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY reject stage=peb");
                }
                link = eproc + kActiveProcessLinks;
                continue;
            }

            // ImageBaseAddress = PEB+0x10, read via the process's own CR3.
            // Save and restore the caller's DTB so nothing else is disturbed.
            auto saved_dtb = drv->get_dtb();
            drv->set_dir_base((void*)cr3);
            auto base = drv->read<uintptr_t>(peb + 0x10);
            drv->set_dir_base((void*)saved_dtb);

            if (!base || base > 0x7FFFFFFFFFFFULL || base < 0x10000) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY reject stage=base");
                }
                link = eproc + kActiveProcessLinks;
                continue;
            }
            // Final gate: the PE header must be readable at base (MZ/PE)
            // through this CR3.
            drv->set_dir_base((void*)cr3);
            bool pe_ok = verify_game_pe(base);
            drv->set_dir_base((void*)saved_dtb);
            if (!pe_ok) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    sky::driver::write_state_log("attach=TRY reject stage=pe");
                }
                link = eproc + kActiveProcessLinks;
                continue;
            }

            out.pid = (uint32_t)pid;
            out.cr3 = cr3;
            out.base = base;
            LOG_INFO("kproc: game found pid=" + std::to_string(out.pid) +
                " cr3=0x" + std::format("{:x}", cr3) +
                " base=0x" + std::format("{:x}", base));
            return true;
        }
        return false;
    }

    // Win10 1904x EPROCESS field offsets (physical reads, no VA translation).
    static constexpr uintptr_t kDirBasePhys = 0x28;      // DirectoryTableBase
    static constexpr uintptr_t kPidPhys     = 0x440;     // UniqueProcessId
    static constexpr uintptr_t kPebPhys     = 0x3B8;     // Peb
    static constexpr uintptr_t kImgPhys     = 0x5A8;     // ImageFileName (15B)

    bool find_game_process_phys(GameProcessInfo& out, const char* name_prefix) {
        if (!name_prefix || !name_prefix[0]) return false;
        auto drv = sky::driver::g_driver;

        // Prefix length capped at the 15-byte ImageFileName field.
        size_t prefix_len = 0;
        while (prefix_len < 15 && name_prefix[prefix_len]) prefix_len++;
        if (prefix_len == 0) return false;

        uintptr_t scan_start = 0x100000;
        uintptr_t full_end = max_physical();
        if (full_end > 0x100000000ULL) full_end = 0x100000000ULL;  // cap at 4GB
        // Two-stage range: kernel pool (EPROCESS) almost always lives below
        // 2GB on typical machines, so the first pass is fast. Only if that
        // misses do later passes extend to the full range.
        static bool s_stage2 = false;
        uintptr_t scan_end = s_stage2 ? full_end : std::min(full_end, 0x80000000ULL);

        // Log the range whenever it changes (first pass + stage-2 pass).
        static uintptr_t s_logged_end = 0;
        if (s_logged_end != scan_end) {
            s_logged_end = scan_end;
            sky::driver::write_state_log("attach=TRY physscan range=0x" +
                std::format("{:x}", scan_start) + "-0x" + std::format("{:x}", scan_end));
        }

        // 1-byte-only backend: a multi-GB scan would take hours. Bail out
        // fast so the EPROCESS walk fallback (and the rest of the engine)
        // still runs.
        size_t chunk_size = drv->max_bulk_chunk();
        if (chunk_size == 0) {
            static bool s_no_bulk = false;
            if (!s_no_bulk) {
                s_no_bulk = true;
                sky::driver::write_state_log("attach=TRY physscan=NO_BULK driver");
            }
            return false;
        }
        if (chunk_size > 0x10000) chunk_size = 0x10000;

        // Overlap chunks by (prefix_len-1) bytes so an ImageFileName
        // straddling a chunk boundary is never missed.
        const size_t step = chunk_size - (prefix_len - 1);

        // Don't rescan more often than every 10s — a full pass takes
        // seconds and we don't want to pin the engine thread while the
        // game isn't up.
        static std::chrono::steady_clock::time_point s_last_scan{};
        auto s_now = std::chrono::steady_clock::now();
        if (s_now - s_last_scan < std::chrono::seconds(10))
            return false;
        s_last_scan = s_now;

        std::vector<uint8_t> chunk(chunk_size);
        auto saved_dtb = drv->get_dtb();
        static std::chrono::steady_clock::time_point s_last_prog{};

        for (uintptr_t pa = scan_start; pa + chunk_size <= scan_end; pa += step) {
            if (!drv->read_physical(pa, chunk.data(), chunk_size))
                continue;
            for (uintptr_t off = 0; off + prefix_len <= chunk_size; off++) {
                if (memcmp(chunk.data() + off, name_prefix, prefix_len) != 0)
                    continue;

                // Candidate: EPROCESS ImageFileName hit → EPROCESS phys is
                // 0x5A8 bytes below the string.
                uintptr_t str_phys = pa + off;
                uintptr_t eproc_phys = str_phys - kImgPhys;
                if (eproc_phys < scan_start) continue;

                // Validate PID / CR3 / PEB directly at physical offsets.
                uint32_t pid = 0;
                if (!drv->read_physical(eproc_phys + kPidPhys, &pid, sizeof(pid)))
                    continue;
                if (pid < 4 || pid > 0xFFFF) continue;

                uintptr_t cr3 = 0;
                if (!drv->read_physical(eproc_phys + kDirBasePhys, &cr3, sizeof(cr3)))
                    continue;
                cr3 &= ~0xFFFULL;
                if (!is_plausible_cr3(cr3)) continue;

                uintptr_t peb = 0;
                if (!drv->read_physical(eproc_phys + kPebPhys, &peb, sizeof(peb)))
                    continue;
                if (!peb || peb > 0x7FFFFFFFFFFFULL) continue;

                // ImageBaseAddress = PEB+0x10 via the found CR3, MZ-verified.
                drv->set_dir_base((void*)cr3);
                auto base = drv->read<uintptr_t>(peb + 0x10);
                bool pe_ok = (base >= 0x10000 && base <= 0x7FFFFFFFFFFFULL)
                    ? verify_game_pe(base) : false;
                drv->set_dir_base((void*)saved_dtb);
                if (!pe_ok) continue;

                out.pid = pid;
                out.cr3 = cr3;
                out.base = base;
                sky::driver::write_state_log("attach=OK physscan pid=" +
                    std::to_string(pid) + " eproc=0x" + std::format("{:x}", eproc_phys) +
                    " cr3=0x" + std::format("{:x}", cr3) +
                    " base=0x" + std::format("{:x}", base));
                return true;
            }

            // Progress heartbeat (~every 4s) so app.log shows the scan
            // moving instead of looking hung.
            auto pnow = std::chrono::steady_clock::now();
            if (pnow - s_last_prog >= std::chrono::seconds(4)) {
                s_last_prog = pnow;
                sky::driver::write_state_log("attach=TRY physscan 0x" +
                    std::format("{:x}", pa) + "/0x" + std::format("{:x}", scan_end));
            }
        }

        if (!s_stage2 && scan_end < full_end) {
            s_stage2 = true;  // stage-1 pass missed — extend next pass to full range
        }
        static bool s_fail_logged = false;
        if (!s_fail_logged) {
            s_fail_logged = true;
            sky::driver::write_state_log("attach=TRY physscan=NO_HIT");
        }
        return false;
    }

} // namespace sky::game
