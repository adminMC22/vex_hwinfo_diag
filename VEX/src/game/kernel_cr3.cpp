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
    // Two-stage filter + SELF-VERIFICATION:
    //   Stage A (cheap, per page): PML4E[0x1F0] (offset 0xF80) must be a
    //     plausible entry: present bit set, frame bits 12-31 nonzero, and
    //     the high dword must be 0x00000000 or 0x80000000 (NX). Real
    //     PML4Es contain a PHYSICAL frame (bits 12-51) — their high 16
    //     bits are 0x0000 or 0x8000, NEVER 0xFFFF. Kernel POOL pages full
    //     of pointers (0xFFFFF8xx...) fail the high-dword check.
    //   Stage B (rare): walk the KNOWN kernel image VA through the
    //     candidate and require phys == known kernel image PA
    //     (verify_pml4_for_kernel). A random page cannot pass a full
    //     4-level walk with a known answer — this is the definitive test.
    uintptr_t find_kernel_pml4() {
        auto drv = sky::driver::g_driver;

        // Need the known kernel image VA+PA for the self-verification walk.
        // This also forces the kernel-offset init (kernel_offset=OK log).
        uintptr_t kvbase = 0, kpbase = 0;
        if (!sky::driver::kernel_image_offset(&kvbase, &kpbase)) {
            sky::driver::write_state_log("kernel_pml4=NO_IMAGE");
            return 0;
        }

        // Tiered scan ranges. ASMMAP64 (the map backend) reaches all RAM,
        // so tiers now extend to 8GB; each range is clamped to the backend's
        // safe cap below (ThrottleStop keeps its 416MB ceiling).
        static constexpr uintptr_t kRanges[][2] = {
            { 0x100000, 0x800000 },    // 1MB-8MB
            { 0x800000,  0x4000000 },  // 8MB-64MB
            { 0x4000000, 0x10000000 }, // 64MB-256MB
            { 0x10000000, 0x40000000 },// 256MB-1GB
            { 0x40000000, 0x100000000 },// 1GB-4GB
            { 0x100000000, 0x200000000 },// 4GB-8GB (ASMMAP64 only)
        };
        static std::chrono::steady_clock::time_point s_last_prog{};
        static int s_rejected = 0;

        const uintptr_t cap = sky::driver::phys_read_cap();

        for (int range = 0; range < 6; range++) {
            uintptr_t scan_start = kRanges[range][0];
            uintptr_t pass_end = std::min(kRanges[range][1], cap);
            if (scan_start >= cap) break;
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

                // Stage A: PML4E[0x1F0] — low dword (offset 0xF80).
                //   4-byte read = 4 IOCTLs, no jitter sleep.
                uint32_t lo = 0;
                if (!drv->read_physical(pa + 0xF80, &lo, sizeof(lo)))
                    continue;
                if (!(lo & 1)) continue;                    // present bit
                if (!(lo & 0xFFFFF000)) continue;           // frame bits 12-31 nonzero

                // Stage A: high dword (offset 0xF84) — frame bits 32-51 live
                // here (hi bits 0-19, may be nonzero on 8GB boxes), reserved
                // bits 52-62 (hi bits 20-30) must be 0, NX bit 31 free.
                uint32_t hi = 0;
                if (!drv->read_physical(pa + 0xF84, &hi, sizeof(hi)))
                    continue;
                if (hi & 0x7FF00000) continue;       // reserved bits set → not a real entry

                // Stage A2: second kernel entry PML4E[0x1F8] (offset 0xFC0)
                // must also look like a real entry. Real PML4s have many
                // consecutive present kernel-half entries; a random data
                // page passing BOTH 0x1F0 and 0x1F8 is far less likely.
                uint32_t lo2 = 0;
                if (!drv->read_physical(pa + 0xFC0, &lo2, sizeof(lo2)))
                    continue;
                if (!(lo2 & 1)) continue;
                if (!(lo2 & 0xFFFFF000)) continue;
                uint32_t hi2 = 0;
                if (!drv->read_physical(pa + 0xFC4, &hi2, sizeof(hi2)))
                    continue;
                if (hi2 & 0x7FF00000) continue;

                // Stage B: SELF-VERIFY — walk the kernel image VA through
                // this candidate. Only a real PML4 maps it to the known PA.
                if (!sky::driver::verify_pml4_for_kernel(pa, kvbase, kpbase)) {
                    // Log once (first reject) with the count — proves the
                    // verification runs and shows how noisy the filter is.
                    s_rejected++;
                    if (s_rejected == 1) {
                        sky::driver::write_state_log("attach=TRY pml4_verify_reject pa=0x" +
                            std::format("{:x}", pa));
                    }
                    continue;
                }

                // Found a REAL PML4 page — verified by the walk.
                sky::driver::write_state_log("kernel_pml4=0x" +
                    std::format("{:x}", pa) + " VERIFIED");
                LOG_INFO("kernel PML4 found at phys=0x" + std::format("{:x}", pa) +
                    " — verified via kernel-image walk");
                return pa;
            }
        }
        sky::driver::write_state_log("kernel_pml4=NOT_FOUND");
        LOG_WARNING("find_kernel_pml4: no PML4 with ntoskrnl mapping in low RAM");
        return 0;
    }

    // ================================================================
    // REVERSE-WALK bootstrap (the deterministic one)
    // ================================================================
    // Instead of content-matching random pages (which 100+ runs proved
    // keeps hitting kernel POOL pages), DERIVE the PML4 from the kernel
    // image's own page-table chain — the CPU runs on these tables, so the
    // chain MUST exist:
    //
    //   We know: kvbase=0xfffff8052c600000, kpbase=0x3000000
    //   pml4i = (kvbase>>39)&0x1FF = 0x1F0
    //   pdpti = (kvbase>>30)&0x1FF = 0x14
    //   pdi   = (kvbase>>21)&0x1FF = 0x163
    //
    //   Step 1: find the PD page — scan phys for a page P where
    //           P[pdi*8] (offset 0xB18) is a present 2MB large-page
    //           entry (PS bit) whose frame == kpbase. The kernel image
    //           is mapped with 2MB pages on modern Windows, and this
    //           entry is FORCED — no guessing.
    //   Step 2: find the PDPT page — scan phys for a page Q where
    //           Q[pdpti*8] (offset 0xA0) is a present entry whose
    //           frame == P (the PD page from step 1).
    //   Step 3: find the PML4 — scan phys for a page R where
    //           R[pml4i*8] (offset 0xF80) is a present entry whose
    //           frame == Q. R IS the kernel PML4 (or any process PML4 —
    //           they share the kernel half).
    //   Step 4: final self-verification via the full known-answer walk.
    //
    // Each step's frame value is KNOWN, so a single 8-byte comparison
    // per page is decisive. False positives are impossible in step 1
    // (the frame must equal kpbase exactly, with PS present) and are
    // re-verified by steps 2-4 anyway.
    uintptr_t find_kernel_pml4_reverse() {
        auto drv = sky::driver::g_driver;

        uintptr_t kvbase = 0, kpbase = 0;
        if (!sky::driver::kernel_image_offset(&kvbase, &kpbase)) {
            sky::driver::write_state_log("kernel_pml4_rev=NO_IMAGE");
            return 0;
        }

        const uintptr_t kPml4i = (kvbase >> 39) & 0x1FF;  // 0x1F0
        const uintptr_t kPdpti = (kvbase >> 30) & 0x1FF;  // 0x14
        const uintptr_t kPdi   = (kvbase >> 21) & 0x1FF;  // 0x163
        const uintptr_t kPti   = (kvbase >> 12) & 0x1FF;  // 0 for 2MB-aligned vbase (4KB fallback)

        sky::driver::write_state_log("attach=TRY build=rev4k pml4_reverse pml4i=0x" +
            std::format("{:x}", kPml4i) + " pdpti=0x" + std::format("{:x}", kPdpti) +
            " pdi=0x" + std::format("{:x}", kPdi) + " pti=0x" + std::format("{:x}", kPti) +
            " img=0x" + std::format("{:x}", kpbase));

        // Tiered scan ranges: boot page tables are allocated in the first
        // MBs (the boot allocator hands out the lowest pages first), so we
        // hit them in tier 0/1 almost always. Extend only on miss.
        //
        // CAP: the backend decides how far physical reads are safe.
        // ThrottleStop machine-checks (WHEA 0x124) somewhere in 427-428MB
        // (observed 0x124 BSOD mid-scan), so TS clamps to 416MB. ASMMAP64
        // maps \Device\PhysicalMemory — it reaches all installed RAM (8GB),
        // which is exactly what the NO_PT4K verdict requires: tables above
        // the old ceiling. Ranges extend to 8GB; each is clamped below.
        static constexpr uintptr_t kRanges[][2] = {
            { 0x100000,  0x800000 },   // 1MB-8MB
            { 0x800000,  0x4000000 },  // 8MB-64MB
            { 0x4000000, 0x10000000 }, // 64MB-256MB
            { 0x10000000, 0x40000000 },// 256MB-1GB
            { 0x40000000, 0x100000000 },// 1GB-4GB
            { 0x100000000, 0x200000000 },// 4GB-8GB (ASMMAP64 only)
        };
        uintptr_t cap = max_physical();
        uintptr_t drv_cap = sky::driver::phys_read_cap();
        if (cap > drv_cap) cap = drv_cap;
        sky::driver::write_state_log("attach=TRY pml4_rev cap=0x" +
            std::format("{:x}", cap) + " total=0x" +
            std::format("{:x}", max_physical()));

        auto heartbeat = [](const char* stage, uintptr_t pa, uintptr_t end) {
            static std::chrono::steady_clock::time_point s_last;
            auto now = std::chrono::steady_clock::now();
            if (now - s_last >= std::chrono::seconds(4)) {
                s_last = now;
                sky::driver::write_state_log("attach=TRY pml4_rev " +
                    std::string(stage) + " at=0x" + std::format("{:x}", pa) +
                    "/0x" + std::format("{:x}", end));
            }
        };

        // Read a 64-bit table entry with a single 8-byte transfer.
        // The TS IOCTL accepts 1/2/4/8 bytes per call, so an 8-byte read
        // is ONE IOCTL — lo+hi dwords in a single call (vs 8 IOCTLs
        // before chunked reads landed). Per-page cost: 1 IOCTL + 1 jitter
        // sleep; the frame/present checks are pure CPU.
        auto read_entry = [&](uintptr_t pa, uintptr_t off, uintptr_t want_frame,
                              uint64_t& out) -> bool {
            uint64_t e = 0;
            if (!drv->read_physical(pa + off, &e, sizeof(e)))
                return false;
            uint32_t lo = (uint32_t)e;
            uint32_t hi = (uint32_t)(e >> 32);
            if (!(lo & 1)) return false;                       // present bit
            if ((lo & 0xFFFFF000) != (want_frame & 0xFFFFF000)) return false; // frame bits 12-31
            if (hi & 0x7FF00000) return false;                 // reserved bits 52-62 must be 0 (NX free; frame 32-51 may be nonzero on 8GB boxes)
            out = e;
            return true;
        };

        // ---- Step 1: find the PD page (PDE[pdi] maps the kernel image) ----
        uintptr_t pd_page = 0;
        {
            const uintptr_t frame2m = kpbase & 0x000FFFFFFFE00000ULL;  // bits 21-51
            sky::driver::write_state_log("attach=TRY pml4_rev step=pd want=0x" +
                std::format("{:x}", kpbase));
            for (auto& r : kRanges) {
                if (r[0] > cap) break;
                uintptr_t end = std::min(r[1], cap);
                for (uintptr_t pa = r[0]; pa + 0x1000 <= end; pa += 0x1000) {
                    heartbeat("pd", pa, end);
                    uint64_t e = 0;
                    if (!read_entry(pa, kPdi * 8, kpbase, e)) continue;
                    // 2MB large page: PS bit + full frame bits 21-51 must
                    // equal the image frame. (ntoskrnl is usually mapped
                    // with 2MB pages; under VBS/HVCI it can be 4KB-mapped
                    // instead — handled by the fallback pass below.)
                    if ((e & 1ULL << 7) && (e & 0x000FFFFFFFE00000ULL) == frame2m) {
                        pd_page = pa;
                        sky::driver::write_state_log("attach=TRY pml4_rev pd=0x" +
                            std::format("{:x}", pd_page) + " (2MB)");
                        break;
                    }
                }
                if (pd_page) break;
            }

            // 4KB fallback: Vanguard REQUIRES VBS/HVCI, and under HVCI the
            // kernel image is frequently mapped with 4KB pages — PDE[pdi]
            // then has no PS bit and the 2MB pass can never match. Find the
            // PT page first (PTE[pti] frame == image frame), then the PD
            // page whose PDE[pdi] points at that PT page. Both steps use the
            // same frame prefilter, so read cost is unchanged per page.
            if (!pd_page) {
                sky::driver::write_state_log("attach=TRY pml4_rev step=pd4k");
                uintptr_t pt_page = 0;
                for (auto& r : kRanges) {
                    if (r[0] > cap) break;
                    uintptr_t end = std::min(r[1], cap);
                    for (uintptr_t pa = r[0]; pa + 0x1000 <= end; pa += 0x1000) {
                        heartbeat("pt", pa, end);
                        uint64_t t = 0;
                        if (!read_entry(pa, kPti * 8, kpbase, t)) continue;
                        if ((t & 0x000FFFFFFFFFF000ULL) != (kpbase & 0x000FFFFFFFFFF000ULL))
                            continue;
                        pt_page = pa;
                        sky::driver::write_state_log("attach=TRY pml4_rev pt=0x" +
                            std::format("{:x}", pt_page));
                        break;
                    }
                    if (pt_page) break;
                }
                if (!pt_page) {
                    sky::driver::write_state_log("kernel_pml4_rev=NO_PT4K");
                    LOG_WARNING("pml4 reverse: no 4KB PT page mapping the kernel image found");
                    return 0;
                }
                for (auto& r : kRanges) {
                    if (r[0] > cap) break;
                    uintptr_t end = std::min(r[1], cap);
                    for (uintptr_t pa = r[0]; pa + 0x1000 <= end; pa += 0x1000) {
                        heartbeat("pd", pa, end);
                        uint64_t e = 0;
                        if (!read_entry(pa, kPdi * 8, pt_page, e)) continue;
                        if ((e & 0x000FFFFFFFFFF000ULL) != pt_page) continue;
                        pd_page = pa;
                        sky::driver::write_state_log("attach=TRY pml4_rev pd=0x" +
                            std::format("{:x}", pd_page) + " (4KB)");
                        break;
                    }
                    if (pd_page) break;
                }
            }
        }
        if (!pd_page) {
            sky::driver::write_state_log("kernel_pml4_rev=NO_PD");
            LOG_WARNING("pml4 reverse: no PD page with kernel image mapping found");
            return 0;
        }

        // ---- Step 2: find the PDPT page (PDPTE[pdpti] frame == pd_page) ----
        uintptr_t pdpt_page = 0;
        {
            sky::driver::write_state_log("attach=TRY pml4_rev step=pdpt want=0x" +
                std::format("{:x}", pd_page));
            for (auto& r : kRanges) {
                if (r[0] > cap) break;
                uintptr_t end = std::min(r[1], cap);
                for (uintptr_t pa = r[0]; pa + 0x1000 <= end; pa += 0x1000) {
                    heartbeat("pdpt", pa, end);
                    uint64_t e = 0;
                    if (!read_entry(pa, kPdpti * 8, pd_page, e)) continue;
                    if ((e & 0x000FFFFFFFFFF000ULL) != pd_page) continue;
                    pdpt_page = pa;
                    sky::driver::write_state_log("attach=TRY pml4_rev pdpt=0x" +
                        std::format("{:x}", pdpt_page));
                    break;
                }
                if (pdpt_page) break;
            }
        }
        if (!pdpt_page) {
            sky::driver::write_state_log("kernel_pml4_rev=NO_PDPT");
            LOG_WARNING("pml4 reverse: no PDPT page pointing to PD page found");
            return 0;
        }

        // ---- Step 3: find the PML4 (PML4E[pml4i] frame == pdpt_page) ----
        uintptr_t pml4_page = 0;
        {
            sky::driver::write_state_log("attach=TRY pml4_rev step=pml4 want=0x" +
                std::format("{:x}", pdpt_page));
            for (auto& r : kRanges) {
                if (r[0] > cap) break;
                uintptr_t end = std::min(r[1], cap);
                for (uintptr_t pa = r[0]; pa + 0x1000 <= end; pa += 0x1000) {
                    heartbeat("pml4", pa, end);
                    uint64_t e = 0;
                    if (!read_entry(pa, kPml4i * 8, pdpt_page, e)) continue;
                    if ((e & 0x000FFFFFFFFFF000ULL) != pdpt_page) continue;
                    pml4_page = pa;
                    sky::driver::write_state_log("attach=TRY pml4_rev pml4=0x" +
                        std::format("{:x}", pml4_page));
                    break;
                }
                if (pml4_page) break;
            }
        }
        if (!pml4_page) {
            sky::driver::write_state_log("kernel_pml4_rev=NO_PML4");
            LOG_WARNING("pml4 reverse: no PML4 page pointing to PDPT page found");
            return 0;
        }

        // ---- Step 4: final self-verification (full known-answer walk) ----
        if (!sky::driver::verify_pml4_for_kernel(pml4_page, kvbase, kpbase)) {
            sky::driver::write_state_log("kernel_pml4_rev=VERIFY_FAIL");
            LOG_WARNING("pml4 reverse: derived PML4 failed the final walk — chain broken");
            return 0;
        }
        sky::driver::write_state_log("kernel_pml4=0x" +
            std::format("{:x}", pml4_page) + " VERIFIED (reverse)");
        LOG_INFO("kernel PML4 found via REVERSE WALK at phys=0x" +
            std::format("{:x}", pml4_page));
        return pml4_page;
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
        if (sky::driver::phys_read_cap() < 0x100000000ULL)
            full_end = std::min(full_end, sky::driver::phys_read_cap());
        if (full_end > 0x100000000ULL) full_end = 0x100000000ULL;  // cap at 4GB

        // First pass: scan all available RAM (not just 2GB) — EPROCESS can
        // live above 2GB on systems with >2GB RAM. The 2GB heuristic was
        // wrong for this machine (RAM to 3.3GB).
        uintptr_t scan_end = full_end;

        // Log the range whenever it changes.
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

                // DEBUG: log the actual 14-char name we found
                static int s_dbg_names = 0;
                if (s_dbg_names < 5) {
                    char nm[15] = { 0 };
                    drv->read_physical(str_phys, nm, 14);
                    sky::driver::write_state_log("attach=TRY found_name=\"" + std::string(nm) + "\" at 0x" + std::format("{:x}", str_phys));
                    s_dbg_names++;
                }

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

        static bool s_fail_logged = false;
        if (!s_fail_logged) {
            s_fail_logged = true;
            sky::driver::write_state_log("attach=TRY physscan=NO_HIT");
        }
        return false;
    }

} // namespace sky::game
