#pragma once

#include "ue_object.hpp"

namespace vex::game::sdk {

    class APlayerState : public UObject {
    public:
        APlayerState() : UObject() {}
        explicit APlayerState(uintptr_t base_address) : UObject(base_address) {}

        uintptr_t TeamComponent() const {
            if (!is_valid()) return 0;
            return read<uintptr_t>(offsets::TeamComponent);
		}

        int32_t TeamId() const {
            if (!is_valid()) return -1;
			auto team_component = TeamComponent();
            if (team_component == 0) return 0;
			return vex::driver::g_driver->read<int32_t>(team_component + offsets::TeamID);
        }
    };
}

