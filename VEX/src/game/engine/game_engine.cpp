#include <include/common.hpp>
#include <include/game/engine/game_engine.hpp>
#include <include/render/render.hpp>
#include <include/game/offsets.hpp>
#include <include/utils/logger.hpp>
#include <include/game/sdk/fname_cache_context.hpp>

namespace sky::game::engine {
    using namespace sky::utils;
	using namespace sky::render;

    GameEngine::GameEngine() {
    }

    GameEngine::~GameEngine() {
        shutdown();
    }

    bool GameEngine::initialize() {
        // Initialize VGK system (needed for start())
        m_vgk = std::make_shared<VGK>();
        if (!m_vgk) {
            LOG_ERROR("Failed to create VGK system");
            return false;
        }

        LOG_INFO("Game engine initialized");
        return true;
    }

    void GameEngine::shutdown() {
        if (m_running.load()) {
            stop();
        }

        // Wait for threads to finish
        if (m_world_thread && m_world_thread->joinable()) {
            m_world_thread->join();
        }
        if (m_actors_thread && m_actors_thread->joinable()) {
            m_actors_thread->join();
        }

        // Clean up resources
        m_vgk.reset();
    }

    void GameEngine::start() {
        if (m_running.load()) {
            return;
        }

        if (!m_vgk) {
            return;
        }

        m_running.store(true);
        m_paused.store(false);

        // Start world thread
        m_world_thread = std::make_unique<std::thread>([this]() {
            world_thread_loop();
        });

        // Start actors thread
        m_actors_thread = std::make_unique<std::thread>([this]() {
            actors_thread_loop();
        });

		// Start skills thread
		m_skills_thread = std::make_unique<std::thread>([this]() {
			skills_thread_loop();
		});
    }

    void GameEngine::stop() {
        if (!m_running.load()) {
            return;
        }

        m_running.store(false);

        // Wait for threads to finish
        if (m_world_thread && m_world_thread->joinable()) {
            m_world_thread->join();
        }
        if (m_actors_thread && m_actors_thread->joinable()) {
            m_actors_thread->join();
        }
    }

    void GameEngine::pause() {
        m_paused.store(true);
    }

    void GameEngine::resume() {
        m_paused.store(false);
    }

    bool GameEngine::set_target_process(const std::wstring& process_name) {
        if (!sky::driver::g_driver) {
            return false;
        }

        if (!sky::driver::g_driver->attach_process(process_name)) {
            return false;
        }
        return true;
    }

    bool GameEngine::set_target_process(uint32_t process_id) {
        if (!sky::driver::g_driver) {
            return false;
        }

        if (!sky::driver::g_driver->attach_process(process_id)) {
            return false;
        }
        return true;
    }

    WorldData GameEngine::get_world_data() const {
        return m_world_data;
    }

    ActorsData GameEngine::get_actors_data() const {
        if (m_actors_data_mutex.try_lock())
        {
			ActorsData copy = m_actors_data;
            m_actors_data_mutex.unlock();
			return copy;
        }
        return {};
    }

    SkillsData GameEngine::get_skills_data() const {
        if (m_skills_data_mutex.try_lock())
        {
            SkillsData copy = m_skills_data;
            m_skills_data_mutex.unlock();
            return copy;
        }
        return {};
    }

    void GameEngine::set_world_update_callback(std::function<void(const WorldData&)> callback) {
        m_world_callback = std::move(callback);
    }

    void GameEngine::set_actors_update_callback(std::function<void(const ActorsData&)> callback) {
        m_actors_callback = std::move(callback);
    }

