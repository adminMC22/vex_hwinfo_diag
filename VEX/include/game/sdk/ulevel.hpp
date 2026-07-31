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

        // UE 5.5+: actors live in ULevelActorContainer (ULevel->ActorCluster->Actors).
        // Falls back to the legacy direct array if the cluster pointer is null.
        TArray<AActor> get_actors() const {
            if (!is_valid()) return {};

            const auto cluster = read<uintptr_t>(offsets::ActorCluster);
            if (cluster) {
                return sky::driver::g_driver->read<TArray<AActor>>(cluster + offsets::LevelActors);
            }
            return sky::driver::g_driver->read<TArray<AActor>>(offsets::ActorArray);
        }
    };

} // namespace sky::game::sdk

