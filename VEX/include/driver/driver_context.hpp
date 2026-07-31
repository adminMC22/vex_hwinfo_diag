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

} // namespace sky::driver


