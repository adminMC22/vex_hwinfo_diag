#pragma once

#include <include/common.hpp>
#include <include/game/sdk/sdk.hpp>
#include <include/game/lineups_data.hpp>

namespace vex::game::lineups {

    /**
     * @brief Structure representing a game lineup
     */
    struct Lineup {
        std::string agent;
        double distance;
        std::string map;
        double position_x;
        double position_y;
        double position_z;
        double rotation_x;
        double rotation_y;
        double rotation_z;
        std::string tips;
        std::string weapon;
    };

    /**
     * @brief Class responsible for managing lineups for any agent
     */
    class LineupsManager {
    public:
        LineupsManager();
        ~LineupsManager() = default;

        /**
         * @brief Load lineups from JSON data
         * @return true if loaded successfully, false otherwise
         */
        bool load_lineups();

        /**
         * @brief Find lineups for a specific agent with specific weapon on a map
         * @param agent_name Agent name (e.g., "Sova", "Kay/O")
         * @param map_name Map name
         * @param weapon_name Weapon name to filter
         * @return Vector with found lineups
         */
        std::vector<Lineup> get_lineups(const std::string& agent_name, const std::string& map_name, const std::string& weapon_name) const;

        /**
         * @brief Find the nearest lineup from player's current position
         * @param current_position Player's current position
         * @param agent_name Agent name
         * @param map_name Map name
         * @param weapon_name Weapon name
         * @param max_distance Maximum distance to consider the lineup
         * @return Nearest lineup or std::nullopt if not found
         */
        std::optional<Lineup> get_nearest_lineup(
            const vex::game::sdk::FVector& current_position,
            const std::string& agent_name,
            const std::string& map_name,
            const std::string& weapon_name,
            double max_distance = 100.0
        ) const;

        /**
         * @brief Check if there are lineups loaded
         * @return true if there are lineups loaded
         */
        bool has_lineups() const { return !m_lineups.empty(); }

        /**
         * @brief Get total number of loaded lineups
         * @return Number of lineups
         */
        size_t get_lineups_count() const { return m_lineups.size(); }

        /**
         * @brief Get number of lineups for a specific agent, map and weapon
         * @param agent_name Agent name
         * @param map_name Map name
         * @param weapon_name Weapon name
         * @return Number of lineups found
         */
        size_t get_lineups_count(const std::string& agent_name, const std::string& map_name, const std::string& weapon_name) const;

    private:
        std::vector<Lineup> m_lineups;

        /**
         * @brief Calculate distance between two positions
         * @param pos1 First position
         * @param pos2 Second position
         * @return Euclidean distance
         */
        double calculate_distance(const vex::game::sdk::FVector& pos1, const vex::game::sdk::FVector& pos2) const;
    };

    // Global instance of the lineups manager
    extern std::unique_ptr<LineupsManager> g_lineups;

} // namespace vex::game::lineups