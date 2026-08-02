#pragma once

#include <cstdint>
#include <string>

namespace sky::game {

    // Locate ntoskrnl.exe and resolve an exported symbol's virtual address.
    // Uses the driver's kernel-VA reads (works via the kernel phys offset).
    uintptr_t find_ntoskrnl_export(const std::string& name);

    // Walk the EPROCESS list from PsInitialSystemProcess and return the
    // DirectoryTableBase (real CR3) of the process with the given PID.
    // Win10 1904x layout: DirectoryTableBase 0x28, UniqueProcessId 0x440,
    // ActiveProcessLinks 0x448. Returns 0 on any failure.
    uintptr_t find_process_cr3(uint32_t pid);

    // Quick plausibility: page-aligned physical address inside RAM.
    bool is_plausible_cr3(uintptr_t cr3);

    // End-to-end check: with the given CR3 as DTB, the game's PE header
    // must be readable at base (MZ/PE signature). Requires set_dir_base(cr3)
    // to have been called by the caller first.
    bool verify_game_pe(uintptr_t game_base);

    // Result of a kernel-only process lookup (no user-mode APIs).
    struct GameProcessInfo {
        uint32_t pid = 0;
        uintptr_t cr3 = 0;   // DirectoryTableBase (real CR3)
        uintptr_t base = 0;  // ImageBaseAddress (from PEB, read via own CR3)
    };

    // Walk the EPROCESS list and find the process whose ImageFileName starts
    // with name_prefix (e.g. "VALORANT-Win64"; max 15 chars stored). Returns
    // true and fills out on success. DTB is restored to whatever it was on
    // entry. Win10 1904x layout: PEB 0x3B8, ImageFileName 0x5A8.
    bool find_game_process(GameProcessInfo& out, const char* name_prefix);

    // Find ANY valid PML4 page (every process's PML4 shares the kernel half,
    // so any PML4 translates kernel VAs) by scanning low physical RAM for
    // the Windows x64 self-referencing entry: PML4E index 0x1ED (PTE_BASE
    // 0xFFFFF68000000000 >> 39 & 0x1FF) must point to the PML4 page itself.
    // Uses 4-byte reads: the 1-byte loop only sleeps (jitter) at 8-byte
    // boundaries, so a 64MB pass is ~16K x 4 IOCTLs with zero sleeps.
    // Returns the PML4 physical address, or 0. Bounds: low physical RAM
    // (PML4 pages are allocated by the boot memory manager in the first
    // ~64MB, extending to 256MB in a second pass if needed).
    uintptr_t find_kernel_pml4();

    // REVERSE-WALK bootstrap (deterministic): derive the PML4 from the
    // kernel image's own page-table chain. We KNOW ntoskrnl's vbase and
    // pbase (kernel_image_offset) — so the PD page containing PDE[pdi]
    // with frame == pbase (2MB large page, PS bit) MUST exist, and the
    // PDPT page whose PDPTE[pdpti] points to it, and the PML4 whose
    // PML4E[pml4i] points to that. Each step is a KNOWN-value scan, not
    // content matching. Final full-walk verification. Tiered scan
    // 1MB→8MB→64MB→256MB→1GB. Returns the PML4 physical address, or 0.
    uintptr_t find_kernel_pml4_reverse();

    // Physical-scan attach: no kernel-pool VA translation required (the
    // ThrottleStop backend's phys offset only maps the ntoskrnl image, so
    // EPROCESS-list reads fail). Instead: bulk-scan physical RAM for the
    // ImageFileName prefix, derive EPROCESS_phys = str_phys - 0x5A8, and
    // read PID / DirectoryTableBase / PEB directly at physical offsets
    // (0x440 / 0x28 / 0x3B8). The ImageBaseAddress is then read through the
    // found CR3 and MZ-verified. Uses g_driver->read_physical() — must be
    // implemented by the backend (bulk IOCTL). DTB is restored on exit.
    // Returns false if the game is not found or validation fails.
    bool find_game_process_phys(GameProcessInfo& out, const char* name_prefix);

} // namespace sky::game
