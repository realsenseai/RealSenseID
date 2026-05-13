// License: Apache 2.0. See LICENSE file in root directory.
// Shared helpers for DiscoverDevices_Win32.cc and DiscoverDevices_Linux.cc.

#pragma once

#include "RealSenseID/DiscoverDevices.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace RealSenseID
{

// Maps VID/PID pair to DeviceType
struct DeviceDescriptor
{
    const std::string vid;
    const std::string pid;
    const DeviceType deviceType = DeviceType::Unknown;
};

// Known RealSenseID VID/PID pairs (must be uppercase hex)
static const std::vector<DeviceDescriptor> ExpectedVidPidPairs {/*{"04D8", "00DD", DeviceType::F45x},// debug channel - CMD*/
                                                                {"2AAD", "6373", DeviceType::F45x},
                                                                /* {"414C", "6666", DeviceType::F50x}, // debug channel - CMD*/
                                                                {"414C", "6578", DeviceType::F50x}};

static std::string ToUpper(const std::string& s)
{
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

// Check if given VID/PID matches one of the known RealSenseID descriptors.
// VID/PID are uppercased before comparison so callers don't need to normalize.
static bool MatchToExpectedVidPidPairs(const std::string& vid, const std::string& pid, DeviceType& deviceType)
{
    deviceType = DeviceType::Unknown;
    auto upper_vid = ToUpper(vid);
    auto upper_pid = ToUpper(pid);

    for (const auto& expected : ExpectedVidPidPairs)
    {
        if (upper_vid == expected.vid && upper_pid == expected.pid)
        {
            deviceType = expected.deviceType;
            return true;
        }
    }
    return false;
}

} // namespace RealSenseID
