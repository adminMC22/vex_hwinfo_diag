#pragma once

#include "sdk.hpp"
#include "../offsets.hpp"
#include "../../driver/driver_context.hpp"
#include "../../utils/logger.hpp"
#include "fname_cache.hpp"
#include "fname_cache_context.hpp"
#include <memory>
#include <string>
#include <vector>
#include <algorithm> 

namespace sky::game::sdk {

    class FNameCache; // forward declaration

    // Forward declarations for UE5 structures
    class UClass;

    /**
     * @brief Simple base class for all UE5 objects
     * No smart pointer overhead, just a wrapper for memory reading
     */
    class UObject {
    public:
        UObject() : m_base_address(0) {}

        // Constructor that uses global driver
        explicit UObject(uintptr_t base_address)
            : m_base_address(base_address) {}

        // Basic getters
        uintptr_t get_base_address() const { return m_base_address; }
        void set_base_address(uintptr_t address) { m_base_address = address; }
        bool is_valid() const { return m_base_address != 0; }

        // Simple template for reading
        template<typename T>
        T read(uintptr_t offset = 0) const {
            if (!this->is_valid()) return T{};
            return sky::driver::g_driver->read<T>(m_base_address + offset);
        }

		template<typename T>
        void write(uintptr_t offset, const T& value) const {
            if (!this->is_valid()) return;
            sky::driver::g_driver->write<T>(m_base_address + offset, value);
		}

        uintptr_t get_vtable() const {
            return read<uintptr_t>(0x00);
        }

        uint32_t get_flags() const {
            return read<uint32_t>(0x08);
        }

        int32_t get_index() const {
            return read<int32_t>(0x0C);
        }

        UObject get_class() const {
            return read<UObject>(0x10);
        }

        UObject get_super() const {
            return read<UObject>(0x48);
		}

        UObject get_outer() const {
            return read<UObject>(0x20);
        }

        std::int32_t get_name() const {
            return read<std::int32_t>(0x18);
        }

        bool is_a(const std::string& class_name) {
            if (!this) return false;

            auto current_class = get_class();
            if (!current_class.is_valid()) return false;

            UObject current_struct = current_class;
            while (current_struct.is_valid()) {
                std::string current_class_name = current_struct.get_name_string();
                if (current_class_name == class_name) {
                    return true;
                }

                current_struct = current_struct.get_super();
            }
            return false;
        }

        uintptr_t RootComponent() const {
            if (!is_valid()) return 0;
            return read<uintptr_t>(offsets::RootComponent);
        }

        FVector RelativeLocation() const {
            if (!is_valid()) return {};
            auto root_comp = RootComponent();
            if (!root_comp) return {};
            return sky::driver::g_driver->read<FVector>(root_comp + offsets::RelativeLocation);
        }

        std::string get_name_string() const {
            const std::int32_t key = get_name();
            if(key == 0)
				return "None";

            if (auto cached = get_global_fname_cache().try_get(key))
            {
                return *cached;
            }

            // Direct name pool read
            const std::uint32_t chunkOffset = static_cast<std::uint32_t>(static_cast<std::uint32_t>(key) >> 16);
            const std::uint16_t nameOffset = static_cast<std::uint16_t>(key & 0xFFFF);
            const std::uintptr_t chunkAddr = sky::driver::g_driver->get_base_address() + offsets::FNamePool + ((static_cast<unsigned long long>(chunkOffset) + 2ULL) * 8ULL);
            const std::uintptr_t namePoolChunk = sky::driver::g_driver->read<std::uintptr_t>(chunkAddr);
            if (namePoolChunk == 0) return "None";
            const std::uintptr_t entryOffset = namePoolChunk + (static_cast<unsigned long long>(4) * nameOffset);

            FNameEntry nameEntry = sky::driver::g_driver->read<FNameEntry>(entryOffset);
            std::uintptr_t nameKey = DecryptNameKey();
            const std::uint16_t len = nameEntry.Header.Len;
            if (!nameKey || len == 0 || len > 1023) {
                return "None";
            }

            std::string name(nameEntry.AnsiName, len);
            for (std::uint16_t i = 0; i < len; i++) {
                std::uint8_t b = i & 3;
                name[i] ^= static_cast<char>(len ^ *(((std::uint8_t*)&nameKey) + b));
            }

			if (name.empty()) return "None";

            if (!std::all_of(name.begin(), name.end(),
                [](unsigned char c) { return c <= 0x7F; }))
            {
                return "None";
            }

            get_global_fname_cache().put(key, name);
            return name;
        }
    protected:
        uintptr_t m_base_address;
        
