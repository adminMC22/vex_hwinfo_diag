#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <vector>
#include <Windows.h>

namespace vex::driver {

    // Forward declarations
    struct MemoryAllocation;
    struct ProcessInfo;
    struct ModuleInfo;

    /**
     * @brief Abstract interface for driver operations
     *
     * This interface defines the contract for all driver operations,
     * allowing different implementations and facilitating testing.
     */
    class IDriver {
    public:
        virtual ~IDriver() = default;

        // Setup and management
        virtual bool setup() = 0;
        virtual void unload() = 0;
        virtual bool is_valid() const = 0;

        // Process management
        virtual bool attach_process(const std::wstring& process_name) = 0;
        virtual bool attach_process(uint32_t process_id) = 0;
        virtual uint32_t get_process_id() const = 0;
        virtual uintptr_t get_base_address() const = 0;
        virtual uintptr_t get_dtb() const = 0;

        virtual bool move_mouse(uint32_t x, uint32_t y, uint16_t button_flags) = 0;

        // Memory operations
        virtual bool read_memory(uintptr_t address, void* buffer, size_t size) = 0;

        virtual bool write_memory(void* dest, void* src, size_t size) = 0;

        virtual bool stream_mode(HWND hwnd, uint32_t flag) = 0;

        // Template method for typed memory reading
        template<typename T>
        T read(uintptr_t address, size_t size = sizeof(T)) {
            T buffer{};
            if (read_memory(address, &buffer, size)) {
                return buffer;
            }
            return T{};
        }

        template<typename T>
        bool write(uintptr_t address, const T& value) {
            return write_memory(reinterpret_cast<void*>(address), const_cast<T*>(&value), sizeof(T));
        }

        // Method for batch array reading
        template<typename T>
        std::vector<T> read_array_batch(uintptr_t data_ptr, int32_t count) {
            std::vector<T> result;
            if (!data_ptr || count <= 0) {
                return result;
            }

            // Allocate buffer for all elements
            const size_t total_size = count * sizeof(T);
            std::vector<std::uint8_t> buffer(total_size);

            // Read all data at once
            if (read_memory(data_ptr, buffer.data(), total_size)) {
                result.reserve(count);

                // Convert bytes to T objects
                const T* data = reinterpret_cast<const T*>(buffer.data());
                for (int32_t i = 0; i < count; ++i) {
                    result.emplace_back(data[i]);
                }
            }

            return result;
        }

        // System information
        virtual uintptr_t get_kernel_base(const std::string& module_name) = 0;

        // Utilities
        virtual void* find_pattern(uintptr_t base, const char* pattern, const char* mask) = 0;

        virtual void set_dir_base(void* dir) = 0;
    };

    // Data structures
    struct MemoryAllocation {
        uintptr_t address;
        uintptr_t mdl;
        size_t size;
        uint32_t protection;
    };

    struct ProcessInfo {
        uint32_t process_id;
        uintptr_t base_address;
        uintptr_t dtb;
        std::wstring name;
    };

    struct ModuleInfo {
        uintptr_t base_address;
        size_t size;
        std::string name;
    };

    /**
     * @brief Factory function to create driver instance
     */
    std::shared_ptr<IDriver> create_driver();

} // namespace vex::driver
