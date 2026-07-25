#pragma once

#include "ue_object.hpp"
#include "aplayer_controller.hpp"
#include <vector>

namespace sky::game::sdk {

    /**
     * @brief Class ULocalPlayer - Represents a local player in UE5
     */
    class ULocalPlayer : public UObject {
    public:
        ULocalPlayer() : UObject() {}

        // Constructor using global DriverContext
        explicit ULocalPlayer(uintptr_t base_address)
            : UObject(base_address) {
        }

        APlayerController get_player_controller() const {
            if (!is_valid()) return {};
            return read<APlayerController>(offsets::PlayerController);
        }

        // Debug
        void print_debug() const;
    };

} // namespace sky::game::sdk