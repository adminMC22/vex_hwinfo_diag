#pragma once

#include <memory>
#include <atomic>
#include <cstdint>
#include "idriver.hpp"

namespace vex::driver {

    inline std::shared_ptr<IDriver> g_driver;

} // namespace vex::driver


