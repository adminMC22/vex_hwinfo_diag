#include "include/common.hpp"
#include "include/vex.hpp"
#include "src/utils/console_logger.cpp"
#include "src/utils/memory_config.cpp"
#include "src/game/lineups.cpp"
#include "src/core/application.cpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    /*AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stderr);*/

    // Initialize logger after console allocation
    vex::utils::initialize_logger(vex::utils::LogLevel::DEBUG);

    try {
        // Create and initialize application
        auto app = vex::core::create_application();
        if (!app) {
            return -1;
        }

        // Initialize systems
        if (!app->initialize()) {
			LOG_ERROR("Failed to initialize application");
            return -1;
        }

        // Run application
        app->run();
        return 0;
    }
    catch (const std::exception& e) {
        return -1;
    }
    catch (...) {
        return -1;
    }
}