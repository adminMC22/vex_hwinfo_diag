#include <include/common.hpp>
#include <include/core/iapplication.hpp>
#include <include/utils/logger.hpp>
#include <include/utils/config.hpp>
#include <include/game/engine/game_engine.hpp>
#include <include/game/lineups.hpp>
#include <include/driver/idriver.hpp>
#include <include/render/render.hpp>
#include <include/security/kill_switch.hpp>
#include <include/game/offsets.hpp>

#pragma comment(lib, "winmm.lib")

namespace sky::core {
    using namespace sky::utils;
	using namespace sky::game::engine;

    /**
     * @brief Main application implementation - Manages UI, D3D and Engine
     */
    class Application : public IApplication {
    public:
        Application() : m_running(false), m_paused(false) {}

        ~Application() override {
            shutdown();
        }

        bool initialize() override {
            try {
                // Initialize configuration
                if (!initialize_configuration()) {
					LOG_ERROR("Failed to initialize configuration");
                    return false;
                }

                // Create and initialize driver
                sky::driver::g_driver = sky::driver::create_driver();
                if (!sky::driver::g_driver) {
                    LOG_ERROR("Failed to create driver instance");
                    MessageBoxA(0, xorstr_("Failed to create driver instance."), xorstr_("Error"), MB_OK | MB_ICONERROR);
                    return false;
                }

                if (!sky::driver::g_driver->setup()) {
                    LOG_WARNING("Failed to setup driver");
                    MessageBoxA(0, xorstr_("Driver setup failed.\n\n"
                        "Make sure the driver is loaded.\n"
                        "Run as Administrator."),
                        xorstr_("Error"), MB_OK | MB_ICONERROR);
                    return false;
                }

                // Initialize game offsets (live streaming from URL)
                offsets::initialize();

                // Arm the kill switch (panic on F10 or VGK scan)
                sky::security::KillSwitchConfig ks_cfg;
                ks_cfg.panic_key = VK_F10;
                ks_cfg.auto_clean_driver = true;
                ks_cfg.auto_wipe_logs = true;
                ks_cfg.enable_vgk_monitor = true;
                sky::security::g_killSwitch.arm(ks_cfg);
                LOG_INFO("[app] Kill switch armed (F10)");

                // Increase timer resolution to allow sleeps < 15.6ms
                //timeBeginPeriod(1);

                if (!initialize_graphics()) {
					LOG_WARNING("Failed to initialize graphics (D3D + ImGui)");
                    return false;
                }

                if (!m_game_engine->initialize()) {
					LOG_ERROR("Failed to initialize GameEngine");
                    return false;
                }

                // Initialize Sova lineups system
                if (!initialize_lineups()) {
                    LOG_WARNING("Failed to initialize Sova lineups system");
                    // Not critical, continue execution
                }

                return true;
            }
            catch (const std::exception& e) {
                LOG_ERROR("Exception during initialization: " + std::string(e.what()));
                return false;
            }
        }

        void run() override {
            if (!m_game_engine) {
				LOG_ERROR("GameEngine not available");
                return;
            }

            m_running = true;

            // Start GameEngine
            m_game_engine->start();

            this->ui_main_loop();
        }

        void shutdown() override {
            stop();

            if (m_game_engine) {
                m_game_engine->shutdown();
                m_game_engine.reset();
            }

            // TODO: Cleanup D3D and UI resources
            cleanup_graphics();

            m_running = false;
        }

        bool is_running() const override {
            return m_running;
        }

        void set_target_process(const std::wstring& process_name) override {
            if (!m_game_engine) {
                LOG_ERROR("GameEngine not available");
                return;
            }
            if (!m_game_engine->set_target_process(process_name)) {
                LOG_ERROR("Failed to attach to process: " + std::string(process_name.begin(), process_name.end()));
            } else {
                LOG_INFO("Successfully attached to process: " + std::string(process_name.begin(), process_name.end()));
            }
        }

        void set_configuration(const std::string& config_name, const std::string& value) override {
            if (sky::utils::g_config) {
                sky::utils::g_config->set_value(config_name, sky::utils::ConfigValue(value));
            }
        }

        void pause() override {
            m_paused = true;
            if (m_game_engine) {
                m_game_engine->pause();
            }
        }

        void resume() override {
            m_paused = false;
            if (m_game_engine) {
                m_game_engine->resume();
            }
        }

        void stop() override {
            m_running = false;
            if (m_game_engine) {
                m_game_engine->stop();
            }
        }

    private:
        sky::game::engine::ActorsData                   m_actors_data;

        // Application state
        bool m_running;
        bool m_paused;

