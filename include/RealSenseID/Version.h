// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"
#include <string>

#define RSID_VER_MAJOR 3
#define RSID_VER_MINOR 6
#define RSID_VER_PATCH 1
#define RSID_VERSION   (RSID_VER_MAJOR * 10000 + RSID_VER_MINOR * 100 + RSID_VER_PATCH)

// Compatible firmware versions for found on F4xx devices
// Major in device must be == _VER_MAJOR defined here
// Minor in device must be >= _VER_MINOR defined here

// F450
#define RSID_FW45x_VER_MAJOR 9
#define RSID_FW45x_VER_MINOR 16

// F500
#define RSID_FW46x_VER_MAJOR 2
#define RSID_FW46x_VER_MINOR 16

namespace RealSenseID
{
enum class DeviceType
{
    Unknown,
    F45x,
    F50x
};

RSID_API const char* Description(DeviceType deviceType);

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceType& deviceType)
{
    os << Description(deviceType);
    return os;
}

/**
 * Library version as semver string
 */
RSID_API const char* Version();

/**
 * Compatible firmware version as semver string (major and minor only)
 */
RSID_API const char* CompatibleFirmwareVersion(DeviceType device);

/**
 * Checks if the firmware version is compatible with the host.
 *
 * @param[in] device String containing firmware version.
 * @param[in] fw_version String containing firmware version.
 * @return True if compatible and false otherwise.
 */
RSID_API bool IsFwCompatibleWithHost(DeviceType device, const std::string& fw_version);

} // namespace RealSenseID
