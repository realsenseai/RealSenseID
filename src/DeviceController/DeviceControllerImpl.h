// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/Status.h"
#include "RealSenseID/Version.h"
#include "PacketManager/SerialConnection.h"

#include <memory>

namespace RealSenseID
{
class DeviceControllerImpl
{
public:
    explicit DeviceControllerImpl(DeviceType device_type);
    ~DeviceControllerImpl() = default;

    DeviceControllerImpl(const DeviceControllerImpl&) = delete;
    DeviceControllerImpl& operator=(const DeviceControllerImpl&) = delete;

    Status Connect(const SerialConfig& config);
    void Disconnect();

    Status Reboot();
    Status QueryFirmwareVersion(std::string& version);
    Status QuerySerialNumber(std::string& serial);
    Status QueryOtpVersion(uint8_t& otpVer);
    Status Ping();
    Status FetchLog(std::string& log);
    Status GetTemperature(float& soc, float& board);
    Status GetColorGains(int& red, int& blue);
    Status SetColorGains(int red, int blue);
    Status QueryBspVer(std::string& bspver);

private:
    std::unique_ptr<PacketManager::SerialConnection> _serial;
    DeviceType _deviceType = DeviceType::Unknown; // will be determined after connection
};
} // namespace RealSenseID
