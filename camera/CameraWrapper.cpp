/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "PantechCameraWrapper"

#include <cstdint>
#include <dlfcn.h>
#include <errno.h>
#include <hardware/camera.h>
#include <log/log.h>
#include <utils/Mutex.h>

#include <string>
#include <vector>

namespace {

constexpr const char* kVendorCameraHal =
        "/vendor/lib/hw/camera.msm8960.vendor.so";
constexpr const char* kSetRecordingHintValueSymbol =
        "_ZN7android24QCameraHardwareInterface21setRecordingHintValueEi";
constexpr const char* kCreateRecordSymbol =
        "_ZN7android24QCameraHardwareInterface12createRecordEv";
constexpr const char* kInitRecordStreamSymbol =
        "_ZN7android20QCameraStream_record10initStreamEhh";
constexpr size_t kHalObjectOffset = 0x48;
constexpr size_t kRecordStreamOffset = 0x284;
constexpr size_t kRecordingHintOffset = 0x59e;
constexpr size_t kPreviewRestartNeededOffset = 0x5b0;

struct WrappedDevice {
    camera_device_t* device;
    camera_device_ops_t* vendorOps;
    int (*vendorClose)(hw_device_t* device);
    camera_device_ops_t ops;
};

void* gVendorHandle;
camera_module_t* gVendorModule;
using SetRecordingHintValue = int (*)(void*, int);
SetRecordingHintValue gSetRecordingHintValue;
using CreateRecord = int (*)(void*);
CreateRecord gCreateRecord;
using InitRecordStream = int (*)(void*, uint8_t, uint8_t);
InitRecordStream gInitRecordStream;
android::Mutex gLock;
std::vector<WrappedDevice*> gDevices;

WrappedDevice* findDeviceLocked(camera_device_t* device) {
    for (WrappedDevice* wrapped : gDevices) {
        if (wrapped->device == device) {
            return wrapped;
        }
    }
    return nullptr;
}

int loadVendorModule() {
    android::Mutex::Autolock lock(gLock);
    if (gVendorModule != nullptr) {
        return 0;
    }

    gVendorHandle = dlopen(kVendorCameraHal, RTLD_NOW | RTLD_LOCAL);
    if (gVendorHandle == nullptr) {
        ALOGE("Unable to load %s: %s", kVendorCameraHal, dlerror());
        return -ENODEV;
    }

    gVendorModule = reinterpret_cast<camera_module_t*>(
            dlsym(gVendorHandle, HAL_MODULE_INFO_SYM_AS_STR));
    if (gVendorModule == nullptr) {
        ALOGE("Unable to find %s in vendor camera HAL: %s",
              HAL_MODULE_INFO_SYM_AS_STR, dlerror());
        dlclose(gVendorHandle);
        gVendorHandle = nullptr;
        return -EINVAL;
    }

    gSetRecordingHintValue = reinterpret_cast<SetRecordingHintValue>(
            dlsym(gVendorHandle, kSetRecordingHintValueSymbol));
    if (gSetRecordingHintValue == nullptr) {
        ALOGE("Unable to find vendor recording hint setter: %s", dlerror());
        return -EINVAL;
    }
    gCreateRecord = reinterpret_cast<CreateRecord>(
            dlsym(gVendorHandle, kCreateRecordSymbol));
    if (gCreateRecord == nullptr) {
        ALOGE("Unable to find vendor record stream creator: %s", dlerror());
        return -EINVAL;
    }
    gInitRecordStream = reinterpret_cast<InitRecordStream>(
            dlsym(gVendorHandle, kInitRecordStreamSymbol));
    if (gInitRecordStream == nullptr) {
        ALOGE("Unable to find vendor record stream initializer: %s", dlerror());
        return -EINVAL;
    }

    return 0;
}

int ensureRecordStream(camera_device_t* device) {
    if (device->priv == nullptr) {
        return -ENODEV;
    }
    auto privateData = static_cast<uint8_t*>(device->priv);
    auto hal = *reinterpret_cast<uint8_t**>(privateData + kHalObjectOffset);
    if (hal == nullptr) {
        return -ENODEV;
    }
    auto recordStream = *reinterpret_cast<void**>(hal + kRecordStreamOffset);
    if (recordStream == nullptr) {
        int rc = gCreateRecord(hal);
        if (rc < 0) {
            return rc;
        }
        recordStream = *reinterpret_cast<void**>(hal + kRecordStreamOffset);
    }
    if (recordStream == nullptr) {
        return -ENODEV;
    }
    return gInitRecordStream(recordStream, 0, 1);
}

int wrappedStartPreview(camera_device_t* device) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    int rc = ensureRecordStream(device);
    if (rc != 0) {
        ALOGE("Unable to prepare vendor record stream before preview: %d", rc);
        return rc;
    }
    ALOGI("Prepared vendor record stream before preview");
    return vendorOps->start_preview(device);
}

int wrappedStartRecording(camera_device_t* device) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            ALOGE("start_recording called for an unknown camera device");
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    if (device->priv != nullptr) {
        auto privateData = static_cast<uint8_t*>(device->priv);
        auto hal = *reinterpret_cast<uint8_t**>(privateData + kHalObjectOffset);
        if (hal != nullptr) {
            int rc = gSetRecordingHintValue(hal, 1);
            if (rc != 0) {
                ALOGE("Unable to enable vendor recording mode: %d", rc);
                return rc;
            }
            hal[kRecordingHintOffset] = 1;
            hal[kPreviewRestartNeededOffset] = 0;
            auto recordStream =
                    *reinterpret_cast<void**>(hal + kRecordStreamOffset);
            if (recordStream == nullptr) {
                return -ENODEV;
            }
            ALOGI("Starting pre-initialized vendor record stream");
        }
    }

    return vendorOps->start_recording != nullptr
            ? vendorOps->start_recording(device) : -ENOSYS;
}

