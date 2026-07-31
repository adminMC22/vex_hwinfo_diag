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

} // namespace sky::game
