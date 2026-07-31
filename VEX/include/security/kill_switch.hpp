#pragma once

/**
 * kill_switch.hpp — Emergency kill switch
 *
 * F10 = close the device handle, wipe logs, exit.
 * No .sys files to delete, no services to stop.
 */

#include <thread>
#include <atomic>
#include <functional>
#include <Windows.h>
#include "driver/driver_context.hpp"
#include "utils/logger.hpp"

namespace sky::security {

    // KillSwitch uses sky::driver::g_hwinfo_device from driver_context.hpp

    struct KillSwitchConfig {
        int panic_key = VK_F10;           // Panic hotkey
        bool auto_wipe_logs = true;       // Clear cheat logs
        int heartbeat_ms = 500;           // Check interval
        bool enable_vgk_monitor = true;   // Monitor for VGK scanning
        bool auto_clean_driver = true;    // Close handle on panic
    };

    class KillSwitch {
    public:
        KillSwitch() : m_armed(false), m_triggered(false) {}

        ~KillSwitch() { disarm(); }

        void arm(const KillSwitchConfig& cfg = KillSwitchConfig{}) {
            if (m_armed.exchange(true)) return;
            m_cfg = cfg;
            m_triggered.store(false);

            m_thread = std::thread([this] {
                LOG_INFO("[KillSwitch] Armed — panic key: F10");
                while (m_armed.load() && !m_triggered.load()) {
                    // Check 1: Panic hotkey
                    if (GetAsyncKeyState(m_cfg.panic_key) & 0x8000) {
                        LOG_WARNING("[KillSwitch] PANIC HOTKEY TRIGGERED");
                        trigger();
                        return;
                    }

                    // Check 2: VGK scanning (optional)
                    if (m_cfg.enable_vgk_monitor) {
                        if (detect_vgk_scanning()) {
                            LOG_WARNING("[KillSwitch] VGK scanning detected — triggering panic");
                            trigger();
                            return;
                        }
                    }

                    Sleep(m_cfg.heartbeat_ms);
                }
            });
            m_thread.detach();
        }

        void disarm() {
            m_armed.store(false);
            // Thread will exit on next loop check
        }

        bool is_triggered() const { return m_triggered.load(); }

        // Register external cleanup functions
        using CleanupFn = std::function<void()>;
        void add_cleanup(CleanupFn fn) {
            // In a real implementation, this would store callbacks
        }

        static void trigger() {
            LOG_WARNING("[KillSwitch] === EMERGENCY PANIC ===");

            // 1. Close device handle
            if (sky::driver::g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO("[KillSwitch] Closing device handle...");
                CloseHandle(sky::driver::g_hwinfo_device);
                sky::driver::g_hwinfo_device = INVALID_HANDLE_VALUE;
            }

            // 2. Wipe logs
            LOG_INFO("[KillSwitch] Wiping logs...");
            WipeLogs();

            // 3. Exit cleanly
            Sleep(100);
            ExitProcess(0);
        }

    private:
        std::atomic<bool> m_armed;
        std::atomic<bool> m_triggered;
        std::thread m_thread;
        KillSwitchConfig m_cfg;

        // Detect VGK scanning by checking if our process handle
        // permissions have been restricted. If OpenProcess returns
        // ACCESS_DENIED on our own PID, something is intercepting.
        // Note: This is a heuristic — it may not fire in all cases.
        // A more robust check would monitor for handle duplication
        // or thread injection signals, but those require kernel support.
        static bool detect_vgk_scanning() {
            DWORD pid = GetCurrentProcessId();
            HANDLE test = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, pid);
            if (test) {
                CloseHandle(test);
                // Check if we can also get VM_READ — if not, something
                // is restricting us
                HANDLE test2 = OpenProcess(PROCESS_VM_READ, FALSE, pid);
                if (test2) {
                    CloseHandle(test2);
                    return false; // Normal — full access to self
                }
                // Could query limited info but not VM_READ — suspicious
                return true;
            }
            // Can't even query our own process — very suspicious
            return true;
        }

        static void WipeLogs() {
            // Generic log filenames — avoid cheat-specific names
            char temp_dir[MAX_PATH + 1] = { 0 };
            if (GetTempPathA(MAX_PATH, temp_dir) > 0) {
                std::string base(temp_dir);
                DeleteFileA((base + "app.log").c_str());
                DeleteFileA((base + "debug.log").c_str());
            }
            SetConsoleTitleA("");
        }
    };

    // Global kill switch instance
    inline KillSwitch g_killSwitch;

} // namespace sky::security