    uintptr_t GameEngine::resolve_world_address() {
        auto game_base = sky::driver::g_driver->get_base_address();
        if (!game_base) {
            // Kernel-only auto-attach: walk the EPROCESS list for the game
            // (ImageFileName prefix), read its PEB ImageBaseAddress via its
            // own CR3, MZ/PE-verified. No CreateToolhelp32Snapshot — Vanguard
            // blocks user-mode process/module enumeration on protected
            // processes. Retry every 2s so start order doesn't matter.
            static std::chrono::steady_clock::time_point s_last_attach{};
            static int s_attempts = 0;
            static int s_last_report = 0;
            auto now = std::chrono::steady_clock::now();
            if (now - s_last_attach >= std::chrono::seconds(2)) {
                s_last_attach = now;
                s_attempts++;
                static const auto game_name = xorstr_("VALORANT-Win64");
                sky::game::GameProcessInfo info;
                // 1) Physical-scan attach: scans RAM for the ImageFileName
                //    prefix and reads PID/DTB/PEB at physical offsets. No
                //    kernel-pool VA translation needed — works with the
                //    ThrottleStop backend where the EPROCESS walk fails.
                bool attached = sky::game::find_game_process_phys(info, game_name);
                // 2) Bootstrap a kernel DTB via the PML4 self-map (works on
                //    drivers with only 1-byte reads — the phys-scan bails
                //    NO_BULK there). Once set, ALL kernel VAs (ntoskrnl
                //    export table AND pool EPROCESS entries) translate
                //    through real page tables, so the export lookup and the
                //    EPROCESS walk both work. One-shot per run.
                if (!attached) {
                    static bool s_pml4_tried = false;
                    if (!s_pml4_tried) {
                        s_pml4_tried = true;
                        sky::driver::write_state_log("attach=TRY kernel_pml4_scan");
                        // Primary: REVERSE-WALK — derive the PML4 from the
                        // kernel image's own page-table chain (deterministic;
                        // each step is a known-value scan). Fallback: the
                        // content-matching scan.
                        uintptr_t pml4 = sky::game::find_kernel_pml4_reverse();
                        if (!pml4)
                            pml4 = sky::game::find_kernel_pml4();
                        if (pml4) {
                            sky::driver::g_driver->set_dir_base((void*)pml4);
                            sky::driver::write_state_log("kernel_pml4=0x" + std::format("{:x}", pml4));
                            // After a verified PML4, the EPROCESS walk below
                            // translates pool VAs through real page tables.
                            // Also cache the pml4 as a fallback DTB for
                            // find_process_cr3 (vgk path) if it's ever needed.
                            m_kernel_pml4.store(pml4);
                        } else {
                            sky::driver::write_state_log("kernel_pml4=NOT_FOUND");
                        }
                    }
                    // 3) EPROCESS-list walk (needs the kernel DTB above, or
                    //    a backend with full kernel VA→PA support).
                    attached = sky::game::find_game_process(info, game_name);
                }
                if (attached) {
                    sky::driver::g_driver->set_attached(info.pid, info.base);
                    m_kproc_cr3.store(info.cr3);          // skip re-walk
                    // KEY: the game's real CR3 becomes the DTB. Kernel space
                    // is mapped in every process, so from here on ALL kernel
                    // VAs (vgk.sys data included) translate through the
                    // game's page tables — not just the ntoskrnl image.
                    sky::driver::g_driver->set_dir_base((void*)info.cr3);
                    LOG_INFO("resolve_world: kernel attach OK");
                } else if (s_attempts - s_last_report >= 10) {
                    // Heartbeat every ~20s: proves the scan/walk is alive
                    // while waiting for the game.
                    s_last_report = s_attempts;
                    sky::driver::write_state_log("attach=TRY n=" + std::to_string(s_attempts));
                }
                game_base = sky::driver::g_driver->get_base_address();
            }
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                sky::driver::write_state_log("attach=WAIT base=0");
            }
            if (!game_base) {
                return 0;
            }
            sky::driver::write_state_log("attach=OK base=0x" + std::format("{:x}", game_base) +
                " cr3=EPROCESS 0x" + std::format("{:x}", m_kproc_cr3.load()));
        }

        // VGK state is read AFTER attach: with the game's real CR3 as DTB,
        // kernel VAs (vgk.sys data) translate through the game's page tables,
        // so the shadow-region decrypt works. app.log still shows the full
        // chain for the current run.
        auto pml4 = m_vgk->find_pml4_base();

