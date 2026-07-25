#pragma once

#include "ue_object.hpp"
#include "aplayer_state.hpp"

namespace sky::game::sdk {

    /**
    * @brief Class ADamageHandler
    */
    class ADamageHandler : public UObject {
    public:
        ADamageHandler() : UObject() {}
		explicit ADamageHandler(uintptr_t base_address) : UObject(base_address) {}

        float GetHealth() const {
            if (!is_valid()) return 0.0f;
            return read<float>(offsets::Health);
		}

        float GetShield() const {
            if (!is_valid()) return 0.0f;
            return read<float>(offsets::Shield);
        }

        float GetMaxShield() const {
            if (!is_valid()) return 0.0f;
            return read<float>(offsets::MaxShield);
        }

        int32_t GetShieldType() const {
            if (!is_valid()) return 0;
            return read<int32_t>(offsets::ShieldType);
		}
	};
} // namespace sky::game::sdk