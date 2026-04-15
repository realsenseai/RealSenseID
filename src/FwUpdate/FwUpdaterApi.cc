// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.


#include "RealSenseID/FwUpdater.h"
#include "F45x/FwUpdaterF45x.h"
#include "F50x/FwUpdaterF50x.h"
#include <stdexcept>

namespace RealSenseID
{

FwUpdater::FwUpdater(DeviceType deviceType)
{
    switch (deviceType)
    {
    case RealSenseID::DeviceType::F45x:
        _impl = new FwUpdateF45x::FwUpdaterF45x();
        break;
    case RealSenseID::DeviceType::F50x:
        _impl = new FwUpdateF50x::FwUpdaterF50x();
        break;
    default:
        throw std::invalid_argument("Unknown device type");
    }
}

bool FwUpdater::ExtractFwInformation(const char* binPath, std::string& outFwVersion, std::string& outRecognitionVersion,
                                     std::vector<std::string>& moduleNames) const
{
    return _impl->ExtractFwInformation(binPath, outFwVersion, outRecognitionVersion, moduleNames);
}


Status FwUpdater::UpdateModules(EventHandler* handler, Settings settings, const char* binPath) const
{
    return _impl->UpdateModules(handler, std::move(settings), binPath);
}

bool FwUpdater::CheckCompatibility(const FwUpdater::Settings& settings, const char* binPath, FwUpdater::FwCompatibilityInfo& info) const
{
    return _impl->CheckCompatibility(settings, binPath, info);
}

bool FwUpdater::IsOtpSkuCompatible(const FwUpdater::Settings& settings, const char* binPath, int& expectedOtpSku, int& deviceOtpSku) const
{
    FwCompatibilityInfo info;
    _impl->CheckCompatibility(settings, binPath, info);
    expectedOtpSku = info.expectedOtpSku;
    deviceOtpSku = info.deviceOtpSku;
    return info.IsOtpSkuCompatible();
}

bool FwUpdater::IsSecureBootCompatible(const FwUpdater::Settings& settings, const char* binPath, int& expectedSecureBoot,
                                       int& deviceSecureBoot) const
{
    FwCompatibilityInfo info;
    _impl->CheckCompatibility(settings, binPath, info);
    expectedSecureBoot = info.expectedSecureBoot;
    deviceSecureBoot = info.deviceSecureBoot;
    return info.IsSecureBootCompatible();
}

bool FwUpdater::IsDbCompatible(const FwUpdater::Settings& settings, const char* binPath, int& expectedDbVer, int& deviceDbVer) const
{
    FwCompatibilityInfo info;
    _impl->CheckCompatibility(settings, binPath, info);
    expectedDbVer = info.expectedDbVer;
    deviceDbVer = info.deviceDbVer;
    return info.IsDbCompatible();
}

bool FwUpdater::IsDeviceTypeCompatible(const FwUpdater::Settings& settings, const char* binPath, int& expectedDeviceType,
                                       int& connectedDeviceType) const
{
    FwCompatibilityInfo info;
    _impl->CheckCompatibility(settings, binPath, info);
    expectedDeviceType = info.expectedDeviceType;
    connectedDeviceType = info.connectedDeviceType;
    return info.IsDeviceTypeCompatible();
}

} // namespace RealSenseID