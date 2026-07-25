#pragma once

/**
 * @brief Common headers - Includes all standard and project headers
 *
 * This file should be included in all project .cpp files
 * to maintain consistency and organization of includes.
 */

// ========== Standard C++20 Headers ==========
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>
#include <set>
#include <unordered_set>
#include <queue>
#include <stack>
#include <list>
#include <deque>

// Threading
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <future>

// Time
#include <chrono>

// Functional
#include <functional>
#include <algorithm>
#include <ranges>
#include <concepts>

// IO
#include <iostream>
#include <fstream>
#include <sstream>
#include <format>

// Utilities
#include <utility>
#include <type_traits>
#include <optional>
#include <variant>
#include <any>
#include <exception>
#include <stdexcept>

// C headers
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>

// Windows specific
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

#include <xorstr.hpp>
#include <nlohmann/json.hpp>

// ========== Basic Sky Project Headers ==========
// Note: Specific headers should be included individually in .cpp files
// to avoid circular dependencies

// ========== Common namespace aliases ==========
namespace sky {
    // Aliases for commonly used types
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i8 = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    using f32 = float;
    using f64 = double;

    // Aliases for common containers
    template<typename T>
    using vector = std::vector<T>;

    template<typename T>
    using shared_ptr = std::shared_ptr<T>;

    template<typename T>
    using unique_ptr = std::unique_ptr<T>;

    template<typename T>
    using atomic = std::atomic<T>;

    using string = std::string;
    using wstring = std::wstring;

    // Threading aliases
    using thread = std::thread;
    using mutex = std::mutex;
    using condition_variable = std::condition_variable;
}

// ========== Useful macros ==========
#define SKY_UNUSED(x) ((void)(x))
#define SKY_STRINGIFY(x) #x
#define SKY_TOSTRING(x) SKY_STRINGIFY(x)

#ifdef _DEBUG
    #define SKY_DEBUG_ONLY(x) x
    #define SKY_RELEASE_ONLY(x)
#else
    #define SKY_DEBUG_ONLY(x)
    #define SKY_RELEASE_ONLY(x) x
#endif

// ========== Common forward declarations ==========
namespace sky {
    namespace core {
        class Application;
    }

    namespace driver {
        class DriverImpl;
    }

    namespace game {
        class VGK;

        namespace sdk {
            class UWorld;
            class UObject;
        }

        namespace engine {
            class GameEngine;
        }
    }

    namespace utils {
        class Logger;
        class ConfigManager;
    }
}
