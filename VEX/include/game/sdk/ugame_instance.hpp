#pragma once

#include "ue_object.hpp"
#include "ulocalplayer.hpp"
#include <vector>

namespace sky::game::sdk {

    /**
     * @brief Class UGameInstance - Represents the game instance in UE5  
     */
    class UGameInstance : public UObject {
    public:
        UGameInstance() : UObject() {}

        // Constructor using global DriverContext
        explicit UGameInstance(uintptr_t base_address)
            : UObject(base_address) {}

        // Specific UGameInstance methods
        ULocalPlayer get_local_player() const {
            if (!is_valid()) return {};

            const auto local_player_addr = read<uintptr_t>(offsets::LocalPlayers);
            if (local_player_addr) {
                return sky::driver::g_driver->read<ULocalPlayer>(local_player_addr);
            }

            return {};
        }

        // Debug
        void print_debug() const;
    };

} // namespace sky::game::sdk

