/* Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <camera/CameraParametersExtra.h>

// Export only the legacy Pantech extension ABI. No CameraParameters objects
// are instantiated here; standard methods remain provided by libcamera_client.
namespace android {
class CameraParameters {
public:
    CAMERA_PARAMETERS_EXTRA_H
};

CAMERA_PARAMETERS_EXTRA_C
}  // namespace android
