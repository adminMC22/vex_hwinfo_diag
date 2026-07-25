#pragma once

#include "../game/sdk/aactor.hpp"
#include "../utils/logger.hpp"
#include <chrono>

namespace sky::utils {

    /**
     * @brief Example demonstrating when to use each bone reading method
     */
    class BoneOptimizationExample {
    public:
        
        /**
         * @brief For AIMBOT - single bone reading (current method is optimal)
         */
        static sdk::FVector GetAimbotBone(const sdk::AActor& actor, int32_t bone_index) {
            // For single bone, current method is perfect
            auto mesh = actor.Mesh();
            auto bone_array = actor.BoneArray();
            
            return actor.GetBoneWithRotation(mesh, bone_array, bone_index);
            // Result: 2 kernel calls (bone + ComponentToWorld)
        }
        
        /**
         * @brief For ESP/SKELETON - multiple bone reading (optimized method)
         */
        static std::vector<sdk::FVector> GetESPBones(const sdk::AActor& actor, 
                                                    const std::vector<int32_t>& bone_indices) {
            auto mesh = actor.Mesh();
            auto bone_array = actor.BoneArray();
            
            // OPTIMIZED METHOD - reads all at once
            return actor.GetMultipleBonesWithRotation(mesh, bone_array, bone_indices);
            // Result: 2 kernel calls (batch bones + ComponentToWorld)
            // vs old method: N+1 calls (N bones + ComponentToWorld)
        }
        
        /**
         * @brief Performance comparison
         */
        static void PerformanceComparison(const sdk::AActor& actor) {
            std::vector<int32_t> skeleton_bones = {8, 21, 6, 23, 24, 25, 49, 50, 51, 4, 3, 77, 78, 80, 84, 85, 87};
            
            LOG_INFO("=== Bone Reading Performance Test ===");
            
            // Test old method (individual)
            auto start = std::chrono::high_resolution_clock::now();
            std::vector<sdk::FVector> bones_old;
            auto mesh = actor.Mesh();
            auto bone_array = actor.BoneArray();
            
            for (int32_t bone_idx : skeleton_bones) {
                bones_old.push_back(actor.GetBoneWithRotation(mesh, bone_array, bone_idx));
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration_old = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            // Test optimized method (batch)
            start = std::chrono::high_resolution_clock::now();
            auto bones_new = actor.GetMultipleBonesWithRotation(mesh, bone_array, skeleton_bones);
            end = std::chrono::high_resolution_clock::now();
            auto duration_new = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            LOG_INFO("Individual reads: {} bones in {} microseconds ({} calls to kernel)", 
                    bones_old.size(), duration_old.count(), skeleton_bones.size() + 1);
            LOG_INFO("Batch read: {} bones in {} microseconds (2 calls to kernel)", 
                    bones_new.size(), duration_new.count());
            
            if (duration_old.count() > 0) {
                double improvement = (double)duration_old.count() / duration_new.count();
                LOG_INFO("Performance improvement: {:.1f}x faster", improvement);
            }
        }
        
        /**
         * @brief Real world usage example
         */
        static void RealWorldUsage() {
            LOG_INFO("=== Real World Usage Examples ===");
            LOG_INFO("");
            LOG_INFO("AIMBOT (1 bone individual):");
            LOG_INFO("  auto head_pos = actor.GetBoneWithRotation(mesh, bone_array, head_bone);");
            LOG_INFO("  // Optimal: 2 kernel calls total");
            LOG_INFO("");
            LOG_INFO("ESP/SKELETON (17 bones):");
            LOG_INFO("  std::vector<int32_t> bones = {8,21,6,23,24,25,49,50,51,4,3,77,78,80,84,85,87};");
            LOG_INFO("  auto positions = actor.GetMultipleBonesWithRotation(mesh, bone_array, bones);");
            LOG_INFO("  // Optimal: 2 kernel calls total vs 18 calls with old method");
            LOG_INFO("");
            LOG_INFO("MIXED USAGE:");
            LOG_INFO("  - Use GetBoneWithRotation() for 1-3 bones");
            LOG_INFO("  - Use GetMultipleBonesWithRotation() for 4+ bones");
        }
    };

} // namespace sky::utils