        bool initialize_configuration() {
            try {
                // Default configurations
                sky::utils::g_config->set_value(xorstr_("driver.service_name"), sky::utils::ConfigValue(xorstr_("gdrv_svc")));
                sky::utils::g_config->set_value(xorstr_("driver.device_path_template"), sky::utils::ConfigValue(xorstr_("\\\\.\\{computer_name}")));
                sky::utils::g_config->set_value(xorstr_("game.target_process"), sky::utils::ConfigValue(xorstr_("VALORANT-Win64-Shipping.exe")));
                sky::utils::g_config->set_value(xorstr_("logging.level"), sky::utils::ConfigValue(xorstr_("INFO")));
                return true;
            }
            catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize configuration: " + std::string(e.what()));
                return false;
            }
        }

        bool initialize_lineups() {
            try {

                if (!sky::game::lineups::g_lineups->load_lineups()) {
                    LOG_ERROR("Failed to load lineups");
                    return false;
                }

                return true;
            }
            catch (const std::exception& e) {
                LOG_ERROR("Exception initializing lineups: " + std::string(e.what()));
                return false;
            }
        }

        // Methods for graphics initialization (D3D + ImGui)
        bool initialize_graphics() {
            return sky::render::m_render->init();
        }

        void cleanup_graphics() {
            if (sky::render::m_render) {
                sky::render::m_render->shutdown();
                sky::render::m_render.reset();
            }
        }

        void draw_players() {
            auto world_data = m_game_engine->get_world_data();
            auto actors_data = m_game_engine->get_actors_data();

			auto camera_view = world_data.player_camera_manager.get_camera_view();
            for (const auto& actor : actors_data.players_list) {
                auto base = actor.actor.get_base_address();

                auto [bones, skeleton] = m_game_engine->SetupActorSkeleton(actor.actor, actor.mesh, actor.bone_array);
                auto health = actor.damage_handler.GetHealth();
                if (health <= 0.f)
                    continue;

                // Calculate more precise model bounds
                float HeadToNeck = bones[1].Y - bones[0].Y;

                // Expand top to fully include head
                float pTop = bones[0].Y - (HeadToNeck * 1.2f);

                // Use feet as base, but with small margin
                float pBottom = bones[13].Y + (HeadToNeck * 0.1f);

                // Calculate dimensions
                float pHeight = pBottom - pTop;

                // Keep original width
                float pWidth = pHeight / 2.0f; // Original width

                // Find center X based on shoulder average for better centering
                float centerX = (bones[4].X + bones[7].X) / 2.0f; // Left and right shoulders

                if (!var->config.esp.enabled)
                    continue;

                auto color = var->config.esp.visible_check ?
                    (actor.visible ? var->config.esp.visible_color : var->config.esp.not_visible_color) :
                    var->config.esp.visible_color;

                // Validity flags (screen + valid)
                bool hasHead = m_game_engine->inScreen(bones[0]);
                bool hasNeck = m_game_engine->inScreen(bones[1]);
                bool hasShoulders = m_game_engine->inScreen(bones[4]) && m_game_engine->inScreen(bones[7]);
                bool hasFeet = m_game_engine->inScreen(bones[13]);
                bool hasChest = m_game_engine->inScreen(bones[8]); // used only for distance

                // --- Health Bar ---
                if (var->config.esp.health && hasFeet && hasHead && hasNeck && hasShoulders) {
                    float healthBarX = centerX - pWidth / 2.0f;  // Aligned with left side of box
                    float healthBarY = pBottom + 3.0f;           // 3 pixels below box
                    float healthBarWidth = pWidth;               // Same width as box
                    float healthBarHeight = 4.0f;                // Fixed height for horizontal bar

                    // Unique ID for health bar animation (base + offset)
                    ImGuiID health_id = ImHashData(&base, sizeof(base)) + 1;

                    sky::render::m_render->HorizontalHealthBar(
                        healthBarX, healthBarY, healthBarWidth, healthBarHeight,
                        health, 100, health_id
                    );
                }

                // --- Box ---
                if (var->config.esp.box && hasHead && hasNeck && hasFeet && hasShoulders) {
                    ImGuiID box_id = ImHashData(&base, sizeof(base)) + 2;
                    sky::render::m_render->CorneredBox(
                        ImVec2(centerX, pBottom), pWidth, pTop, pBottom,
                        IM_COL32(color[0] * 255.f, color[1] * 255.f, color[2] * 255.f, color[3] * 255.f),
                        1.0f, true, health, box_id, 3.0f
                    );
                }

                // --- Skeleton ---
                if (var->config.esp.skeleton) {
                    for (const auto& skel : skeleton) {
                        if (m_game_engine->inScreen(skel[0]) && m_game_engine->inScreen(skel[1])) {
                            sky::render::m_render->DrawSkeletonLine(
                                ImVec2(skel[0].X, skel[0].Y), ImVec2(skel[1].X, skel[1].Y),
                                IM_COL32(color[0] * 255.f, color[1] * 255.f, color[2] * 255.f, color[3] * 255.f),
                                1.5f,
                                0.08f  // Subtle curvature for natural movement
                            );
                        }
                    }
                }

				// --- Head Circle ---
                if (var->config.esp.head) {
                    if (hasHead) {
                        sky::render::m_render->DrawCircleFilled(
                            ImVec2(bones[0].X, bones[0].Y),
                            3.5f,
                            IM_COL32(color[0] * 255.f, color[1] * 255.f, color[2] * 255.f, color[3] * 255.f),
                            30
                        );
                    }
				}

                // --- Lines ---
                if (var->config.esp.lines && hasFeet && hasShoulders) {
                    ImVec2 feet_center = ImVec2(centerX, pBottom);
                    sky::render::m_render->DrawTracerLine(
                        feet_center,
                        IM_COL32(color[0] * 255.f, color[1] * 255.f, color[2] * 255.f, color[3] * 255.f),
                        1.0f
                    );
                }

                // --- Name ---
                if (var->config.esp.agent_name && !actor.name.empty() && hasFeet && hasShoulders) {
                    // Calculate name position
                    float nameY;
                    if (var->config.esp.health) {
                        // If health bar is active, draw name below it
                        nameY = pBottom + 3.0f + 4.0f + 5.0f;
                    }
                    else {
                        // If health bar is not active, draw in its place
                        nameY = pBottom + 3.0f;
                    }

                    // Prepare name text
                    std::string display_name = actor.name;

                    // Add distance if configured
                    if (var->config.esp.distance && hasChest) {
                        sky::game::sdk::FVector pos = actor.actor.GetBoneWithRotation(actor.mesh, actor.bone_array, 8);
                        auto distance = camera_view.Location.Distance(pos) / 100.0;
                        int rounded_distance = (int)std::round(distance);
                        display_name = std::format("[{}m] {}", rounded_distance, actor.name);
                    }

                    // Draw name (with or without distance) centered
                    sky::render::m_render->DrawPlayerName(
                        ImVec2(centerX, nameY), display_name,
                        IM_COL32(color[0] * 255.f, color[1] * 255.f, color[2] * 255.f, color[3] * 255.f)
                    );
                }
            }
        }

