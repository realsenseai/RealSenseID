// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/RealSenseIDExports.h"
#include "RealSenseID/Version.h"
#include <vector>

namespace RealSenseID
{

struct RSID_API DeviceInfo
{
    static constexpr size_t MaxBufferSize = 256;
    char serialPort[MaxBufferSize] = {};
    DeviceType deviceType = DeviceType::Unknown;
    char serialNumber[MaxBufferSize] = {};
    int cameraNumber = -1; //  0-based capture index, or -1 if not known
};

std::vector<DeviceInfo> RSID_API DiscoverDevices();
std::vector<int> RSID_API DiscoverCapture();
DeviceType RSID_API DiscoverDeviceType(const char* serial_port);
} // namespace RealSenseID
