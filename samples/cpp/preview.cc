// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/Preview.h"
#include "RealSenseID/DiscoverDevices.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdio>

class PreviewRender : public RealSenseID::PreviewImageReadyCallback
{
public:
    void OnPreviewImageReady(const RealSenseID::Image& image)
    {
        std::cout << "frame #" << image.number << ": " << image.width << "x" << image.height << " (" << image.size << " bytes)"
                  << std::endl;
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

    using namespace RealSenseID;
    PreviewConfig p_conf;
    p_conf.deviceType = device.deviceType;
    p_conf.cameraNumber = device.cameraNumber;
    RealSenseID::Preview preview(p_conf);
    PreviewRender image_clbk;

    bool success = preview.StartPreview(image_clbk);
    std::cout << "run preview for 30 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(30));
    success = preview.StopPreview();
}
