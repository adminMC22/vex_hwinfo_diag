#include <include/common.hpp>
#include <include/game/lineups.hpp>
#include <include/utils/logger.hpp>

namespace vex::game::lineups {

    using namespace vex::utils;

    LineupsManager::LineupsManager() {
    }

    bool LineupsManager::load_lineups() {
        try {

            nlohmann::json json_data = nlohmann::json::parse(std::string_view(
                reinterpret_cast<const char*>(lineups_data),
                lineups_data_size
            ));

            if (!json_data.contains("Lineups") || !json_data["Lineups"].is_array()) {
                LOG_ERROR("Invalid JSON format: missing 'Lineups' array");
                return false;
            }

            m_lineups.clear();
            size_t total_count = 0;

            for (const auto& lineup_json : json_data["Lineups"]) {
                try {
                    Lineup lineup;
                    lineup.agent = lineup_json.value("Agent", "");
                    lineup.distance = lineup_json.value("Distance", 0.0);
                    lineup.map = lineup_json.value("Map", "");
                    lineup.position_x = lineup_json.value("PositionX", 0.0);
                    lineup.position_y = lineup_json.value("PositionY", 0.0);
                    lineup.position_z = lineup_json.value("PositionZ", 0.0);
                    lineup.rotation_x = lineup_json.value("RotationX", 0.0);
                    lineup.rotation_y = lineup_json.value("RotationY", 0.0);
                    lineup.rotation_z = lineup_json.value("RotationZ", 0.0);
                    lineup.tips = lineup_json.value("Tips", "");
                    lineup.weapon = lineup_json.value("Weapon", "");

                    m_lineups.push_back(lineup);
                    total_count++;
                }
                catch (const std::exception& e) {
                    LOG_ERROR(std::format("Error parsing lineup entry: {}", e.what()));
                    continue;
                }
            }
            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR(std::format("Exception loading lineups: {}", e.what()));
            return false;
        }
    }

    std::vector<Lineup> LineupsManager::get_lineups(const std::string& agent_name, const std::string& map_name, const std::string& weapon_name) const {
        std::vector<Lineup> filtered_lineups;

        for (const auto& lineup : m_lineups) {
            if (lineup.agent == agent_name &&
                lineup.map == map_name &&
                lineup.weapon == weapon_name) {
                filtered_lineups.push_back(lineup);
            }
        }

        return filtered_lineups;
    }

    std::optional<Lineup> LineupsManager::get_nearest_lineup(
        const vex::game::sdk::FVector& current_position,
        const std::string& agent_name,
        const std::string& map_name,
        const std::string& weapon_name,
        double max_distance
    ) const {
        std::optional<Lineup> nearest_lineup;
        double min_distance = max_distance;

        for (const auto& lineup : m_lineups) {
            if (lineup.agent == agent_name &&
                lineup.map == map_name &&
                lineup.weapon == weapon_name) {

                vex::game::sdk::FVector lineup_position{
                    static_cast<float>(lineup.position_x),
                    static_cast<float>(lineup.position_y),
                    static_cast<float>(lineup.position_z)
                };

                double distance = calculate_distance(current_position, lineup_position);

                if (distance < min_distance) {
                    min_distance = distance;
                    nearest_lineup = lineup;
                }
            }
        }

        if (nearest_lineup.has_value()) {
        }

        return nearest_lineup;
    }

    size_t LineupsManager::get_lineups_count(const std::string& agent_name, const std::string& map_name, const std::string& weapon_name) const {
        size_t count = 0;

        for (const auto& lineup : m_lineups) {
            if (lineup.agent == agent_name &&
                lineup.map == map_name &&
                lineup.weapon == weapon_name) {
                count++;
            }
        }

        return count;
    }

    double LineupsManager::calculate_distance(const vex::game::sdk::FVector& pos1, const vex::game::sdk::FVector& pos2) const {
        double dx = pos1.X - pos2.X;
        double dy = pos1.Y - pos2.Y;
        double dz = pos1.Z - pos2.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Global instance
    std::unique_ptr<LineupsManager> g_lineups = std::make_unique<LineupsManager>();

} // namespace vex::game::lineups