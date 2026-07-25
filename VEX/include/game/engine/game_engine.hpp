#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>

#include <include/game/vgk_system.hpp>
#include <include/game/sdk/ue_object.hpp>
#include <include/game/sdk/uworld.hpp>
#include <include/game/sdk/ulevel.hpp>
#include <include/game/sdk/ugame_instance.hpp>
#include <include/game/sdk/ulocalplayer.hpp>
#include <include/game/sdk/aplayer_camera_manager.hpp>
#include <include/game/sdk/aplayer_controller.hpp>
#include <include/game/sdk/aactor.hpp>
#include <include/driver/idriver.hpp>
#include <include/utils/logger.hpp>
#include <include/render/render.hpp>

namespace vex::game::engine {

    struct CameraCache {
        sdk::FVector LastValidLocation{};
        sdk::FRotator LastValidRotation{};
        float LastValidFOV = 90.0f;
        bool HasValidCache = false;
    } m_cameraCache;

    struct WorldData {
        sdk::UWorld world{0};
        sdk::ULevel level{ 0 };
        sdk::UGameInstance game_instance{ 0 };
		sdk::TArray<sdk::AActor> actors{};
        sdk::ULocalPlayer local_player{ 0 };
        sdk::APlayerController player_controller{ 0 };
		sdk::AActor acknowledged_pawn{ 0 };
        sdk::APlayerCameraManager player_camera_manager{ 0 };
        std::string local_player_name;
        bool is_valid = false;
        std::chrono::steady_clock::time_point last_update;
    };

    struct ActorInfo {
		sdk::AActor                                     actor;
        std::uintptr_t                                  mesh;
        std::uintptr_t                                  bone_array;
        std::string                                     name;
        sdk::ADamageHandler                             damage_handler;
        bool                                            visible;
    };

    struct SkillInfo {
        sdk::UObject    skill;
		std::string     name;
    };

    struct ActorsData {
        std::vector<ActorInfo> players_list;
        std::chrono::steady_clock::time_point last_update;
    };

    struct SkillsData {
        std::vector<SkillInfo> skills_list;
        sdk::UObject timed_bomb{ 0 };
        std::chrono::steady_clock::time_point last_update;
    };

    class GameEngine {
    public:
        GameEngine();
        ~GameEngine();

        bool initialize();
        void shutdown();

        void start();
        void stop();
        void pause();
        void resume();
        bool is_running() const { return m_running.load(); }
        bool is_paused() const { return m_paused.load(); }

        bool set_target_process(const std::wstring& process_name);
        bool set_target_process(uint32_t process_id);

        WorldData get_world_data() const;
        ActorsData get_actors_data() const;
        SkillsData get_skills_data() const;

        void set_world_update_callback(std::function<void(const WorldData&)> callback);
        void set_actors_update_callback(std::function<void(const ActorsData&)> callback);

        std::pair<std::array<sdk::FVector2D, 17>, std::array<std::array<sdk::FVector2D, 2>, 16>> SetupActorSkeleton(sdk::AActor actor, uintptr_t mesh, uintptr_t bone_array);

        std::shared_ptr<VGK> get_vgk_system() const { return m_vgk; }

        sdk::FVector2D ProjectWorldToScreen(sdk::FVector world_location);

        bool inScreen(const vex::game::sdk::FVector2D& bone) {
            return bone.isValid() &&
                bone.X > 0 && bone.X < vex::render::m_render->Width &&
                bone.Y > 0 && bone.Y < vex::render::m_render->Height;
        }

    private:
        std::atomic<bool> m_running{false};
        std::atomic<bool> m_paused{false};
        std::unique_ptr<std::thread> m_world_thread;
        std::unique_ptr<std::thread> m_actors_thread;
		std::unique_ptr<std::thread> m_skills_thread;

        mutable std::mutex m_world_data_mutex;
        mutable std::mutex m_actors_data_mutex;
		mutable std::mutex m_skills_data_mutex;
        WorldData m_world_data;
        ActorsData m_actors_data;
        SkillsData m_skills_data;

        std::function<void(const WorldData&)> m_world_callback;
        std::function<void(const ActorsData&)> m_actors_callback;

        void world_thread_loop();
        void actors_thread_loop();
		void skills_thread_loop();

        uintptr_t resolve_world_address();
        void update_world_data();
        void update_actors_data();
        void update_skills_data();
        bool is_world_data_valid(const WorldData& data) const;

		std::string GetAgentName(std::string name) const;
		std::string GetSkillName(std::string name) const;

        std::atomic<bool> m_needs_refresh{false};
        std::atomic<uintptr_t> m_uworld_offset{0};

        uintptr_t m_last_world_addr{0};
    };

	inline std::shared_ptr<GameEngine> m_game_engine = std::make_shared<GameEngine>();

} // namespace vex::game::engine
