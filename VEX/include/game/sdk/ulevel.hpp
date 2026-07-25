#pragma once

#include "ue_object.hpp"

namespace sky::game::sdk {

    /**
     * @brief Class ULevel - Represents a level/map in UE5
     */
    class ULevel : public UObject {
    public:
        ULevel() : UObject() {}

        // Constructor using global DriverContext
        explicit ULevel(uintptr_t base_address)
            : UObject(base_address) {}

        // Specific ULevel methods
        TArray<AActor> get_actors() const {
			return read<TArray<AActor>>(offsets::ActorArray);
        }
    };

} // namespace sky::game::sdk

