#pragma once

#include <unordered_map>
#include <string>
#include <mutex>
#include <chrono>
#include <optional>

namespace sky::game::sdk {

    class FNameCache {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        explicit FNameCache(std::chrono::milliseconds ttl = std::chrono::minutes(1))
            : m_ttl(ttl) {}

        std::optional<std::string> try_get(std::int32_t key) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_cache.find(key);
            if (it == m_cache.end()) return std::nullopt;
            if (is_expired(it->second.timestamp)) {
                m_cache.erase(it);
                return std::nullopt;
            }
            return it->second.value;
        }

        void put(std::int32_t key, const std::string& value) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache[key] = { value, Clock::now() };
        }

        void invalidate_all() {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache.clear();
        }

        void set_ttl(std::chrono::milliseconds ttl) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ttl = ttl;
        }

        std::size_t size() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_cache.size();
        }

    private:
        struct Entry {
            std::string value;
            TimePoint timestamp;
        };

        bool is_expired(const TimePoint& ts) const {
            return (Clock::now() - ts) > m_ttl;
        }

        std::unordered_map<std::int32_t, Entry> m_cache;
        std::chrono::milliseconds m_ttl;
        mutable std::mutex m_mutex;
    };

} // namespace sky::game::sdk


