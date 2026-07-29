// main.cpp — Sky GUI entry point with ThrottleStop backend
// Uses imgui overlay (boxes, health bars, skeletons, radar)
#include "include/common.hpp"
#include "include/sky.hpp"
#include "include/game/offsets.hpp"
#include "src/utils/console_logger.cpp"
#include "src/utils/memory_config.cpp"
#include "src/game/lineups.cpp"
#include "src/core/application.cpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Enable console for diagnostics
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stderr);
    SetConsoleTitleA("Sky");

    sky::utils::initialize_logger(sky::utils::LogLevel::DEBUG);

    LOG_INFO("=== Sky Starting (GUI Overlay Mode) ===");

    try {
        // Create the application (drivers + graphics + game engine)
        auto app = sky::core::create_application();
        if (!app) {
            LOG_ERROR("Failed to create application");
            MessageBoxA(nullptr, "Failed to create application.", "Sky", MB_OK | MB_ICONERROR);
            return -1;
        }

        if (!app->initialize()) {
            LOG_ERROR("Application initialization failed");
            MessageBoxA(nullptr,
                "Initialization failed.\n\n"
                "Check console for details.\n"
                "Make sure throttlestop.sys is next to this EXE.\n"
                "Run as Administrator.",
                "Sky", MB_OK | MB_ICONERROR);
            return -1;
        }

        LOG_INFO("Application initialized — entering main loop");
        app->run();

        // Cleanup
        LOG_INFO("Sky shutting down");
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("Exception: ") + e.what());
        MessageBoxA(nullptr, e.what(), "Sky Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (...) {
        LOG_ERROR("Unknown exception");
        return -1;
    }
}
