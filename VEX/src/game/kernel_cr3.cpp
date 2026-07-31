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
            return 0;
        }

        // PE headers
        auto pe_off = drv->read<uint32_t>(base + 0x3C);
        if (!pe_off || pe_off > 0x1000) return 0;
        auto pe_sig = drv->read<uint32_t>(base + pe_off);
        if ((pe_sig & 0xFFFF) != 0x4550) return 0;     // "PE\0\0"

        auto opt = base + pe_off + 24;                 // optional header
        auto magic = drv->read<uint16_t>(opt);
        if (magic != 0x20B) return 0;                  // PE32+ only

        // Data directory 0 = exports: (VirtualAddress, Size) at opt+0x70
        auto exp_rva = drv->read<uint32_t>(opt + 0x70);
        auto exp_size = drv->read<uint32_t>(opt + 0x74);
        if (!exp_rva || !exp_size) return 0;

        auto exp = base + exp_rva;
        auto num_names  = drv->read<uint32_t>(exp + 24);
        auto addr_funcs = drv->read<uint32_t>(exp + 28);   // RVA of EAT
        auto addr_names = drv->read<uint32_t>(exp + 32);   // RVA of name pointers
        auto addr_ords  = drv->read<uint32_t>(exp + 36);   // RVA of name ordinals
        if (!num_names || num_names > 0x10000 || !addr_names) return 0;

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
        if (!psi_ptr) return false;
        auto sys_eproc = drv->read<uintptr_t>(psi_ptr);
        if (!sys_eproc || sys_eproc < 0xFFFF000000000000ULL) return false;

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
        for (int i = 0; i < 4096; i++) {
            auto flink = drv->read<uintptr_t>(link);
            if (!flink || flink < 0xFFFF000000000000ULL) return false;
            auto eproc = flink - kActiveProcessLinks;
            if (i > 0 && eproc == sys_eproc) break;      // wrapped around

            // Cheap pre-filter: first 8 chars of the image name.
            auto name_first = drv->read<uint64_t>(eproc + kImageFileName);
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
                link = eproc + kActiveProcessLinks;
                continue;
            }
            auto cr3 = drv->read<uintptr_t>(eproc + kDirectoryTableBase) & ~0xFFFULL;
            if (!is_plausible_cr3(cr3)) {
                link = eproc + kActiveProcessLinks;
                continue;
            }
            auto peb = drv->read<uintptr_t>(eproc + kPeb);
            if (!peb || peb > 0x7FFFFFFFFFFFULL) {       // user-mode VA
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
                link = eproc + kActiveProcessLinks;
                continue;
            }
            // Final gate: the PE header must be readable at base (MZ/PE)
            // through this CR3.
            drv->set_dir_base((void*)cr3);
            bool pe_ok = verify_game_pe(base);
            drv->set_dir_base((void*)saved_dtb);
            if (!pe_ok) {
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

} // namespace sky::game
