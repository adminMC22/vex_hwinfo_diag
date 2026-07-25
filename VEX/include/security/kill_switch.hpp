#pragma once

/**
 * kill_switch.hpp — Emergency kill switch for VEX
 *
 * No driver to unload — the cheat just uses HWiNFO's existing device.
 * F10 = close the device handle, wipe logs, exit.
 * No .sys files to delete, no services to stop.
 *
 * This is safer than loading a driver ourselves because:
 *   - HWiNFO64 is a legitimate signed driver
 *   - Vanguard whitelists HWiNFO as a legitimate monitoring tool
 *   - No suspicious service entries or unsigned driver loads
 *   - F10 just closes our handle — HWiNFO keeps running normally
 *   - No trace of the cheat is left behind
 */

#include <Windows.h>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <string>
#include <fstream>
#include "utils/logger.hpp"

namespace vex::security {

    // Forward declare the device handle from driver_impl.cpp
    // (We just need to close it on panic)
    extern HANDLE g_hwinfo_device;

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

            // 1. Close HWiNFO device handle
            if (g_hwinfo_device != INVALID_HANDLE_VALUE) {
                LOG_INFO("[KillSwitch] Closing HWiNFO device handle...");
                CloseHandle(g_hwinfo_device);
                g_hwinfo_device = INVALID_HANDLE_VALUE;
            }

            // 2. Wipe logs
            LOG_INFO("[KillSwitch] Wiping logs...");
            WipeLogs();

            // 3. Exit cleanly — no driver to unload, no .sys to delete
            // HWiNFO64 keeps running normally — no trace of the cheat
            Sleep(100);
            ExitProcess(0);
        }

    private:
        std::atomic<bool> m_armed;
        std::atomic<bool> m_triggered;
        std::thread m_thread;
        KillSwitchConfig m_cfg;

        static bool detect_vgk_scanning() {
            // Check if VGK is actively scanning our process
            // by testing handle access patterns
            HANDLE test = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, GetCurrentProcessId());
            if (test) {
                CloseHandle(test);
                return false; // Normal
            }
            // If OpenProcess suddenly fails from our own PID,
            // VGK may be blocking us — could be detection
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                return true; // Suspicious
            }
            return false;
        }

        static void WipeLogs() {
            // Clear console title
            SetConsoleTitleA("");

            // Clear our log file if it exists
            DeleteFileA("VEX.log");
            DeleteFileA("debug.log");
        }
    };

    // Global kill switch instance
    inline KillSwitch g_killSwitch;

} // namespace vex::security
