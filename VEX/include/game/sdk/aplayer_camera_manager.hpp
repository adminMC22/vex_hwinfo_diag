#pragma once

#include "ue_object.hpp"

namespace sky::game::sdk {

    class APlayerCameraManager : public UObject {
    public:
        APlayerCameraManager() : UObject() {}
        explicit APlayerCameraManager(uintptr_t base_address) : UObject(base_address) {}

        FMinimalViewInfo get_camera_view() const {
            if (!is_valid()) return {};

			FCameraCacheEntry CameraCachePrivate = read<FCameraCacheEntry>(offsets::CameraCache);

            return CameraCachePrivate.POV;
		}

        // Specific methods may be added as known offsets are found
    };

} // namespace sky::game::sdk


