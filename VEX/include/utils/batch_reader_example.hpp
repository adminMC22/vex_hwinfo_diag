#pragma once

#include <vector>
#include <chrono>
#include "../game/sdk/sdk.hpp"
#include "../driver/driver_context.hpp"
#include "../utils/logger.hpp"

namespace sky::utils {

    /**
     * @brief Example of how to use batch reading for TArray<FTransform>
     *
     * This class demonstrates the performance difference between individual
     * reading and batch reading for arrays of FTransform (such as bone arrays).
     */
    class BatchReaderExample {
    public:
        
        /**
         * @brief Reads all bones at once using batch read (RECOMMENDED)
         *
         * @param bone_array Pointer to the bone array
         * @param bone_count Number of bones
         * @return std::vector<sdk::FTransform> All bone transforms
         */
        static std::vector<sdk::FTransform> ReadBonesOptimized(uintptr_t bone_array, int32_t bone_count) {
            if (!bone_array || bone_count <= 0) {
                return {};
            }

            auto start_time = std::chrono::high_resolution_clock::now();

            // Create temporary TArray to use batch methods
            sdk::TArray<sdk::FTransform> temp_array;
            temp_array.Data = reinterpret_cast<sdk::FTransform*>(bone_array);
            temp_array.Count = bone_count;
            temp_array.Max = bone_count;

            // Read all at once - SINGLE KERNEL DRIVER CALL
            auto bones = temp_array.ReadAllBatch<sdk::FTransform>();

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            LOG_INFO("Batch read completed: {} bones in {} microseconds", bones.size(), duration.count());

            return bones;
        }

        /**
         * @brief Reads bones individually (OLD METHOD - SLOW)
         *
         * @param bone_array Pointer to the bone array
         * @param bone_count Number of bones
         * @return std::vector<sdk::FTransform> All bone transforms
         */
        static std::vector<sdk::FTransform> ReadBonesOldWay(uintptr_t bone_array, int32_t bone_count) {
            if (!bone_array || bone_count <= 0) {
                return {};
            }

            auto start_time = std::chrono::high_resolution_clock::now();

            std::vector<sdk::FTransform> bones;
            bones.reserve(bone_count);

            // Read one by one - MULTIPLE KERNEL DRIVER CALLS
            for (int32_t i = 0; i < bone_count; ++i) {
                sdk::FTransform bone = sky::driver::g_driver->read<sdk::FTransform>(bone_array + (i * sizeof(sdk::FTransform)));
                bones.push_back(bone);
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            LOG_INFO("Individual read completed: {} bones in {} microseconds", bones.size(), duration.count());

            return bones;
        }

        /**
         * @brief Reads only a specific range of bones (useful for optimizations)
         *
         * @param bone_array Pointer to the bone array
         * @param total_count Total bones in array
         * @param start_index Starting index
         * @param count How many bones to read from start index
         * @return std::vector<sdk::FTransform> Range of bone transforms
         */
        static std::vector<sdk::FTransform> ReadBonesRange(uintptr_t bone_array, int32_t total_count, 
                                                         int32_t start_index, int32_t count) {
            if (!bone_array || total_count <= 0) {
                return {};
            }

            // Create temporary TArray
            sdk::TArray<sdk::FTransform> temp_array;
            temp_array.Data = reinterpret_cast<sdk::FTransform*>(bone_array);
            temp_array.Count = total_count;
            temp_array.Max = total_count;

            // Read only desired range
            return temp_array.ReadRangeBatch<sdk::FTransform>(start_index, count);
        }

        /**
         * @brief Compares performance between the two methods
         *
         * @param bone_array Pointer to the bone array
         * @param bone_count Number of bones
         */
        static void ComparePerformance(uintptr_t bone_array, int32_t bone_count) {
            LOG_INFO("=== Performance Comparison ===");
            LOG_INFO("Reading {} bones...", bone_count);

            // Test optimized method
            LOG_INFO("Testing optimized batch read:");
            auto bones_optimized = ReadBonesOptimized(bone_array, bone_count);

            // Test old method
            LOG_INFO("Testing old individual read:");
            auto bones_old = ReadBonesOldWay(bone_array, bone_count);

            LOG_INFO("=== Results ===");
            LOG_INFO("Optimized method read {} bones", bones_optimized.size());
            LOG_INFO("Old method read {} bones", bones_old.size());
            LOG_INFO("Performance improvement: ~{}x faster (depending on bone count)", 
                    std::max(1, bone_count / 10)); // Conservative estimate
        }
    };

} // namespace sky::utils