        ImU32 draw_spike_timer_bar(double time_remaining, double defuse_progress, int32_t defuse_section) {
            ImGuiIO& io = ImGui::GetIO();
            ImDrawList* draw = ImGui::GetBackgroundDrawList();

            // Circle settings
            const float circle_radius = 40.0f;
            const float circle_thickness = 6.0f;
            const float max_time = 45.0f; // Spike max time (45 seconds)

            // Position at screen center top
            ImVec2 circle_center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.25f);

            // Calculate percentage of time remaining
            float time_percent = std::max(0.0f, std::min(1.0f, (float)time_remaining / max_time));

            // Calculate color based on time (green to red)
            ImU32 progress_color = IM_COL32(
                (int)(255 * (1.0f - time_percent)), // Red increases as time decreases
                (int)(255 * time_percent),          // Green decreases as time decreases
                0,                                  // No blue
                255                                 // Full opacity
            );

            // Draw background circle (dark gray)
            draw->AddCircle(circle_center, circle_radius, IM_COL32(60, 60, 60, 200), 64, circle_thickness);

            // Draw progress arc (decreasing)
            if (time_percent > 0.0f) {
                const float PI = 3.14159265359f;
                float start_angle = -PI * 0.5f; // Start at top (12 o'clock)
                float end_angle = start_angle + (2.0f * PI * time_percent); // Angle based on time remaining

                // Draw progress arc
                draw->PathClear();
                draw->PathArcTo(circle_center, circle_radius, start_angle, end_angle, 64);
                draw->PathStroke(progress_color, false, circle_thickness);
            }

            // Draw outer defuse circle (with 2 sections)
            const float defuse_radius = circle_radius + 15.0f;
            const float defuse_thickness = 4.0f;
            const float PI = 3.14159265359f;

            // Defuse color (blue)
            ImU32 defuse_color = IM_COL32(100, 150, 255, 255);
            ImU32 defuse_bg_color = IM_COL32(120, 120, 120, 180); // Lighter and more opaque

            // Draw background for 2 sections (with symmetric gaps centered)
            float gap_angle = 0.15f; // Gap between sections
            float section_angle = PI - gap_angle; // Each section occupies 180° - gap

