// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.

// Sample: continuously detect faces from the camera preview using the host-side pipeline.
// Prints the detected face rectangle for each frame.

#include "RealSenseID/Preview.h"
#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/FaceRect.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdio>

static const int DURATION_SECONDS = 60;

class DetectFaceRenderer : public RealSenseID::PreviewImageReadyCallback
{
public:
    explicit DetectFaceRenderer(RealSenseID::FaceAuthenticator& authenticator) : _authenticator(authenticator)
    {
    }

    void OnPreviewImageReady(const RealSenseID::Image& image) override
    {
        RealSenseID::FaceRect rect;
        auto status = _authenticator.DetectFace(image.buffer, image.width, image.height, rect, false);
        if (status == RealSenseID::Status::Ok)
        {
            // Preview image is not mirrored — flip x for display
            unsigned int mirrored_x = image.width - rect.x - rect.w;
            printf("frame #%u: face at (%u, %u) %ux%u\n", image.number, mirrored_x, rect.y, rect.w, rect.h);
        }
        else
        {
            printf("frame #%u: no face detected\n", image.number);
        }
    }

private:
    RealSenseID::FaceAuthenticator& _authenticator;
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

    RealSenseID::FaceAuthenticator authenticator(device.deviceType);

    RealSenseID::PreviewConfig p_conf;
    p_conf.deviceType = device.deviceType;
    p_conf.cameraNumber = device.cameraNumber;
    RealSenseID::Preview preview(p_conf);

    DetectFaceRenderer callback(authenticator);

    if (!preview.StartPreview(callback))
    {
        std::cout << "Failed to start preview" << std::endl;
        return 1;
    }

    std::cout << "Running face detection for " << DURATION_SECONDS << " seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(DURATION_SECONDS));

    preview.StopPreview();
    std::cout << "Done." << std::endl;
    return 0;
}
