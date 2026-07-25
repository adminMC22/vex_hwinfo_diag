#pragma once

#include <vector>
#include <algorithm>
#include "ue_object.hpp"
#include "aplayer_state.hpp"
#include "adamage_handler.hpp"

namespace vex::game::sdk {

	/**
	 * @brief Class UVisibilityComponent
	 */
    class UVisibilityComponent : public UObject {
    public:
        UVisibilityComponent() : UObject() {}
        explicit UVisibilityComponent(uintptr_t base_address) : UObject(base_address) {}

        float VisibilityLostTime() {
			if (!is_valid()) return 0.0f;

			return read<float>(0x016C);
        }
    };


    /**
	* @brief Class AShooterCharacterMinimapComponent
    */
    class AShooterCharacterMinimapComponent : public UObject {
    public:
        AShooterCharacterMinimapComponent() : UObject() {}
        explicit AShooterCharacterMinimapComponent(uintptr_t base_address) : UObject(base_address) {}

        bool bVisible() const {
            if (!is_valid()) return false;
            return read<bool>(0x659);
		}

        UVisibilityComponent VisibilityComponent() {
            if (!is_valid()) return {};

			return read<UVisibilityComponent>(0x0680);
        }
    };

    /**
	* @brief Class AActor
    */
    class AActor : public UObject {
    public:
        AActor() : UObject() {}
        explicit AActor(uintptr_t base_address) : UObject(base_address) {}

        int32_t UniqueID() const {
            if (!is_valid()) return -1;
            return read<int32_t>(offsets::UniqueID);
        }

        bool isDormant() const {
            if (!is_valid()) return false;
            return read<bool>(offsets::Dormant);
		}

        uintptr_t Mesh() const {
            if (!is_valid()) return 0;
            return read<uintptr_t>(offsets::Mesh);
        }

        int32_t BoneCount(uintptr_t ) const {
            if (!is_valid()) return 0;
			auto mesh = Mesh();
            if (!mesh) return 0;

            auto bone_count = vex::driver::g_driver->read<int32_t>(mesh + offsets::BoneArray + 0x8);
            if(!bone_count)
				bone_count = vex::driver::g_driver->read<int32_t>(mesh + offsets::BoneArrayCache + 0x8);

            return bone_count;
        }

        uintptr_t BoneArray() const {
			if (!is_valid()) return 0;

            auto mesh = Mesh();
            if (!mesh) return 0;

			auto bone_array = vex::driver::g_driver->read<uintptr_t>(mesh + offsets::BoneArray);
            if (!bone_array) 
                return vex::driver::g_driver->read<uintptr_t>(mesh + offsets::BoneArrayCache);

			return bone_array;
		}

        FVector GetBoneWithRotation(uintptr_t mesh, uintptr_t bone_array, int32_t idx) const {
            if (!is_valid()) return {};
			FTransform bone = vex::driver::g_driver->read<FTransform>(bone_array + (idx * 0x60));

            FTransform ComponentToWorld = vex::driver::g_driver->read<FTransform>(mesh + offsets::ComponentToWorld);

            FMatrix Matrix = bone.ToMatrixWithScale() * ComponentToWorld.ToMatrixWithScale();
            return FVector{ Matrix.WPlane.X, Matrix.WPlane.Y, Matrix.WPlane.Z };
        }

        // New optimized method for multiple bones
        std::vector<FVector> GetMultipleBonesWithRotation(uintptr_t mesh, uintptr_t bone_array, 
                                                         const std::vector<int32_t>& bone_indices) const {
            std::vector<FVector> result;
            if (!is_valid() || bone_indices.empty()) return result;

            // Ler ComponentToWorld uma vez
            FTransform ComponentToWorld = vex::driver::g_driver->read<FTransform>(mesh + offsets::ComponentToWorld);
            
            // Find range of required bones
            int32_t min_idx = *std::min_element(bone_indices.begin(), bone_indices.end());
            int32_t max_idx = *std::max_element(bone_indices.begin(), bone_indices.end());
            int32_t bone_count = max_idx - min_idx + 1;

            // Read all required bones at once
            auto bones_batch = vex::driver::g_driver->read_array_batch<FTransform>(
                bone_array + (min_idx * 0x60), bone_count);

            if (bones_batch.size() != bone_count) return result;

            result.reserve(bone_indices.size());
            
            // Processar cada bone solicitado
            for (int32_t idx : bone_indices) {
                int32_t local_idx = idx - min_idx;
                if (local_idx >= 0 && local_idx < bone_count) {
                    FMatrix Matrix = bones_batch[local_idx].ToMatrixWithScale() * ComponentToWorld.ToMatrixWithScale();
                    result.emplace_back(Matrix.WPlane.X, Matrix.WPlane.Y, Matrix.WPlane.Z);
                } else {
                    result.emplace_back(); // Invalid bone
                }
            }

            return result;
        }

        uintptr_t ComponentToWorld() const {
            if (!is_valid()) return 0;
            return read<uintptr_t>(offsets::ComponentToWorld);
		}
       
        ADamageHandler DamageHandler() const {
            if (!is_valid()) return {};
            return read<ADamageHandler>(offsets::DamageHandler);
        }

        APlayerState PlayerState() const {
            if (!is_valid()) return {};
			
			return vex::driver::g_driver->read<APlayerState>(m_base_address + offsets::PlayerState);
		}

        bool wasAlly() const {
            if (!is_valid()) return false;
			return read<bool>(offsets::bWasAlly);
		}

        AShooterCharacterMinimapComponent MinimapComponent() const {
            if (!is_valid()) return {};
            return read<AShooterCharacterMinimapComponent>(offsets::CharacterMinimap);
		}

        bool IsVisible(uintptr_t Mesh) const {
            if (!is_valid()) return {};

            float lastRenderTime = vex::driver::g_driver->read<float>(Mesh + offsets::LastRenderTime);
            float lastSubmitTime = vex::driver::g_driver->read<float>(Mesh + offsets::LastSubmitTime);
            float visionTick = lastRenderTime + 0.06f;

            auto minimap_comp = MinimapComponent();
			if (!minimap_comp.is_valid()) return visionTick >= lastSubmitTime;

            return visionTick >= lastSubmitTime && minimap_comp.bVisible();
        }

        UObject CurrentEquippable() const {
            if (!is_valid()) return {};
			auto inventory = read<uintptr_t>(offsets::Inventory);

            if (!inventory) return {};
			return vex::driver::g_driver->read<UObject>(inventory + offsets::CurrentEquippable);
		}
    };

} // namespace vex::game::sdk


