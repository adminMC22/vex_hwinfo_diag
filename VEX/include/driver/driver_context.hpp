#pragma once

#include <memory>
#include <atomic>
#include <cstdint>
#include <Windows.h>
#include "idriver.hpp"

namespace sky::driver {

    extern HANDLE g_hwinfo_device;  // Defined in driver_impl.cpp
    inline std::shared_ptr<IDriver> g_driver;

} // namespace sky::driver


