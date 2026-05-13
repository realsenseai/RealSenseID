// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2026-present RealSense Inc. . All Rights Reserved.

// Multi-camera sample: discover all connected RealSense ID devices,
// then create an Authenticator + Preview pair for each one.
//
// Each device is uniquely identified by its serial number (DeviceInfo::serialNumber),
// which can be used to map a specific camera to a specific role (e.g. "entrance", "exit").

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/Preview.h"
#include "RealSenseID/DiscoverDevices.h"

#include <cstdio>

using namespace RealSenseID;

int main()
{
    // Discover all connected devices
    auto devices = DiscoverDevices();
    if (devices.empty())
    {
        printf("No devices detected\n");
        return 1;
    }

    for (size_t i = 0; i < devices.size(); i++)
        printf("Detected [%s] port: %s  S/N: %s\n", Description(devices[i].deviceType), devices[i].serialPort, devices[i].serialNumber);

    // Create an Authenticator + Preview object for each detected device
    for (auto& device : devices)
    {
        FaceAuthenticator authenticator(device.deviceType);
        Preview preview(PreviewConfig {device.deviceType, device.cameraNumber});
    }

    return 0;
}
