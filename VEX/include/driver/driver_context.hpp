#pragma once

#include <memory>
#include <atomic>
#include <cstdint>
#include <Windows.h>
#include "idriver.hpp"

namespace sky::driver {

    extern HANDLE g_hwinfo_device;  // Defined in driver_impl.cpp
    inline std::shared_ptr<IDriver> g_driver;

    // Append one line to %TEMP%\app.log (diagnostics for init chain).
    // Implemented in driver_impl.cpp. Safe to call from any thread.
    void write_state_log(const std::string& line);

    // ntoskrnl image VA+PA (ensures the kernel-offset init ran). Returns
    // false if the image couldn't be located in physical memory.
    bool kernel_image_offset(uintptr_t* vbase, uintptr_t* pbase);

    // Highest physical address the current backend may safely read
    // (exclusive). ThrottleStop: ~416MB WHEA ceiling. ASMMAP64: all RAM.
    // Scanners must never probe at or above this.
    uintptr_t phys_read_cap();

    // Walk a kernel VA through a candidate PML4 page table and require the
    // result to match the known ntoskrnl physical base. Used to prove a
    // scanned page is a REAL PML4 (self-verifying attach bootstrap).
    bool verify_pml4_for_kernel(uintptr_t pml4_pa, uintptr_t vbase, uintptr_t pbase);

} // namespace sky::driver


