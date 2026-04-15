// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"
#include "Version.h" // for device type

namespace RealSenseID
{
class PreviewImpl;

/**
 * Preview modes
 */
enum class PreviewMode
{
    MJPEG_1080P = 0, // default
    MJPEG_720P = 1,
    RAW10_1080P = 2,
};

/**
 * Preview configuration
 */
struct RSID_API PreviewConfig
{
    DeviceType deviceType = DeviceType::F45x;           // device type
    int cameraNumber = -1;                              // attempt to auto detect by default
    PreviewMode previewMode = PreviewMode::MJPEG_1080P; // RAW10 requires custom fw support
    bool portraitMode = true; // change Preview to get portrait or landscape images. Algo process is defined separately
                              // in DeviceConfig::CameraRotation
    bool rotateRaw = false;   // enables rotation of raw data in portraitMode == true
    bool skip_decode = false; // set to true to skip decoding MJPEG frames to rgb (to get MJPEG frames directly)
};

/**
 * RAW image metadata
 */
struct RSID_API ImageMetadata
{
    unsigned int timestamp = 0; // sensor timestamp (milliseconds)
    unsigned int exposure = 0;
    unsigned int gain = 0;
    char led = 0;
    unsigned int sensor_id = 0;
    unsigned int status = 0;
    char is_snapshot = 0;
};

/**
 * Image data for preview
 */
struct RSID_API Image
{
    unsigned char* buffer = nullptr;
    unsigned int size = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int stride = 0;
    unsigned int number = 0;
    ImageMetadata metadata;
};

/**
 * User defined callback for preview.
 * OnPreviewImageReady Callback will be used to provide RGB preview image (for RAW10_1080P PreviewMode - raw converted
 * to RGB). OnSnapshotImageReady Callback will be used to provide images destined to be dumped (not for preview) for raw
 * preview mode: Raw10 formatted images (relevant only if PreviewMode is RAW10_1080P) other preview modes: Cropped
 * face/Full image for any authentication/enrollment (based on DeviceConfig::DumpMode attribute)
 */
class RSID_API PreviewImageReadyCallback
{
public:
    virtual ~PreviewImageReadyCallback() = default;
    virtual void OnPreviewImageReady(const Image& image) = 0;
    virtual void OnSnapshotImageReady(const Image& /*image*/) {}; // Empty implementation for backward compatibility
};

/**
 * Preview Support. Use StartPreview to get callbacks for image frames
 */
class RSID_API Preview
{
public:
    explicit Preview(const PreviewConfig&);
    ~Preview();

    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    /**
     * Start preview.
     *
     * @param callback reference to callback object
     * @return True on success.
     */
    bool StartPreview(PreviewImageReadyCallback& callback);

    /**
     * Pause preview.
     *
     * @return True on success.
     */
    bool PausePreview();

    /**
     * Resume preview.
     *
     * @return True on success.
     */
    bool ResumePreview();

    /**
     * Stop preview.
     *
     * @return True on success.
     */
    bool StopPreview();

private:
    RealSenseID::PreviewImpl* _impl = nullptr;
};
} // namespace RealSenseID
