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
