#pragma once

#include "fname_cache.hpp"

namespace sky {
namespace game {
namespace sdk {

    inline FNameCache& get_global_fname_cache() {
        static FNameCache s_cache{};
        return s_cache;
    }

    inline void reset_global_fname_cache() {
        get_global_fname_cache().invalidate_all();
    }

} // namespace sdk
} // namespace game
} // namespace sky


