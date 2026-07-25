#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "ue_object.hpp"
#include "ulevel.hpp"
#include "ugame_instance.hpp"

namespace sky::game::sdk {

    /**
     * @brief Class UWorld
     */
    class UWorld : public UObject {
    public:
        UWorld() : UObject() {}

        explicit UWorld(uintptr_t base_address)
            : UObject(base_address) {
        }

        uintptr_t get_uworld_address() const { return get_base_address(); }
        bool is_uworld_valid() const { return get_base_address() != 0; }

        UGameInstance get_game_instance() const {
            const auto gi_addr = read<uintptr_t>(offsets::OwningGameInstance);
            return UGameInstance(gi_addr);
		}

        ULevel get_persistent_level() const {
            const auto level_addr = read<uintptr_t>(offsets::PersistentLevel);
            return ULevel(level_addr);
		}
    };

} // namespace sky::game::sdk