        // ---- Build the CR3 candidate list: VGK clone first, then the real
        // CR3 from the EPROCESS list (immune to vgk.sys updates). ----
        uintptr_t candidates[2] = { 0, 0 };
        int n_candidates = 0;
        if (sky::game::is_plausible_cr3(pml4.DecryptedClonedCr3)) {
            candidates[n_candidates++] = pml4.DecryptedClonedCr3;
        }
        auto kproc_cr3 = m_kproc_cr3.load();
        if (!kproc_cr3) {
            // EPROCESS walk is read-heavy (~2-3K reads the first time);
            // gate re-attempts to at most once every 3 seconds.
            static std::chrono::steady_clock::time_point s_last_attempt{};
            auto now = std::chrono::steady_clock::now();
            if (now - s_last_attempt >= std::chrono::seconds(3)) {
                s_last_attempt = now;
                kproc_cr3 = sky::game::find_process_cr3(sky::driver::g_driver->get_process_id());
                if (kproc_cr3) {
                    sky::driver::g_driver->set_dir_base((void*)kproc_cr3);
                    if (sky::game::verify_game_pe(game_base)) {
                        m_kproc_cr3.store(kproc_cr3);
                        static bool s_logged = false;
                        if (!s_logged) {
                            s_logged = true;
                            sky::driver::write_state_log("cr3=EPROCESS 0x" + std::format("{:x}", kproc_cr3));
                        }
                    } else {
                        LOG_WARNING("resolve_world: EPROCESS CR3 failed PE verification");
                        kproc_cr3 = 0;
                    }
                }
            }
        }
        if (kproc_cr3) {
            candidates[n_candidates++] = kproc_cr3;
        }
        if (!n_candidates) {
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                sky::driver::write_state_log("cr3=NONE");
            }
            LOG_WARNING("resolve_world: no usable CR3 (VGK clone invalid, EPROCESS walk failed)");
            return 0;
        }

        // ---- Direct GWorld read with structural validation for each CR3. ----
        for (int c = 0; c < n_candidates; c++) {
            sky::driver::g_driver->set_dir_base((void*)candidates[c]);
            auto world = sky::driver::g_driver->read<uintptr_t>(game_base + offsets::GWorld);
            if (!world) continue;
            auto lvl = sky::driver::g_driver->read<uintptr_t>(world + offsets::PersistentLevel);
            if (!lvl || lvl == world) continue;
            auto owning = sky::driver::g_driver->read<uintptr_t>(lvl + offsets::OwningWord);
            if (owning != world) continue;
            m_needs_refresh.store(false);
            static bool s_logged = false;
            if (!s_logged) {
                s_logged = true;
                sky::driver::write_state_log("world_scan=OK (direct) world=0x" + std::format("{:x}", world));
            }
            return world;
        }

        // ---- Scan fallback (needs VGK's PML4Base; not available via EPROCESS). ----
        if (pml4.PML4Base && pml4.DecryptedClonedCr3) {
            sky::driver::g_driver->set_dir_base((void*)pml4.DecryptedClonedCr3);
            auto offset = m_uworld_offset.load();
            if (offset) {
                auto world_ptr = sky::driver::g_driver->read<uintptr_t>(pml4.PML4Base + offset);
                if (world_ptr) {
                    const auto lvl = sky::driver::g_driver->read<uintptr_t>(world_ptr + offsets::PersistentLevel);
                    if (lvl && lvl != world_ptr &&
                        sky::driver::g_driver->read<uintptr_t>(lvl + offsets::OwningWord) == world_ptr) {
                        m_needs_refresh.store(false);
                        return world_ptr;
                    }
                }
                m_uworld_offset.store(0);
            }
            for (int i = 0; i < 0x1000; i += 8) {
                const auto world_ptr = sky::driver::g_driver->read<uintptr_t>(pml4.PML4Base + i);
                if (!world_ptr) continue;
                const auto lvl = sky::driver::g_driver->read<uintptr_t>(world_ptr + offsets::PersistentLevel);
                if (!lvl || lvl == world_ptr) continue;
                const auto owning_world = sky::driver::g_driver->read<uintptr_t>(lvl + offsets::OwningWord);
                if (owning_world == world_ptr) {
                    m_uworld_offset.store(i);
                    m_needs_refresh.store(false);
                    sky::driver::write_state_log("world_scan=OK offset=0x" + std::format("{:x}", i) +
                        " world=0x" + std::format("{:x}", world_ptr));
                    return world_ptr;
                }
            }
        }

        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            sky::driver::write_state_log("world_scan=FAIL cr3=0x" + std::format("{:x}", candidates[0]) +
                " kproc=0x" + std::format("{:x}", kproc_cr3));
        }
        LOG_WARNING("resolve_world: direct read + scan found nothing");
        return 0;
    }

    void GameEngine::world_thread_loop() {
        while (m_running.load()) {
            try {
                update_world_data();
                {
                    auto wd = m_world_data;
                    if (wd.is_valid) {
                        auto cv = wd.player_camera_manager.get_camera_view();
                        LOG_INFO("World: valid=true, camera=(" +
                            std::to_string((int)cv.Location.X) + "," +
                            std::to_string((int)cv.Location.Y) + "," +
                            std::to_string((int)cv.Location.Z) + ")");
                    }
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR(std::format("Exception in world thread: {0}", e.what()));
            }
            // If world data failed, back off to avoid spamming failed scans
            {
                auto snap = m_world_data;
                if (!snap.is_valid || m_needs_refresh.load()) {
                    Sleep(1000);
                } else {
                    Sleep(16);
                }
            }
        }
    }

    void GameEngine::actors_thread_loop() {
        while (m_running.load()) {
            try {
                update_actors_data();
            }
            catch (const std::exception& e) {
                LOG_ERROR(std::format("Exception in actors thread: {}", e.what()));
            }
			Sleep(1);
        }
    }

    void GameEngine::skills_thread_loop() {
        while (m_running.load()) {
            if (m_paused.load()) {
                Sleep(1);
                continue;
            }
            try {
                update_skills_data();
            }
            catch (const std::exception& e) {
                LOG_ERROR(std::format("Exception in skills thread: {}", e.what()));
            }
            Sleep(1);
        }
    }

    void GameEngine::update_world_data() {
        WorldData new_data;
        new_data.last_update = std::chrono::steady_clock::now();

        try {
            auto world_addr = resolve_world_address();
            if (world_addr == 0) {
                return;
            }

            new_data.world = sdk::UWorld(world_addr);
            //printf("name: %s\n", new_data.world.get_name_string().c_str());
            if (m_last_world_addr != world_addr) {
                sky::game::sdk::reset_global_fname_cache();
                m_last_world_addr = world_addr;
            }

            new_data.level = new_data.world.get_persistent_level();
			//printf("Level: %s\n", new_data.level.get_name_string().c_str());

			new_data.game_instance = new_data.world.get_game_instance();
			//printf("GameInstance: %s\n", new_data.game_instance.get_name_string().c_str());
            if (!new_data.game_instance.is_valid())
                return;

			new_data.actors = new_data.level.get_actors();

			// One-shot diagnostic: first successful actor enumeration
			static bool s_actor_count_logged = false;
			if (!s_actor_count_logged && new_data.actors.Num() > 0) {
				s_actor_count_logged = true;
				sky::driver::write_state_log("actors_count=" + std::to_string(new_data.actors.Num()));
			}

            new_data.local_player = new_data.game_instance.get_local_player();
			//printf("LocalPlayer: %s\n", new_data.local_player.get_name_string().c_str());
            if (!new_data.local_player.is_valid())
                return;

            new_data.player_controller = new_data.local_player.get_player_controller();
			//printf("PlayerController: %s\n", new_data.player_controller.get_name_string().c_str());
            if (!new_data.player_controller.is_valid())
				return;

			new_data.player_camera_manager = new_data.player_controller.get_player_camera_manager();
			//printf("PlayerCameraManager: %s\n", new_data.player_camera_manager.get_name_string().c_str());
            if (!new_data.player_camera_manager.is_valid())
				return;

			new_data.acknowledged_pawn = new_data.player_controller.get_acknowledged_pawn();
			//printf("AcknowledgedPawn: %s\n", new_data.acknowledged_pawn.get_name_string().c_str());
			if (!new_data.acknowledged_pawn.is_valid())
				return;

			new_data.local_player_name = GetAgentName(new_data.acknowledged_pawn.get_name_string());

            new_data.is_valid = true;

            {
                std::lock_guard<std::mutex> lock(m_world_data_mutex);
                m_world_data = std::move(new_data);
            }

            // Call callback if defined
            if (m_world_callback && m_world_data.is_valid) {
                m_world_callback(m_world_data);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR(std::format("Exception updating world data: {}", e.what()));
        }
    }

    void GameEngine::update_actors_data() {
        ActorsData updated_data;
        updated_data.last_update = std::chrono::steady_clock::now();

        try {
            // Get world data first
            WorldData world_data = get_world_data();
            if (world_data.is_valid || world_data.level.get_base_address() != 0) {
                // Internal cache by actor base address (persists between ticks in thread)
                static thread_local std::unordered_map<uintptr_t, ActorInfo> players_cache;
                // Set of actors seen in this tick
                static thread_local std::unordered_set<uintptr_t> seen_this_tick;
                seen_this_tick.clear();

                // Thread-local buffer for reconstruction without frequent reallocations
                static thread_local std::vector<ActorInfo> tmp_players;
                tmp_players.clear();
                size_t reserve_count = static_cast<size_t>(world_data.actors.Num());
                tmp_players.reserve(reserve_count);

                for (int i = 0; i < world_data.actors.Num(); i++) {
                    auto actor = world_data.actors[i];
                    if (!actor.is_valid()) continue;
                    if (actor.get_base_address() == world_data.acknowledged_pawn.get_base_address()) continue;

                    const auto base_addr = actor.get_base_address();

                    // Character filter FIRST: agent names only exist on characters.
                    // Gates below (wasAlly) read character-only fields, so they must
                    // not run on world props / projectiles.
                    auto agent_name = GetAgentName(actor.get_name_string());
                    if (agent_name.empty()) continue;
                    if (actor.wasAlly()) continue;

                    // Update only what's needed when already known
                    if (auto it = players_cache.find(base_addr); it != players_cache.end()) {
                        ActorInfo& info = it->second;
                        info.actor = actor;
                        //info.bone_array = actor.BoneArray();
                        info.visible = actor.IsVisible(info.mesh);
                        seen_this_tick.insert(base_addr);
                        continue;
                    }

                    // New actor: collect full data only once
                    auto mesh = actor.Mesh();
                    if (!mesh) continue;

                    auto bone_array = actor.BoneArray();
                    if (!bone_array) continue;

                    ActorInfo actor_info;
                    actor_info.actor = actor;
                    actor_info.mesh = mesh;
                    actor_info.bone_array = bone_array;
                    actor_info.name = agent_name;
                    actor_info.damage_handler = actor.DamageHandler();
                    actor_info.visible = actor.IsVisible(mesh);
                    players_cache.emplace(base_addr, std::move(actor_info));
                    seen_this_tick.insert(base_addr);
                }

                // Remove actors that didn't appear in this tick
                for (auto it = players_cache.begin(); it != players_cache.end(); ) {
                    if (seen_this_tick.find(it->first) == seen_this_tick.end()) {
                        it = players_cache.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Rebuild linear list from cache (maintaining external API)
                tmp_players.reserve(tmp_players.size() + players_cache.size());
                for (const auto& kv : players_cache) {
                    tmp_players.emplace_back(kv.second);
                }
                updated_data.players_list.swap(tmp_players);
            }

            // Prepare data for callback before move
            ActorsData callback_data = updated_data;

            // Update thread-safe data
            {
                if (m_actors_data_mutex.try_lock()) {
                    m_actors_data = std::move(updated_data);
                    m_actors_data_mutex.unlock();
                }
            }

            // Call callback if defined
            if (m_actors_callback) {
                m_actors_callback(callback_data);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR(std::format("Exception updating actors data: {}", e.what()));
        }
    }

    void GameEngine::update_skills_data() {
        SkillsData new_data;
        new_data.last_update = std::chrono::steady_clock::now();

        if (!var->config.esp.skills)
            return;

        try {
            // Get world data first
            WorldData world_data = get_world_data();
            if (world_data.is_valid || world_data.level.get_base_address() != 0) {
                // Internal cache by skill base address (persists between ticks in thread)
                static thread_local std::unordered_map<uintptr_t, SkillInfo> skills_cache;
                // Set of skills seen in this tick
                static thread_local std::unordered_set<uintptr_t> seen_this_tick;
                seen_this_tick.clear();

                // Thread-local buffer for reconstruction
                static thread_local std::vector<SkillInfo> tmp_skills;
                tmp_skills.clear();
                tmp_skills.reserve(static_cast<size_t>(world_data.actors.Num()));

                for (int i = 0; i < world_data.actors.Num(); i++) {
                    auto actor = world_data.actors[i];
                    if (!actor.is_valid()) continue;
                    if (actor.get_base_address() == world_data.acknowledged_pawn.get_base_address()) continue;

                    const auto base_addr = actor.get_base_address();
                    auto actor_name = actor.get_name_string();

                    // Timed bomb is handled separately
                    if (!actor_name.empty() && actor_name == "TimedBomb_C") {
                        new_data.timed_bomb = actor;
                        continue;
                    }

                    // Already in cache: just update object and mark as seen
                    if (auto it = skills_cache.find(base_addr); it != skills_cache.end()) {
                        it->second.skill = actor;
                        seen_this_tick.insert(base_addr);
                        continue;
                    }

                    // New: resolve name only once
                    auto name = GetSkillName(actor_name);
                    if (name.empty()) continue;

                    SkillInfo skill_info;
                    skill_info.skill = actor;
                    skill_info.name = name;

                    skills_cache.emplace(base_addr, std::move(skill_info));
                    seen_this_tick.insert(base_addr);
                }

                // Remove skills that didn't appear in this tick
                for (auto it = skills_cache.begin(); it != skills_cache.end(); ) {
                    if (seen_this_tick.find(it->first) == seen_this_tick.end()) {
                        it = skills_cache.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Rebuild linear list from cache
                tmp_skills.reserve(tmp_skills.size() + skills_cache.size());
                for (const auto& kv : skills_cache) {
                    tmp_skills.emplace_back(kv.second);
                }
                new_data.skills_list.swap(tmp_skills);
            }

            // Update thread-safe data
            {
                if (m_skills_data_mutex.try_lock()) {
                    m_skills_data = std::move(new_data);
                    m_skills_data_mutex.unlock();
                }
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR(std::format("Exception updating actors data: {}", e.what()));
        }
    }

    bool GameEngine::is_world_data_valid(const WorldData& data) const {
        return data.is_valid &&
               data.world.get_base_address() != 0 &&
               data.level.get_base_address() != 0;
    }

    std::pair<std::array<sdk::FVector2D, 17>, std::array<std::array<sdk::FVector2D, 2>, 16>> GameEngine::SetupActorSkeleton(sdk::AActor actor, uintptr_t mesh, uintptr_t bone_array)
    {
        auto bone_count = actor.BoneCount(bone_array);

        struct BoneMap {
            int head, neck, chest;
            int lUpperArm, lForeArm, lHand;
            int rUpperArm, rForeArm, rHand;
            int stomach, pelvis;
            int lThigh, lKnee, lFoot;
            int rThigh, rKnee, rFoot;
        };

        static const std::unordered_map<int, BoneMap> boneMaps = {
            { 104, {8,21,6, 23,24,25, 49,50,51, 4,3, 77,78,80, 84,85,87} },
            { 101, {8,21,6, 23,24,25, 49,50,51, 4,3, 75,76,78, 82,83,85} },
            { 103, {8,9,6, 33,30,32, 58,55,57, 4,3, 63,65,69, 77,79,83} }
        };

        auto it = boneMaps.find(bone_count);
        if (it == boneMaps.end())
            return {};

        const BoneMap& map = it->second;

        std::array<sdk::FVector2D, 17> bones{};
        std::array<std::array<sdk::FVector2D, 2>, 16> skeleton{};

        std::array<int, 17> boneIdx = {
            map.head, map.neck, map.chest,
            map.lUpperArm, map.lForeArm, map.lHand,
            map.rUpperArm, map.rForeArm, map.rHand,
            map.stomach, map.pelvis,
            map.lThigh, map.lKnee, map.lFoot,
            map.rThigh, map.rKnee, map.rFoot
        };

        // OPTIMIZED METHOD - read all bones at once
        std::vector<int32_t> bone_indices(boneIdx.begin(), boneIdx.end());
        auto bone_positions = actor.GetMultipleBonesWithRotation(mesh, bone_array, bone_indices);

        for (size_t i = 0; i < bone_positions.size() && i < bones.size(); ++i) {
            bones[i] = ProjectWorldToScreen(bone_positions[i]);
        }

        // OLD METHOD (commented for comparison)
        /*
        for (size_t i = 0; i < boneIdx.size(); ++i) {
            auto pos = actor.GetBoneWithRotation(mesh, bone_array, boneIdx[i]);
            bones[i] = ProjectWorldToScreen(pos);
        }
        */

        static const std::array<std::pair<int, int>, 16> skeletonLinks = { {
            {0,1}, {1,2}, {2,3}, {3,4}, {4,5},
            {2,6}, {6,7}, {7,8},
            {2,9}, {9,10},
            {10,11}, {11,12}, {12,13},
            {10,14}, {14,15}, {15,16}
        } };

        for (size_t i = 0; i < skeletonLinks.size(); ++i) {
            skeleton[i][0] = bones[skeletonLinks[i].first];
            skeleton[i][1] = bones[skeletonLinks[i].second];
        }

        return { bones, skeleton };
    }

    sdk::FVector2D GameEngine::ProjectWorldToScreen(sdk::FVector WorldLocation) {
		sdk::FVector2D Screenlocation{};

		auto world_data = get_world_data();
        auto camera_view = world_data.player_camera_manager.get_camera_view();

		auto CameraLocation = camera_view.Location;
        auto CameraRotation = camera_view.Rotation;
        auto CameraFOV = camera_view.FOV;

        bool isLocationValid = !(CameraLocation.X == 0.f && CameraLocation.Y == 0.f && CameraLocation.Z == 0.f);
        bool isRotationValid = !(CameraRotation.Pitch == 0.f && CameraRotation.Yaw == 0.f && CameraRotation.Roll == 0.f);
        bool isFOVValid = CameraFOV > 0.0f;

        if (isLocationValid && isRotationValid && isFOVValid) {
            m_cameraCache.LastValidLocation = CameraLocation;
            m_cameraCache.LastValidRotation = CameraRotation;
            m_cameraCache.LastValidFOV = CameraFOV;
            m_cameraCache.HasValidCache = true;
        }
        else if (m_cameraCache.HasValidCache) {
            if (!isLocationValid) {
                CameraLocation = m_cameraCache.LastValidLocation;
            }
            if (!isRotationValid) {
                CameraRotation = m_cameraCache.LastValidRotation;
            }
            if (!isFOVValid) {
                CameraFOV = m_cameraCache.LastValidFOV;
            }
        }
        else {
            return {};
        }

        auto matrix = sdk::Math::toMatrix(CameraRotation, {});

        sdk::FVector vAxisX = sdk::FVector(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]);
        sdk::FVector vAxisY = sdk::FVector(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]);
        sdk::FVector vAxisZ = sdk::FVector(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]);

        sdk::FVector vDelta = WorldLocation - CameraLocation;
        sdk::FVector vTransformed = sdk::FVector(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

        if (vTransformed.Z < 1.0)
            vTransformed.Z = 1.0;

        if (CameraFOV <= 0.0 || CameraFOV > 180.0) {
            return {};
        }

        Screenlocation.X = (double)(m_render->Width / 2.f) + vTransformed.X * ((double)(m_render->Width / 2.f) / tanf(CameraFOV * (float)sdk::Constants::HALF_DEG_TO_RAD)) / vTransformed.Z;
        Screenlocation.Y = (double)(m_render->Height / 2.f) - vTransformed.Y * ((double)(m_render->Width / 2.f) / tanf(CameraFOV * (float)sdk::Constants::HALF_DEG_TO_RAD)) / vTransformed.Z;
        if(Screenlocation.X < 0 || Screenlocation.X > m_render->Width ||
            Screenlocation.Y < 0 || Screenlocation.Y > m_render->Height) {
            return {};
		}

        return Screenlocation;
    }

    std::string GameEngine::GetAgentName(std::string name) const {
        if (name.empty())
            return xorstr_("");

        static std::unordered_map<std::string, std::string> patterns = {
        { xorstr_("Sequoia_PC_C"),                              xorstr_("Iso") },
        { xorstr_("Pawn_TrainingBot_DanceHall_Easy_Attacker"),  xorstr_("Bot Training") },
        { xorstr_("TrainingBot_PC_C"),                          xorstr_("Bot") },
        { xorstr_("Rift_PC_C"),                                 xorstr_("Astra") },
        { xorstr_("Rift_TargetingForm_PC_C"),                   xorstr_("Astra") },
        { xorstr_("Breach_PC_C"),                               xorstr_("Breach") },
        { xorstr_("Sarge_PC_C"),                                xorstr_("Brimstone") },
        { xorstr_("Deadeye_PC_C"),                              xorstr_("Chamber") },
        { xorstr_("Gumshoe_PC_C"),                              xorstr_("Cypher") },
        { xorstr_("Wushu_PC_C"),                                xorstr_("Jett") },
        { xorstr_("Grenadier_PC_C"),                            xorstr_("Kay/O") },
        { xorstr_("Killjoy_PC_C"),                              xorstr_("Killjoy") },
        { xorstr_("Sprinter_PC_C"),                             xorstr_("Neon") },
        { xorstr_("Wraith_PC_C"),                               xorstr_("Omen") },
        { xorstr_("Phoenix_PC_C"),                              xorstr_("Phoenix") },
        { xorstr_("Clay_PC_C"),                                 xorstr_("Raze") },
        { xorstr_("Vampire_PC_C"),                              xorstr_("Reyna") },
        { xorstr_("Thorne_PC_C"),                               xorstr_("Sage") },
        { xorstr_("Guide_PC_C"),                                xorstr_("Skye") },
        { xorstr_("BountyHunter_PC_C"),                         xorstr_("Fade") },
        { xorstr_("Hunter_PC_C"),                               xorstr_("Sova") },
        { xorstr_("Pandemic_PC_C"),                             xorstr_("Viper") },
        { xorstr_("Stealth_PC_C"),                              xorstr_("Yoru") },
        { xorstr_("Mage_PC_C"),                                 xorstr_("Harbor") },
        { xorstr_("AggroBot_PC_C"),                             xorstr_("Gekko") },
        { xorstr_("Cable_PC_C"),                                xorstr_("Deadlock") },
        { xorstr_("Smonk_PC_C"),                                xorstr_("Clove") },
        { xorstr_("Nox_PC_C"),                                  xorstr_("Vyse") },
        { xorstr_("Cashew_PC_C"),                               xorstr_("Tejo") },
        { xorstr_("Terra_PC_C"),                                xorstr_("Waylay") }
        };

        return patterns[name];
    }

std::string GameEngine::GetSkillName(std::string name) const {
        if (name.empty())
            return xorstr_("");

        static std::unordered_map<std::string, std::string> skill_patterns = {
        // Phoenix
        { xorstr_("Patch_Phoenix_MolotovFire_C"),                       xorstr_("Hot Hands") },
        { xorstr_("Projectile_Phoenix_E_FlareCurve_Synced_C"),          xorstr_("Curve Ball") },
        { xorstr_("GameObject_Phoenix_X_ResTarget_Production_C"),       xorstr_("Phoenix Ult") },

        // Astra
        { xorstr_("GameObject_Rift_X_Markers_C"),                       xorstr_("Astra Star") },
        { xorstr_("GameObject_Rift_4_BlackHole_C"),                     xorstr_("Gravity Well") },
        { xorstr_("Ability_Rift_Q_FlashBurst_C"),                       xorstr_("Nova Pulse") },
        { xorstr_("Ability_Rift_E_TransformRift_Smoke_C"),              xorstr_("Nebula") },

        // Breach
        { xorstr_("Projectile_Breach_Q_ThroughWalls_Flash_C"),         xorstr_("Flashpoint") },
        { xorstr_("Ability_Breach_4_FusionBlast_C"),                   xorstr_("Aftershock") },
        { xorstr_("GameObject_Breach_E_SweetSpotFissure_C"),           xorstr_("Fault Line") },
        { xorstr_("GameObject_Breach_X_Shockwave_C"),                  xorstr_("Rolling Thunder") },

        // Brimstone
        { xorstr_("GameObject_Sarge_E_SpeedStim_C"),                   xorstr_("Stim Beacon") },
        { xorstr_("Ability_Sarge_Q_Molotov_Production_C"),             xorstr_("Incendiary") },
        { xorstr_("Ability_Sarge_4_MapTargetSmoke_Production_C"),      xorstr_("Sky Smoke") },
        { xorstr_("GameObject_Sarge_X_OrbitalStrike_Production_C"),    xorstr_("Orbital Strike") },

        // Chamber
        { xorstr_("GameObect_Deadeye_E_Trap_C"),                         xorstr_("Trademark") },
        { xorstr_("GameObject_Deadeye_E_Teleporter_Tether_C"),         xorstr_("Rendezvous") },

        // Clove
        { xorstr_("Projectile_Smonk_DecayNade_C"),                     xorstr_("Meddle Nade") },
        { xorstr_("GameObject_Smonk_NewSmoke_C"),                      xorstr_("Ruse Smoke") },

        // Cypher
        { xorstr_("GameObject_Gumshoe_E_TripWire_C"),                  xorstr_("Tripwire 1") },
        { xorstr_("GameObject_Gumshoe_E_TripWire_SecondWire_C"),       xorstr_("Tripwire 2") },
        { xorstr_("Projectile_Gumshoe_4_CageTrap_C"),                  xorstr_("Cyber Cage") },
        { xorstr_("Ability_Gumshoe_Q_Camera_C"),                       xorstr_("Spycam") },

        // Deadlock
        { xorstr_("GameObject_StealthingTrap_SoundSensor_C"),          xorstr_("Sonic Sensor") },
        { xorstr_("Projectile_NetToss_C"),                             xorstr_("Gravnet") },

        // Fade
        { xorstr_("Projectile_E_BountyHunter_Divebomb_C"),             xorstr_("Haunt Projectile") },
        { xorstr_("GameObject_BountyHunter_E_LoSReveal_Source_Reactivate_C"), xorstr_("Haunt") },
        { xorstr_("Projectile_Q_BountyHunter_TetherGrenade_SphereExpansion_C"), xorstr_("Seize Projectile") },

        // Gekko
        { xorstr_("Projectile_E_Aggrobot_DiscTurret_PowerWave_C"),     xorstr_("Dizzy") },
        { xorstr_("Projectile_Aggrobot_C_ExplodeyPatch_C"),            xorstr_("Mosh Pit") },

        // Harbor
        { xorstr_("Zone_Mage_Q_SphereShield_C"),                       xorstr_("Cove") },

        // Jett
        { xorstr_("Projectile_Wushu_4_Smoke_C"),                       xorstr_("Cloudburst Projectile") },
        { xorstr_("GameObject_Wushu_4_SmokeZone_C"),                   xorstr_("Cloudburst") },

        // Kayo
        { xorstr_("Projectile_Grenadier_E_SuppressionBlade_C"),        xorstr_("Zero Point Blade") },
        { xorstr_("Ability_E_Grenadier_EMPKnife_C"),                   xorstr_("Zero Point Blade") },
        { xorstr_("Ability_Grenadier_C_Flash_C"),                      xorstr_("Flash Drive") },
        { xorstr_("Ability_Grenadier_Q_BasicSemtex_C"),                xorstr_("Fragment") },

        // Killjoy
        { xorstr_("Projectile_Killjoy_4_RemoteBees_MultiDetonate_C"),  xorstr_("Nanoswarm") },
        { xorstr_("Ability_Killjoy_Q_Alarmbot_C"),                     xorstr_("Alarmbot") },
        { xorstr_("Pawn_Killjoy_E_Turret_C"),                          xorstr_("Turret") },

        // Omen
        { xorstr_("Zone_Wraith_4_Smoke_C"),                            xorstr_("Dark Cover") },
        { xorstr_("Projectile_Wraith_Q_NearsightMissile_C"),           xorstr_("Paranoia") },

        // Reyna
        { xorstr_("Projectile_Vampire_4_NearsightAoE_C"),              xorstr_("Leer") },

        // Sova
        { xorstr_("FXC_Hunter_E_DronePossessed_C"),                        xorstr_("Owl Drone") },
        { xorstr_("Projectile_Hunter_4_ExplosiveBolt_PrototypeBalance_C"), xorstr_("Shock Bolt") },
        { xorstr_("Projectile_Hunter_Q_RevealBolt_C"),                 xorstr_("Recon Bolt") },

        // Tejo
        { xorstr_("Projectile_Cashew_Q_ShellShockGrenade_C"),          xorstr_("Special Delivery") },

        // Vyse
        { xorstr_("GameObject_Nox_BarbedWire_C"),                      xorstr_("Barbwire") },
        { xorstr_("GameObject_Nox_WallTrap_C"),                        xorstr_("Wall Trap") },
        { xorstr_("GameObject_Nox_StealthingTrap_Flash_2_C"),          xorstr_("Arc Rose") }
        };

        return skill_patterns[name]; // Returns empty string if not found
    }

} // namespace sky::game::engine