        __forceinline __int64 decrypt_xor_keys(const uint32_t StateKey, const uintptr_t* State) const
        {
            uint64_t mixed = 0x2545F4914F6CDD1DULL * static_cast<uint64_t>(StateKey ^ ((StateKey ^ (StateKey >> 15)) >> 12) ^ (StateKey << 25));
            uint32_t index = mixed % 7;
            uint32_t high = mixed >> 32;
            uint64_t v = State[index];

            uint64_t sum = static_cast<uint64_t>(index) + high;
            unsigned int mod = sum % 63;
            unsigned int div = sum / 63;
            unsigned int low = static_cast<unsigned int>(sum % 256);
            unsigned int shiftR = mod + 1;
            unsigned int shiftL = 63 * div - low + 63;

            switch (index) {
            case 0:
                v = ~(v - (high - 1));
                break;
            case 1:
                v ^= static_cast<uint32_t>(index + high) ^ static_cast<uint32_t>(high + 2 * index);
                break;
            case 2: {
                uint64_t v9 = ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL) >> 2;
                uint64_t v10 = v9 ^ (v9 ^ (4ULL * ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL))) & 0xCCCCCCCCCCCCCCCCULL;
                uint64_t v11 = v10 >> 4;
                uint64_t temp12 = ((v11 ^ (v11 ^ (v10 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) >> 8) ^
                    (((v11 ^ (v11 ^ (v10 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) >> 8) ^
                        ((v11 ^ (v11 ^ (v10 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) << 8)) & 0xFF00FF00FF00FF00ULL;
                uint64_t swapped = (temp12 << 32) | (temp12 >> 32);
                v = __ROR8__(swapped, shiftR);
                break;
            }
            case 3: {
                uint64_t v29 = ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL) >> 2;
                uint64_t v30 = v29 ^ (v29 ^ (4ULL * ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL))) & 0xCCCCCCCCCCCCCCCCULL;
                uint64_t v31 = v30 >> 4;
                uint64_t temp32 = ((v31 ^ (v31 ^ (v30 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) >> 8) ^
                    (((v31 ^ (v31 ^ (v30 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) >> 8) ^
                        ((v31 ^ (v31 ^ (v30 << 4)) & 0xF0F0F0F0F0F0F0F0ULL) << 8)) & 0xFF00FF00FF00FF00ULL;
                uint64_t swapped = (temp32 << 32) | (temp32 >> 32);
                v = __ROL8__(swapped, shiftR);
                break;
            }
            case 4:
                v += static_cast<uint64_t>(high + 2 * index) + static_cast<uint64_t>(index + high);
                break;
            case 5:
                v = (~v << shiftL) | (~v >> shiftR);
                break;
            case 6: {
                uint64_t v13 = ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL) >> 2;
                uint64_t v14 = v13 ^ (v13 ^ (4ULL * ((v >> 1) ^ ((v >> 1) ^ (v << 1)) & 0xAAAAAAAAAAAAAAAAULL))) & 0xCCCCCCCCCCCCCCCCULL;
                uint64_t v15 = (v14 >> 4) ^ ((v14 >> 4) ^ (v14 << 4)) & 0xF0F0F0F0F0F0F0F0ULL;
                uint64_t temp = (v15 >> 8) ^ ((v15 >> 8) ^ (v15 << 8)) & 0xFF00FF00FF00FF00ULL;
                uint64_t swapped = (temp << 32) | (temp >> 32);
                v = swapped - static_cast<uint32_t>(index + high);
                break;
            }
            }
            return v ^ StateKey;
        }

        uintptr_t DecryptNameKey() const
        {
            uintptr_t namepool_state_offset = offsets::FNameState;
            uintptr_t namepool_key_offset = namepool_state_offset + 0x38;

            uint64_t namepoolkey = sky::driver::g_driver->read<uint64_t>(sky::driver::g_driver->get_base_address() + namepool_key_offset);
            if (!namepoolkey)
                return 0;
#pragma pack(push, 1)
            struct State {
                uintptr_t keys[7];
            };
#pragma pack(pop)
            State xor_state = sky::driver::g_driver->read<State>(sky::driver::g_driver->get_base_address() + namepool_state_offset);
            auto address = decrypt_xor_keys(namepoolkey, reinterpret_cast<uintptr_t*>(&xor_state));
            uintptr_t xors = sky::driver::g_driver->read<uintptr_t>(address);
            return xors;
        }
    };

} // namespace sky::game::sdk