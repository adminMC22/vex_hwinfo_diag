#pragma once

#include "ue_object.hpp"

namespace vex::game::sdk {

    class APlayerCameraManager : public UObject {
    public:
        APlayerCameraManager() : UObject() {}
        explicit APlayerCameraManager(uintptr_t base_address) : UObject(base_address) {}

        FMinimalViewInfo get_camera_view() const {
            if (!is_valid()) {};

			FCameraCacheEntry CameraCachePrivate = read<FCameraCacheEntry>(offsets::CameraCache);

            return CameraCachePrivate.POV;
		}

        // Specific methods may be added as known offsets are found
    };

} // namespace vex::game::sdk


