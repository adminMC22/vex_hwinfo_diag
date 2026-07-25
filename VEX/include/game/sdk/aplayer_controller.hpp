#pragma once

#include "ue_object.hpp"
#include "aactor.hpp"
#include "aplayer_camera_manager.hpp"

namespace sky::game::sdk {

    class APlayerController : public UObject {
    public:
        APlayerController() : UObject() {}
        explicit APlayerController(uintptr_t base_address) : UObject(base_address) {}

        AActor get_acknowledged_pawn() const {
            if (!is_valid()) return {};
            return read<AActor>(offsets::AcknowledgedPawn);
		}

        APlayerCameraManager get_player_camera_manager() const {
            if (!is_valid()) return {};
            return read<APlayerCameraManager>(offsets::PlayerCameraManager);
		}

        FRotator ControlRotation() const {
            if (!is_valid()) return {};
            return read<FRotator>(offsets::ControlRotation);
        }

        void SetControlRotation(const FRotator& rotation) {
            if (!is_valid()) return;
            write<FRotator>(offsets::ControlRotation, rotation);
		}
    };

} // namespace sky::game::sdk


