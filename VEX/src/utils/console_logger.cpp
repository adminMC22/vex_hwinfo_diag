#include "../../include/utils/logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <windows.h>
#include <mutex>

namespace sky::utils {

    class ConsoleLogger : public ILogger {
    public:
        explicit ConsoleLogger(LogLevel level = LogLevel::INFO) 
            : m_level(level) {}

        void log(LogLevel level, const std::string& message) override {
            if (level < m_level) return;
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto tm = *std::localtime(&time_t);
            
            std::ostringstream oss;
            oss << std::put_time(&tm, "%H:%M:%S");
            
            std::string level_str = get_level_string(level);
            std::string color_code = get_color_code(level);
            
            std::ostringstream full_message;
            full_message << "[" << oss.str() << "] " 
                        << color_code << "[" << level_str << "]" << "\033[0m" << " " 
                        << message;
            
            std::cout << full_message.str() << std::endl;
            std::cout.flush();
        }

        void log(LogLevel level, const std::wstring& message) override {
            std::string narrow_message(message.begin(), message.end());
            log(level, narrow_message);
        }

        void trace(const std::string& message) override { log(LogLevel::TRACE, message); }
        void debug(const std::string& message) override { log(LogLevel::DEBUG, message); }
        void info(const std::string& message) override { log(LogLevel::INFO, message); }
        void warning(const std::string& message) override { log(LogLevel::WARNING, message); }
        void error(const std::string& message) override { log(LogLevel::ERRO, message); }
        void fatal(const std::string& message) override { log(LogLevel::FATAL_LEVEL, message); }

        void set_level(LogLevel level) override { m_level = level; }
        LogLevel get_level() const override { return m_level; }

    private:
        LogLevel m_level;
        mutable std::mutex m_mutex;

        std::string get_level_string(LogLevel level) const {
            switch (level) {
                case LogLevel::TRACE: return "TRACE";
                case LogLevel::DEBUG: return "DEBUG";
                case LogLevel::INFO: return "INFO";
                case LogLevel::WARNING: return "WARN";
                case LogLevel::ERRO: return "ERROR";
                case LogLevel::FATAL_LEVEL: return "FATAL";
                default: return "UNKNOWN";
            }
        }

        std::string get_color_code(LogLevel level) const {
            switch (level) {
                case LogLevel::TRACE: return "\033[36m";    // Cyan
                case LogLevel::DEBUG: return "\033[34m";    // Blue
                case LogLevel::INFO: return "\033[32m";     // Green
                case LogLevel::WARNING: return "\033[33m";  // Yellow
                case LogLevel::ERRO: return "\033[31m";     // Red
                case LogLevel::FATAL_LEVEL: return "\033[35m";    // Magenta
                default: return "\033[0m"; // Reset
            }
        }
    };

    std::shared_ptr<ILogger> LoggerFactory::create_console_logger(LogLevel level) {
        return std::make_shared<ConsoleLogger>(level);
    }

    std::shared_ptr<ILogger> LoggerFactory::create_file_logger(const std::string& filename, LogLevel level) {
        return create_console_logger(level);
    }

    std::shared_ptr<ILogger> LoggerFactory::create_null_logger() {
        return create_console_logger(LogLevel::FATAL_LEVEL);
    }

    std::shared_ptr<ILogger> g_logger = nullptr;

    void initialize_logger(LogLevel level) {
        g_logger = LoggerFactory::create_console_logger(level);
    }

    void set_global_log_level(LogLevel level) {
        if (g_logger) {
            g_logger->set_level(level);
        }
    }

} // namespace sky::utils