            // Section 1 (top) - centered at top with symmetric gap
            float section1_start = -PI * 0.5f + gap_angle * 0.5f; // -90° + half gap
            float section1_end = section1_start + section_angle; // to 90° - half gap
            draw->PathClear();
            draw->PathArcTo(circle_center, defuse_radius, section1_start, section1_end, 32);
            draw->PathStroke(defuse_bg_color, false, defuse_thickness);

            // Section 2 (bottom) - centered at bottom with symmetric gap
            float section2_start = PI * 0.5f + gap_angle * 0.5f; // 90° + half gap
            float section2_end = section2_start + section_angle; // to 270° - half gap
            draw->PathClear();
            draw->PathArcTo(circle_center, defuse_radius, section2_start, section2_end, 32);
            draw->PathStroke(defuse_bg_color, false, defuse_thickness);

            // Draw defuse progress based on current section
            if (defuse_section >= 0 && defuse_progress > 0.0f) {
                // Convert percentage to 0.0-1.0 value
                float normalized_progress = std::max(0.0f, std::min(100.0f, (float)defuse_progress)) / 100.0f;

                if (defuse_section == 0) {
                    // First section in progress (0-50% of total progress)
                    // Map 0-50% to 0-100% of first section
                    float section_progress = std::min(1.0f, normalized_progress * 2.0f); // 0-50% becomes 0-100%
                    float progress_end = section1_start + section_angle * section_progress;
                    draw->PathClear();
                    draw->PathArcTo(circle_center, defuse_radius, section1_start, progress_end, 32);
                    draw->PathStroke(defuse_color, false, defuse_thickness);
                } else if (defuse_section == 1) {
                    // First section always complete
                    draw->PathClear();
                    draw->PathArcTo(circle_center, defuse_radius, section1_start, section1_end, 32);
                    draw->PathStroke(defuse_color, false, defuse_thickness);

                    // Second section in progress (50-100% of total progress)
                    // Map 50-100% to 0-100% of second section
                    float section_progress = std::max(0.0f, (normalized_progress - 0.5f) * 2.0f); // 50-100% becomes 0-100%
                    float progress_end = section2_start + section_angle * section_progress;
                    draw->PathClear();
                    draw->PathArcTo(circle_center, defuse_radius, section2_start, progress_end, 32);
                    draw->PathStroke(defuse_color, false, defuse_thickness);
                } else if (defuse_section >= 2) {
                    // Both sections complete (defuse finished)
                    draw->PathClear();
                    draw->PathArcTo(circle_center, defuse_radius, section1_start, section1_end, 32);
                    draw->PathStroke(defuse_color, false, defuse_thickness);

                    draw->PathClear();
                    draw->PathArcTo(circle_center, defuse_radius, section2_start, section2_end, 32);
                    draw->PathStroke(defuse_color, false, defuse_thickness);
                }
            }

            // Draw text in center of circle
            std::string timer_text = std::format("{}", (int)time_remaining);
            ImVec2 text_size = ImGui::CalcTextSize(timer_text.c_str());
            ImVec2 text_pos = ImVec2(
                circle_center.x - text_size.x * 0.5f,
                circle_center.y - text_size.y * 0.5f
            );

            // Draw text shadow
            draw->AddText(
                ImVec2(text_pos.x + 1, text_pos.y + 1),
                IM_COL32(0, 0, 0, 255),
                timer_text.c_str()
            );

            // Draw main text
            draw->AddText(
                text_pos,
                IM_COL32(255, 255, 255, 255),
                timer_text.c_str()
            );

