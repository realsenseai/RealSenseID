// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/Preview.h"
#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#ifdef RSID_SECURE
#include "secure_mode_helper.h"
#endif
#include <chrono>
#include <thread>
#include <iostream>

class EmptyAuthenticationCallback : public RealSenseID::AuthenticationCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id, short score) override
    {
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
    }

    void OnPersonDetected(const std::vector<RealSenseID::PersonRect>& persons, unsigned int ts) override
    {
    }
};

class PreviewSampleCallback : public RealSenseID::PreviewImageReadyCallback
{
public:
    void OnPreviewImageReady(const RealSenseID::Image& image)
    {
        std::cout << "preview -> " << image.width << "x" << image.height << " (" << image.size << "B)\n";
    }

    void OnSnapshotImageReady(const RealSenseID::Image& image)
    {
        std::cout << "snapshot -> " << image.width << "x" << image.height << " (" << image.size << "B)\n";
    }
};

int main()
{
    auto devices = RealSenseID::DiscoverDevices();
    if (devices.empty())
    {
        std::cout << "No device detected" << std::endl;
        return 1;
    }
    auto& device = devices.front();
    std::cout << "Using device on port " << device.serialPort << std::endl;

    RealSenseID::PreviewConfig preview_config;
    preview_config.cameraNumber = device.cameraNumber;
    preview_config.deviceType = device.deviceType;
    RealSenseID::Preview preview(preview_config);
    PreviewSampleCallback preview_callback;

    RealSenseID::Samples::SignHelper secure_helper;
#ifdef RSID_SECURE
    RealSenseID::FaceAuthenticator authenticator(&secure_helper, device.deviceType);
#else // RSID_SECURE
    RealSenseID::FaceAuthenticator authenticator(device.deviceType);
#endif
    auto status = authenticator.Connect({device.serialPort});
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "connection failed!\n";
        return 1;
    }

    RealSenseID::DeviceConfig device_config;
    device_config.dump_mode = RealSenseID::DeviceConfig::DumpMode::CroppedFace;

    status = authenticator.SetDeviceConfig(device_config);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "failed to apply device settings!\n";

        authenticator.Disconnect();
        return 1;
    }

    EmptyAuthenticationCallback authentication_callback;
    authenticator.Authenticate(authentication_callback);

    std::cout << "starting preview...\n";

    preview.StartPreview(preview_callback);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "sending authentication request, snapshots should arrive if a face is detected\n";
    authenticator.Authenticate(authentication_callback);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    preview.StopPreview();
}
