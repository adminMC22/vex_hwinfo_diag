#include "../../include/common.hpp"
#include "../../include/driver/driver_context.hpp"
#include "../../include/game/vgk_system.hpp"
#include "../../include/game/offsets.hpp"
#include "../../include/utils/logger.hpp"
#include "../../include/driver/idriver.hpp"
#include <intrin.h>

namespace sky::game {
    using namespace sky::utils;  // To use LOG_* directly

	uintptr_t VGK::decrypt_cloned_cr3_simplified(uintptr_t cloned_cr3)
	{
		uintptr_t Last = cloned_cr3 & 0xFFFFFFFFF;
		uintptr_t FirstDigit = Last >> 32 & 0xF;
		uintptr_t LastDigit = Last & 0xF;

		FirstDigit -= 1;
		LastDigit -= 1;

		return FirstDigit << 32 | Last & 0x0FFFFFFF0 | LastDigit;
	}

	ShadowData VGK::find_pml4_base() {
		ShadowRegionsDataStructure data = sky::driver::g_driver->read<ShadowRegionsDataStructure>(sky::driver::g_driver->get_kernel_base("vgk.sys") + offsets::vgk::ShadowRegions);
		if (!data.OriginalPML4_t || !data.ClonedPML4_t) {
			data = sky::driver::g_driver->read<ShadowRegionsDataStructure>(sky::driver::g_driver->get_kernel_base("vgk.sys") + offsets::old_vgk::ShadowRegions);
		}
		auto decypted_cloned_cr3 = this->decrypt();
		return { decypted_cloned_cr3, data.FreeIndex << 0x27 };
	}

	uint64_t VGK::cpuid_rax() {
		int cpuInfo[4] = { 0 };
		__cpuid(cpuInfo, 1);
		return static_cast<int64_t>(static_cast<int32_t>(cpuInfo[0]));
	}

	uintptr_t VGK::decrypt() {
		try
		{
			auto base = sky::driver::g_driver->get_kernel_base(xorstr_("vgk.sys"));
			if(!base) {
				return 0;
			}

			auto byte = sky::driver::g_driver->read<uint8_t>(base + offsets::vgk::ShadowRegionB);
			if(!byte) {
				byte = sky::driver::g_driver->read<uint8_t>(base + offsets::old_vgk::ShadowRegionB);
			}
			auto key = sky::driver::g_driver->read<uintptr_t>(base + offsets::vgk::ShadowRegionQ);
			if(!key) {
				key = sky::driver::g_driver->read<uintptr_t>(base + offsets::old_vgk::ShadowRegionQ);
			}

			auto _RAX = cpuid_rax();
			uint64_t v5 = byte & 0x73;
			uint64_t v11 = ~(int)_RAX;

			uint64_t v12 =
				(0x8000000000000001ULL * (int)_RAX
					+ 0xAFDBF65F8A4AC9C9ULL * ~key
					+ 0x2FDBF65F8A4AC9C9ULL * (key + 1)
					+ (((key ^ (int)_RAX) << 63) ^ 0x8000000000000000ULL))
				*
				(0x7D90DC33C620C593ULL * key * (0x13D0F34E00000000ULL * key + 0x483C4F8900000000ULL)
					+ 0xFD90DC33C620C592ULL * ~(key * (0x13D0F34E00000000ULL * key + 0x483C4F8900000000ULL))
					+ (key *
						(0xCE3CE5E180000000ULL * ~key
							+ 0x55494E5B80000000ULL * (int)_RAX
							+ 0xC83B18136241A38DULL * v11
							+ 0x72F1C9B7E241A38DULL *
							(
								((int)_RAX | 0x3F71D992FBB2CCEBULL) -
								(0x3F71D992FBB2CCEAULL - ((int)_RAX & 0x3F71D992FBB2CCEBULL))
								)
							)
						+ 0x71C31A1E80000000ULL)
					*
					(0x99BF7D2380CF6EC3ULL * (int)_RAX
						+ 0x664082DC7F30913EULL * (v5 | key)
						+ 0x19BF7D2380CF6EC2ULL * v11
						+ 0xE64082DC7F30913EULL * (~key & ~v5)
						+ ((key + (v5 | (int)_RAX) + (v5 & (key ^ (int)_RAX))) << 63))
					+ 0x2183995CC620C592ULL);

			uint64_t expr =
				0x49B74B6480000000ULL * key +
				0xC2D8B464B4418C6CULL * ~v5 +
				0x66B8CDC1FFFFFFFFULL * v5 +
				0x5C1FE6A2B4418C6DULL * ((v5 & key) - (key | ~v5));

			uint64_t logic =
				-3 * ~key +
				(v5 ^ key) +
				-2 * (v5 ^ (v5 | key)) +
				2 * (((v5 | 0x3F71D992FBB2CCEBULL) ^ 0xC08E266D044D3314ULL) + (~key | v5 ^ 0x3F71D992FBB2CCEBULL)) -
				(v5 ^ ~key ^ 0x3F71D992FBB2CCEBULL) -
				0x3F71D992FBB2CCEBULL;

			uint64_t v13 =
				((~(expr * logic) ^ ~v12) << 63) +
				0x6C80110954C7304DULL *
				(
					((int)_RAX & (expr * logic)) -
					(v11 & ~(expr * logic)) -
					(int)_RAX
					);

			uint64_t DecryptedCR3 =
				0x137FEEF6AB38CFB4ULL * (expr * logic) +
				v13 -
				0x7FFFFFFFFFFFFFFFULL * v12 -
				0x4F167C5CD4C7304EULL;

			return DecryptedCR3;
		}
		catch (...) {
		}
	}

} // namespace sky::game