            return progress_color; // Return calculated color for spike text
        }

        void draw_lineups() {
            // Demonstrate lineup system usage
            if (!sky::game::lineups::g_lineups->has_lineups()) {
                return;
            }

            auto world_data = m_game_engine->get_world_data();

            // Get current player position
            auto camera_view = world_data.player_camera_manager.get_camera_view();
            sky::game::sdk::FVector player_position = camera_view.Location;

            // Get all lineups for current agent
            auto lineups = sky::game::lineups::g_lineups->get_lineups(world_data.local_player_name, world_data.world.get_name_string(), world_data.acknowledged_pawn.CurrentEquippable().get_name_string());

            // Maps to control vertical spacing of nearby lineups and tips
            std::map<std::pair<int, int>, int> position_offsets; // (screen_x, screen_y) -> offset_count for lineups

            // Structure to store complete occupied tooltip info
            struct OccupiedTooltip {
                ImVec2 position;
                ImVec2 size;
                ImVec2 min_bounds() const { return position; }
                ImVec2 max_bounds() const { return ImVec2(position.x + size.x, position.y + size.y); }
            };
            std::vector<OccupiedTooltip> occupied_tooltips;

            // Check globally if crosshair is near any aimpoint (1.3f distance)
            // Use same method as triggerbot to get screen center
            ImGuiIO& io = ImGui::GetIO();
            bool any_crosshair_near_aimpoint = false;
            float crossX = (float)(sky::render::m_render->Width / 2.0f);
            float crossY = (float)(sky::render::m_render->Height / 2.0f);

            for (const auto& lineup_check : lineups) {
                sky::game::sdk::FVector lineup_pos_check{
                    static_cast<float>(lineup_check.position_x),
                    static_cast<float>(lineup_check.position_y),
                    static_cast<float>(lineup_check.position_z)
                };

                double distance_units_check = player_position.Distance(lineup_pos_check);
                double distance_meters_check = distance_units_check / 100.0;
                if (distance_meters_check > 6.0 || distance_meters_check > 1.5) continue;

                sky::game::sdk::FVector aim_point_check{
                    static_cast<float>(lineup_check.rotation_x),
                    static_cast<float>(lineup_check.rotation_y),
                    static_cast<float>(lineup_check.rotation_z)
                };
                auto aim_screen_pos_check = m_game_engine->ProjectWorldToScreen(aim_point_check);
                if (!m_game_engine->inScreen(aim_screen_pos_check)) continue;

                // Calculate crosshair distance to this specific aimpoint (same as triggerbot)
                float xdist = aim_screen_pos_check.X - crossX;
                float ydist = aim_screen_pos_check.Y - crossY;
                float crosshair_distance_check = sqrtf((xdist * xdist) + (ydist * ydist));

                if (crosshair_distance_check <= 1.1f) {
                    any_crosshair_near_aimpoint = true;
                    break;
                }
            }

            // Draw all nearby lineups (within 6 meters)
            for (const auto& lineup : lineups) {
                // Use same distance calculation method as players
                sky::game::sdk::FVector lineup_pos{
                    static_cast<float>(lineup.position_x),
                    static_cast<float>(lineup.position_y),
                    static_cast<float>(lineup.position_z)
                };

                double distance_units = player_position.Distance(lineup_pos);
                double distance_meters = distance_units / 100.0; // Convert to meters

                // Only show lineups within 10 meters
                if (distance_meters > 6.0) continue;

                // Position where player should stand (position)
                sky::game::sdk::FVector lineup_position{
                    static_cast<float>(lineup.position_x),
                    static_cast<float>(lineup.position_y),
                    static_cast<float>(lineup.position_z)
                };

                // rotation_x, y, z is already where we need to aim (world 3D coordinates)
                sky::game::sdk::FVector aim_point{
                    static_cast<float>(lineup.rotation_x),
                    static_cast<float>(lineup.rotation_y),
                    static_cast<float>(lineup.rotation_z)
                };

                auto lineup_screen_pos = m_game_engine->ProjectWorldToScreen(lineup_position);
                auto aim_screen_pos = m_game_engine->ProjectWorldToScreen(aim_point);

                // Always show when we're close (≤10m), regardless if lineup_screen_pos is visible
                {
                    // Color based on distance (closer = more green, farther = more yellow)
                    float distance_factor = std::min(1.0f, static_cast<float>(distance_meters / 10.0));
                    ImU32 lineup_color = IM_COL32(
                        static_cast<int>(255 * distance_factor),      // Red increases with distance
                        255,                                          // Green always max
                        static_cast<int>(255 * (1.0f - distance_factor)), // Blue decreases with distance
                        255
                    );

                    // Draw 3D circle on ground regardless of position visibility
                    {
                        // Draw 3D circle on ground (green if ≤1.5m, yellow if >1.5m)
                        ImU32 position_circle_color = (distance_meters <= 1.f) ?
                            IM_COL32(0, 255, 0, 180) :    // Green if we're close
                            IM_COL32(255, 255, 0, 180);   // Yellow if we're far

                        ImDrawList* draw = ImGui::GetBackgroundDrawList();

                        // Create 3D circle on ground plane
                        const int num_segments = 32;
                        const float radius_world = 20.0f; // Radius in world units (approximately 0.5 meters)
                        std::vector<ImVec2> circle_points;
                        bool all_points_visible = true;

                        for (int i = 0; i < num_segments; i++) {
                            float angle = (float)i / (float)num_segments * 2.0f * 3.14159265359f;

                            // Calculate 3D position on circle (XY plane, Z fixed on ground)
                            sky::game::sdk::FVector circle_point{
                                static_cast<float>(lineup.position_x + cos(angle) * radius_world),
                                static_cast<float>(lineup.position_y + sin(angle) * radius_world),
                                static_cast<float>(lineup.position_z - 100.0) // On ground
                            };

                            // Project 3D point to 2D screen
                            auto screen_point = m_game_engine->ProjectWorldToScreen(circle_point);

                            if (m_game_engine->inScreen(screen_point)) {
                                circle_points.push_back(ImVec2(screen_point.X, screen_point.Y));
                            }
                            else {
                                all_points_visible = false;
                                break;
                            }
                        }

                        // Draw 3D circle if all points visible
                        if (all_points_visible && circle_points.size() == num_segments) {
                            // Draw only 3D circle outline (green or yellow)
                            ImU32 border_color = (distance_meters <= 1.0f) ?
                                IM_COL32(0, 255, 0, 220) :    // Green if we're close
                                IM_COL32(255, 255, 0, 220);   // Yellow if we're far

                            // Draw circle outline
                            for (int i = 0; i < num_segments; i++) {
                                int next_i = (i + 1) % num_segments;
                                draw->AddLine(
                                    circle_points[i],
                                    circle_points[next_i],
                                    border_color,
                                    2.5f // Thicker line for better visibility
                                );
                            }
                        }
                    }

                    // Only draw position markers if they're visible on screen
                    if (m_game_engine->inScreen(lineup_screen_pos)) {
                        // Calculate key for screen position (round to avoid micro-differences)
                        int screen_x = static_cast<int>(lineup_screen_pos.X / 50) * 50; // Group by 50 pixel blocks
                        int screen_y = static_cast<int>(lineup_screen_pos.Y / 50) * 50;
                        std::pair<int, int> position_key = { screen_x, screen_y };

                        // Get vertical offset for this position
                        int vertical_offset = position_offsets[position_key] * 60; // 60 pixels spacing
                        position_offsets[position_key]++; // Increment for next lineup at same position

                        // Draw detailed lineup info (with offset)
                        std::string lineup_info = std::format(
                            "{}\n{}m",
                            lineup.tips.empty() ? (world_data.local_player_name + " Lineup") : lineup.tips,
                            (int)std::round(distance_meters)
                        );

                        sky::render::m_render->DrawPlayerName(
                            ImVec2(lineup_screen_pos.X, lineup_screen_pos.Y + 10 + vertical_offset),
                            lineup_info,
                            IM_COL32(200, 255, 200, 255) // Light green
                        );
                    }

                    if (distance_meters <= 1.5) {
                        ImDrawList* draw = ImGui::GetBackgroundDrawList();

                        ImVec2 screen_center = ImVec2(crossX, crossY);

                        // Always draw line from crosshair to aim point, even if point is off-screen
                        if (m_game_engine->inScreen(aim_screen_pos)) {
                            ImVec2 aim_screen_pt = ImVec2(aim_screen_pos.X, aim_screen_pos.Y);

                            // Check if crosshair is near this specific aimpoint (same as triggerbot)
                            float xdist = aim_screen_pos.X - crossX;
                            float ydist = aim_screen_pos.Y - crossY;
                            float crosshair_distance = sqrtf((xdist * xdist) + (ydist * ydist));
                            bool crosshair_near_this_aimpoint = crosshair_distance <= 1.3f;

                            if (any_crosshair_near_aimpoint) {
                                // When crosshair is near any aimpoint, hide lines and tooltips
                                // Draw ONLY green circle at aimpoint where crosshair is near
                                if (crosshair_near_this_aimpoint) {
                                    draw->AddCircleFilled(
                                        aim_screen_pt,
                                        1.8f,
                                        IM_COL32(0, 255, 0, 255) // Green when crosshair is near
                                    );
                                }
                            }
                            else {
                                // Normal behavior: draw line, red circle and tooltip
                                draw->AddLine(
                                    screen_center,
                                    aim_screen_pt,
                                    IM_COL32(255, 255, 255, 200), // Aim line
                                    2.f
                                );

                                // Marker at aim point
                                draw->AddCircleFilled(
                                    aim_screen_pt,
                                    1.8f,
                                    IM_COL32(255, 0, 0, 255) // Solid red
                                );

                                // Draw tooltip balloon with tips
                                if (!lineup.tips.empty()) {
                                    // Calculate text size first to determine tooltip dimensions
                                    ImVec2 text_size = ImGui::CalcTextSize(lineup.tips.c_str());
                                    float padding = 8.0f;
                                    float tooltip_width = text_size.x + (padding * 2);
                                    float tooltip_height = text_size.y + (padding * 2);

                                    // Lambda function to check if a position collides with existing tooltips
                                    auto check_collision = [&](ImVec2 pos, float width, float height) -> bool {
                                        const float margin_x = 15.0f; // Increased horizontal margin
                                        const float margin_y = 10.0f; // Increased vertical margin

                                        for (const auto& occupied : occupied_tooltips) {
                                            // Check overlap considering actual dimensions and margins
                                            if (pos.x < occupied.max_bounds().x + margin_x &&
                                                pos.x + width + margin_x > occupied.min_bounds().x &&
                                                pos.y < occupied.max_bounds().y + margin_y &&
                                                pos.y + height + margin_y > occupied.min_bounds().y) {
                                                return true; // Collision detected
                                            }
                                        }
                                        return false; // No collision
                                        };

                                    // Try different positions around aim point with more variations
                                    std::vector<ImVec2> candidate_positions;

                                    // Basic positions
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 35, aim_screen_pos.Y - 10)); // Right
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X - tooltip_width - 35, aim_screen_pos.Y - 10)); // Left
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 15, aim_screen_pos.Y - tooltip_height - 25)); // Above
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 15, aim_screen_pos.Y + 25)); // Below

                                    // Nearby diagonal positions
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 30, aim_screen_pos.Y - tooltip_height - 20)); // Top right
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 30, aim_screen_pos.Y + 20)); // Bottom right
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X - tooltip_width - 30, aim_screen_pos.Y - tooltip_height - 20)); // Top left
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X - tooltip_width - 30, aim_screen_pos.Y + 20)); // Bottom left

                                    // More distant positions (for edge cases)
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X + 60, aim_screen_pos.Y - 5)); // Further right
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X - tooltip_width - 60, aim_screen_pos.Y - 5)); // Further left
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X, aim_screen_pos.Y - tooltip_height - 40)); // Further up
                                    candidate_positions.push_back(ImVec2(aim_screen_pos.X, aim_screen_pos.Y + 40)); // Further down

                                    // Fallback spiral system: if no position works, create one in spiral
                                    ImVec2 tooltip_pos = candidate_positions[0]; // Default position
                                    bool found_position = false;

                                    for (const auto& candidate : candidate_positions) {
                                        if (!check_collision(candidate, tooltip_width, tooltip_height)) {
                                            tooltip_pos = candidate;
                                            found_position = true;
                                            break;
                                        }
                                    }

                                    // If still no position found, use spiral system
                                    if (!found_position) {
                                        for (int radius = 80; radius <= 200; radius += 20) {
                                            for (int angle = 0; angle < 360; angle += 45) {
                                                float rad = angle * 3.14159f / 180.0f;
                                                ImVec2 spiral_pos = ImVec2(
                                                    aim_screen_pos.X + cos(rad) * radius - tooltip_width * 0.5f,
                                                    aim_screen_pos.Y + sin(rad) * radius - tooltip_height * 0.5f
                                                );

                                                if (!check_collision(spiral_pos, tooltip_width, tooltip_height)) {
                                                    tooltip_pos = spiral_pos;
                                                    found_position = true;
                                                    break;
                                                }
                                            }
                                            if (found_position) break;
                                        }
                                    }

                                    // Register this position as occupied with its dimensions
                                    occupied_tooltips.push_back({
                                        tooltip_pos,
                                        ImVec2(tooltip_width, tooltip_height)
                                        });

                                    // Balloon dimensions
                                    ImVec2 balloon_min = ImVec2(tooltip_pos.x - padding, tooltip_pos.y - padding);
                                    ImVec2 balloon_max = ImVec2(tooltip_pos.x + text_size.x + padding, tooltip_pos.y + text_size.y + padding);

                                    // Draw balloon background (rounded rectangle)
                                    draw->AddRectFilled(
                                        balloon_min, balloon_max,
                                        IM_COL32(0, 0, 0, 200), // Semi-transparent black background
                                        4.0f // Rounded corners
                                    );

                                    // Draw balloon border
                                    draw->AddRect(
                                        balloon_min, balloon_max,
                                        IM_COL32(255, 255, 255, 150), // Semi-transparent white border
                                        4.0f, 0, 1.0f
                                    );

                                    // Draw balloon "tail" pointing to aim point
                                    // Calculate closest point on balloon edge to aim point
                                    ImVec2 balloon_center = ImVec2(
                                        tooltip_pos.x + text_size.x * 0.5f,
                                        tooltip_pos.y + text_size.y * 0.5f
                                    );

                                    // Determine which balloon edge is closest to aim point
                                    ImVec2 tail_start;
                                    if (aim_screen_pos.X < balloon_center.x) {
                                        // Aim point is to the left - tail comes from left edge
                                        tail_start = ImVec2(balloon_min.x, balloon_center.y);
                                    }
                                    else {
                                        // Aim point is to the right - tail comes from right edge
                                        tail_start = ImVec2(balloon_max.x, balloon_center.y);
                                    }

                                    ImVec2 tail_end = ImVec2(aim_screen_pos.X, aim_screen_pos.Y);

                                    // Only draw tail if distance is reasonable (avoid very long tails)
                                    float tail_distance = sqrt(pow(tail_end.x - tail_start.x, 2) + pow(tail_end.y - tail_start.y, 2));
                                    if (tail_distance < 150.0f) { // Max 150 pixels
                                        // Tail triangle
                                        ImVec2 tail_top = ImVec2(tail_start.x, tail_start.y - 3.0f);
                                        ImVec2 tail_bottom = ImVec2(tail_start.x, tail_start.y + 3.0f);

                                        draw->AddTriangleFilled(tail_top, tail_bottom, tail_end, IM_COL32(0, 0, 0, 200));
                                        draw->AddTriangle(tail_top, tail_bottom, tail_end, IM_COL32(255, 255, 255, 150), 1.0f);
                                    }

                                    // Draw tip text
                                    draw->AddText(
                                        tooltip_pos,
                                        IM_COL32(255, 255, 255, 255), // White text
                                        lineup.tips.c_str()
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }

        void draw_skills() {
            auto world_data = m_game_engine->get_world_data();
            auto actors_data = m_game_engine->get_skills_data();

            if (var->config.esp.spike && actors_data.timed_bomb.is_valid()) {
                auto relative_location = actors_data.timed_bomb.RelativeLocation();
                auto position = m_game_engine->ProjectWorldToScreen(relative_location);
                double time_to_explode = sky::driver::g_driver->read<double>(actors_data.timed_bomb.get_base_address() + offsets::SpikeTimer);
				double defuse_time = sky::driver::g_driver->read<double>(actors_data.timed_bomb.get_base_address() + offsets::SpikeDefuseProgress) * 100 / 6.984602;
				int32_t defuse_section = sky::driver::g_driver->read<int32_t>(actors_data.timed_bomb.get_base_address() + offsets::DefuseSection);

                // Draw timer bar in middle of screen and get progress color
                ImU32 timer_color = draw_spike_timer_bar(time_to_explode, defuse_time, defuse_section);

                if (m_game_engine->inScreen(position)) {
					sky::render::m_render->DrawPlayerName(ImVec2(position.X, position.Y), std::format("Spike: {}s", (int)time_to_explode).c_str(), timer_color);
				}
			}

            if (!var->config.esp.skills)
                return;

            auto camera_view = m_game_engine->get_world_data().player_camera_manager.get_camera_view();
            for (const auto& actor : actors_data.skills_list) {
                auto relative_location = actor.skill.RelativeLocation();
                auto position = m_game_engine->ProjectWorldToScreen(relative_location);

                if (m_game_engine->inScreen(position)) {

                    auto color = IM_COL32(var->config.esp.skills_color[0] * 255.f,
                        var->config.esp.skills_color[1] * 255.f,
                        var->config.esp.skills_color[2] * 255.f,
                        var->config.esp.skills_color[3] * 255.f);

                    sky::render::m_render->DrawPlayerName(ImVec2(position.X, position.Y), actor.name.c_str(), color);
                }
            }
        }

        // Main UI/D3D thread
        void ui_main_loop() {
            while (m_running) {
                if (m_paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                try {
                    // Check if game window exists — don't ExitProcess
                    // immediately, just skip rendering if not found.
                    // The game might not be focused or might be loading.
                    static DWORD last_check = 0;
                    DWORD now = GetTickCount();
                    static bool game_found = false;
                    if (now - last_check > 2000) {  // check every 2s
                        last_check = now;
                        HWND hGame = FindWindowA(xorstr_("VALORANTUnrealWindow"), NULL);
                        game_found = (hGame != nullptr);
                    }
                    // Continue rendering regardless — overlay works
                    // even without the game window (for menu interaction)

                    if (sky::render::m_render) {
                        sky::render::m_render->begin();

                        draw_players();
                        draw_skills();
                        draw_lineups();

                        sky::render::m_render->end();
                    }
                }
                catch (const std::exception& e) {
                    LOG_ERROR(std::format("Exception in UI loop: {}", e.what()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                Sleep(3);
            }
        }

        void monitor_loop() {
            while (m_running) {
                try {
                    // Wait for next monitoring cycle
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                catch (const std::exception& e) {
                    LOG_ERROR("Exception in monitor loop: " + std::string(e.what()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    };

    // Factory to create application instances
    std::shared_ptr<IApplication> create_application() {
        return std::make_shared<Application>();
    }

} // namespace sky::core