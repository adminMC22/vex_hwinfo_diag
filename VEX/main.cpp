#include "include/common.hpp"
#include "include/sky.hpp"
#include "include/game/offsets.hpp"
#include "include/web/web_server.hpp"
#include "src/utils/console_logger.cpp"
#include "src/utils/memory_config.cpp"
#include "src/game/lineups.cpp"
#include "src/core/application.cpp"
#include "src/web/web_server.cpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Enable console for diagnostics
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stderr);
    SetConsoleTitleA("Sky - Headless Web Panel");

    sky::utils::initialize_logger(sky::utils::LogLevel::DEBUG);

    LOG_INFO("=== Sky Starting (Headless Web Panel Mode) ===");

    try {
        // Step 1: Create and initialize driver
        sky::driver::g_driver = sky::driver::create_driver();
        if (!sky::driver::g_driver) {
            LOG_ERROR("Failed to create driver instance");
            MessageBoxA(0, "Failed to create driver.", "Sky", MB_OK | MB_ICONERROR);
            return -1;
        }

        if (!sky::driver::g_driver->setup()) {
            LOG_ERROR("Failed to setup driver");
            MessageBoxA(0, "Driver setup failed.\n\nCheck console for details.\nMake sure MSI Afterburner is installed (RTCore64.sys).", "Sky", MB_OK | MB_ICONERROR);
            return -1;
        }
        LOG_INFO("Driver connected");

        // Step 2: Initialize offsets
        sky::game::offsets::initialize();
        LOG_INFO("Offsets initialized");

        // Step 3: Start web server
        if (!sky::web::g_web.start(8080)) {
            LOG_ERROR("Failed to start web server");
            return -1;
        }
        LOG_INFO("Web server running on http://localhost:8080");

        // Step 4: Initialize game engine
        auto game_engine = sky::game::engine::m_game_engine;
        if (!game_engine->initialize()) {
            LOG_ERROR("Failed to initialize game engine");
        } else {
            game_engine->start();
            LOG_INFO("Game engine started");
        }

        // Step 5: Wire web callbacks to config
        sky::web::g_web.on_aimbot_toggle = [](bool val) {
            LOG_INFO(std::string("Aimbot: ") + (val ? "ON" : "OFF"));
        };
        sky::web::g_web.on_triggerbot_toggle = [](bool val) {
            LOG_INFO(std::string("Triggerbot: ") + (val ? "ON" : "OFF"));
        };
        sky::web::g_web.on_esp_toggle = [](bool val) {
            LOG_INFO(std::string("ESP: ") + (val ? "ON" : "OFF"));
        };
        sky::web::g_web.on_radar_toggle = [](bool val) {
            LOG_INFO(std::string("Radar: ") + (val ? "ON" : "OFF"));
        };

        // Step 6: Main loop — feed game data to web clients
        LOG_INFO("=== Sky Running. Open browser to http://localhost:8080 ===");
        LOG_INFO("=== Press F10 in this console to exit ===");

        while (true) {
            // Check for F10 (kill switch)
            if (GetAsyncKeyState(VK_F10) & 0x8000) {
                LOG_INFO("F10 pressed — shutting down");
                break;
            }

            // Get game data and push to web clients
            if (game_engine->is_running()) {
                auto world = game_engine->get_world_data();
                auto actors = game_engine->get_actors_data();

                sky::web::GameSnapshot snap;
                snap.InGame = world.is_valid;

                if (snap.InGame) {
                    auto cam = world.player_camera_manager.get_camera_view();
                    snap.camera_x = cam.Location.X;
                    snap.camera_y = cam.Location.Y;
                    snap.camera_z = cam.Location.Z;
                    snap.fov = cam.FOV;

                    for (const auto& actor : actors.players_list) {
                        auto health = actor.damage_handler.GetHealth();
                        if (health <= 0.f) continue;

                        sky::web::PlayerData pd;
                        pd.name = actor.name;
                        pd.health = health;
                        pd.visible = actor.visible;

                        // World position
                        auto pos = actor.actor.GetBoneWithRotation(actor.mesh, actor.bone_array, 0);
                        pd.pos_x = pos.X;
                        pd.pos_y = pos.Y;
                        pd.pos_z = pos.Z;

                        // Distance
                        pd.distance = cam.Location.Distance(pos) / 100.0f;

                        // Check if enemy (simplified)
                        pd.is_enemy = true; // TODO: proper team check

                        snap.players.push_back(pd);
                    }
                }

                sky::web::g_web.update_snapshot(snap);
            }

            Sleep(16); // ~60fps update rate
        }

        // Cleanup
        game_engine->stop();
        sky::web::g_web.stop();
        sky::driver::g_driver->unload();
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("Exception: ") + e.what());
        return -1;
    }
    catch (...) {
        return -1;
    }
}
