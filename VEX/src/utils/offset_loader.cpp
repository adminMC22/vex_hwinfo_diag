#include <Windows.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include "game/offsets.hpp"
#include "utils/CurlSetup.hpp"

template <typename T>
void readValue(const json& src, T& dest)
{
    if (!src.is_null())
    {
        if (src.is_string())
        {
            std::string value_str = src.get<std::string>();
            if (value_str.substr(0, 2) == "0x")
            {
                dest = static_cast<T>(std::stoul(value_str.substr(2), nullptr, 16));
            }
            else
            {
                dest = static_cast<T>(std::stoul(value_str, nullptr, 10));
            }
        }
        else
        {
            dest = src.get<T>();
        }
    }
}

namespace offsets {

void initialize()
{
    // Offsets streaming without a json file in disk
    json json = setup_curl();

    if (json.is_null()) {
        std::cout << "[Offsets] Using hardcoded defaults." << std::endl;
        return;
    }

    readValue(json["offsets"]["fname_pool"], fname_pool);
    readValue(json["offsets"]["persistent_level"], persistent_level);
    readValue(json["offsets"]["owning_game_instance"], owning_game_instance);
    readValue(json["offsets"]["game_state"], game_state);
    readValue(json["offsets"]["levels"], levels);
    readValue(json["offsets"]["local_players"], local_players);
    readValue(json["offsets"]["actor_array"], actor_array);
    readValue(json["offsets"]["viewport_client"], viewport_client);
    readValue(json["offsets"]["player_controller"], player_controller);
    readValue(json["offsets"]["acknowledged_pawn"], acknowledged_pawn);
    readValue(json["offsets"]["player_camera"], player_camera);
    readValue(json["offsets"]["control_rotation"], control_rotation);
    readValue(json["offsets"]["root_component"], root_component);
    readValue(json["offsets"]["damage_handler"], damage_handler);
    readValue(json["offsets"]["actor_id"], actor_id);
    readValue(json["offsets"]["fname_id"], fname_id);
    readValue(json["offsets"]["dormant"], dormant);
    readValue(json["offsets"]["player_state"], player_state);
    readValue(json["offsets"]["current_mesh"], current_mesh);
    readValue(json["offsets"]["inventory"], inventory);
    readValue(json["offsets"]["outline_component"], outline_component);
    readValue(json["offsets"]["portrait_map"], portrait_map);
    readValue(json["offsets"]["character_map"], character_map);
    readValue(json["offsets"]["current_equippable"], current_equippable);
    readValue(json["offsets"]["local_observer"], local_observer);
    readValue(json["offsets"]["is_visible"], is_visible);
    readValue(json["offsets"]["component_to_world"], component_to_world);
    readValue(json["offsets"]["bone_array"], bone_array);
    readValue(json["offsets"]["bone_count"], bone_count);
    readValue(json["offsets"]["last_submit_time"], last_submit_time);
    readValue(json["offsets"]["last_render_time"], last_render_time);
    readValue(json["offsets"]["outline_mode"], outline_mode);
    readValue(json["offsets"]["attach_children"], attach_children);
    readValue(json["offsets"]["attach_children_count"], attach_children_count);
    readValue(json["offsets"]["team_component"], team_component);
    readValue(json["offsets"]["team_id"], team_id);
    readValue(json["offsets"]["current_health"], current_health);
    readValue(json["offsets"]["max_life"], max_life);
    readValue(json["offsets"]["relative_location"], relative_location);
    readValue(json["offsets"]["relative_rotation"], relative_rotation);
    readValue(json["offsets"]["default_fov"], default_fov);
    readValue(json["offsets"]["camera_cache"], camera_cache);
    readValue(json["offsets"]["pov"], pov);
    readValue(json["offsets"]["location"], location);
    readValue(json["offsets"]["rotation"], rotation);
    readValue(json["offsets"]["current_fov"], current_fov);
    readValue(json["offsets"]["enemy_outline"], enemy_outline);
    readValue(json["offsets"]["bone_array_cache"], bone_array_cache);
    readValue(json["offsets"]["current_defuse_section"], current_defuse_section);
    readValue(json["offsets"]["magazine_ammo"], magazine_ammo);
    readValue(json["offsets"]["auth_resource_amount"], auth_resource_amount);
    readValue(json["offsets"]["max_ammo"], max_ammo);
    readValue(json["offsets"]["spike_timer"], spike_timer);
    readValue(json["offsets"]["my_hud"], my_hud);
    readValue(json["offsets"]["HP"], HP);
    readValue(json["offsets"]["MaxHP"], MaxHP);
    readValue(json["offsets"]["DamageType"], DamageType);
    readValue(json["offsets"]["DamageSections"], DamageSections);
    readValue(json["offsets"]["CurrentEquippableVFXState"], CurrentEquippableVFXState);
    readValue(json["offsets"]["fresnel_intensity"], FresnelIntensity);

    std::cout << "owning_game_instance: 0x" << std::hex << owning_game_instance << std::dec << std::endl;
    std::cout << "game_state: 0x" << std::hex << game_state << std::dec << std::endl;
    std::cout << "levels: 0x" << std::hex << levels << std::dec << std::endl;
    std::cout << "local_players: 0x" << std::hex << local_players << std::dec << std::endl;
    std::cout << "actor_array: 0x" << std::hex << actor_array << std::dec << std::endl;
    std::cout << "viewport_client: 0x" << std::hex << viewport_client << std::dec << std::endl;
    std::cout << "player_controller: 0x" << std::hex << player_controller << std::dec << std::endl;
    std::cout << "acknowledged_pawn: 0x" << std::hex << acknowledged_pawn << std::dec << std::endl;
    std::cout << "player_camera: 0x" << std::hex << player_camera << std::dec << std::endl;
    std::cout << "control_rotation: 0x" << std::hex << control_rotation << std::dec << std::endl;
    std::cout << "root_component: 0x" << std::hex << root_component << std::dec << std::endl;
    std::cout << "damage_handler: 0x" << std::hex << damage_handler << std::dec << std::endl;
    std::cout << "actor_id: 0x" << std::hex << actor_id << std::dec << std::endl;
    std::cout << "fname_id: 0x" << std::hex << fname_id << std::dec << std::endl;
    std::cout << "dormant: 0x" << std::hex << dormant << std::dec << std::endl;
}

} // namespace offsets