int wrappedSetParameters(camera_device_t* device, const char* parameters) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    if (vendorOps->set_parameters == nullptr) {
        return -ENOSYS;
    }
    if (parameters == nullptr) {
        return vendorOps->set_parameters(device, parameters);
    }

    std::string adjusted(parameters);
    constexpr const char* enabled = "recording-hint=true";
    size_t pos = adjusted.find(enabled);
    if (pos != std::string::npos) {
        adjusted.replace(pos, std::char_traits<char>::length(enabled),
                         "recording-hint=false");
        ALOGI("Keeping video preview in non-recording mode");
    }
    return vendorOps->set_parameters(device, adjusted.c_str());
}

int wrappedStoreMetaDataInBuffers(camera_device_t* device, int enable) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    // This blob reports metadata-buffer support but its timestamp callback
    // contains raw YUV camera_memory_t buffers. Make CameraSource select its
    // YUV callback path instead of interpreting image bytes as native_handle_t.
    if (enable != 0) {
        ALOGI("Rejecting unsupported video metadata-buffer mode");
        return -ENOSYS;
    }
    return vendorOps->store_meta_data_in_buffers != nullptr
            ? vendorOps->store_meta_data_in_buffers(device, 0) : 0;
}

int wrappedClose(hw_device_t* hwDevice) {
    camera_device_t* device = reinterpret_cast<camera_device_t*>(hwDevice);
    WrappedDevice* wrapped = nullptr;
    {
        android::Mutex::Autolock lock(gLock);
        for (auto it = gDevices.begin(); it != gDevices.end(); ++it) {
            if ((*it)->device == device) {
                wrapped = *it;
                gDevices.erase(it);
                break;
            }
        }
    }

    if (wrapped == nullptr) {
        return -ENODEV;
    }

    device->ops = wrapped->vendorOps;
    device->common.close = wrapped->vendorClose;
    int rc = wrapped->vendorClose != nullptr
            ? wrapped->vendorClose(&device->common) : 0;
    delete wrapped;
    return rc;
}

int wrappedOpen(const hw_module_t*, const char* id, hw_device_t** hwDevice) {
    int rc = loadVendorModule();
    if (rc != 0) {
        return rc;
    }

    hw_device_t* vendorHwDevice = nullptr;
    rc = gVendorModule->common.methods->open(
            &gVendorModule->common, id, &vendorHwDevice);
    if (rc != 0) {
        return rc;
    }

    camera_device_t* device = reinterpret_cast<camera_device_t*>(vendorHwDevice);
    WrappedDevice* wrapped = new WrappedDevice;
    wrapped->device = device;
    wrapped->vendorOps = device->ops;
    wrapped->vendorClose = device->common.close;
    wrapped->ops = *device->ops;
    wrapped->ops.set_parameters = wrappedSetParameters;
    wrapped->ops.store_meta_data_in_buffers = wrappedStoreMetaDataInBuffers;
    wrapped->ops.start_preview = wrappedStartPreview;
    wrapped->ops.start_recording = wrappedStartRecording;

    device->ops = &wrapped->ops;
    device->common.close = wrappedClose;
    {
        android::Mutex::Autolock lock(gLock);
        gDevices.push_back(wrapped);
    }

    *hwDevice = vendorHwDevice;
    return 0;
}

int getNumberOfCameras() {
    return loadVendorModule() == 0 &&
            gVendorModule->get_number_of_cameras != nullptr
            ? gVendorModule->get_number_of_cameras() : 0;
}

int getCameraInfo(int cameraId, camera_info* info) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->get_camera_info != nullptr
            ? gVendorModule->get_camera_info(cameraId, info) : rc;
}

int setCallbacks(const camera_module_callbacks_t* callbacks) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->set_callbacks != nullptr
            ? gVendorModule->set_callbacks(callbacks) : rc;
}

void getVendorTagOps(vendor_tag_ops_t* ops) {
    if (loadVendorModule() == 0 && gVendorModule->get_vendor_tag_ops != nullptr) {
        gVendorModule->get_vendor_tag_ops(ops);
    }
}

int openLegacy(const hw_module_t*, const char* id, uint32_t halVersion,
               hw_device_t** device) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->open_legacy != nullptr
            ? gVendorModule->open_legacy(&gVendorModule->common, id,
                                         halVersion, device) : -ENOSYS;
}

int setTorchMode(const char* cameraId, bool enabled) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->set_torch_mode != nullptr
            ? gVendorModule->set_torch_mode(cameraId, enabled) : -ENOSYS;
}

int initModule() {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->init != nullptr
            ? gVendorModule->init() : rc;
}

hw_module_methods_t gModuleMethods = {
    .open = wrappedOpen,
};

}  // namespace

extern "C" camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        // The proprietary module predates camera module API 2.0 and leaves
        // camera_info::device_version at zero. Advertising 1.0 makes the
        // framework correctly treat each device as HAL1.
        .module_api_version = CAMERA_MODULE_API_VERSION_1_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Pantech MSM8960 Camera Wrapper",
        .author = "The LineageOS Project",
        .methods = &gModuleMethods,
        .dso = nullptr,
        .reserved = {0},
    },
    .get_number_of_cameras = getNumberOfCameras,
    .get_camera_info = getCameraInfo,
    .set_callbacks = setCallbacks,
    .get_vendor_tag_ops = getVendorTagOps,
    .open_legacy = openLegacy,
    .set_torch_mode = setTorchMode,
    .init = initModule,
    .reserved = {nullptr, nullptr, nullptr, nullptr, nullptr},
};
