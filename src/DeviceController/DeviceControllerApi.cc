// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/DeviceController.h"
#include "DeviceControllerImpl.h"

namespace RealSenseID
{
DeviceController::DeviceController(DeviceType device_type) : _impl {new DeviceControllerImpl(device_type)}
{
}

DeviceController::~DeviceController()
{
    try
    {
        delete _impl;
    }
    catch (...)
    {
    }
    _impl = nullptr;
}

// Move constructor
DeviceController::DeviceController(DeviceController&& other) noexcept
{
    _impl = other._impl;
    other._impl = nullptr;
}

// Move assignment
DeviceController& DeviceController::operator=(DeviceController&& other) noexcept
{
    if (this != &other)
    {
        delete _impl;
        _impl = other._impl;
        other._impl = nullptr;
    }
    return *this;
}


Status DeviceController::Connect(const SerialConfig& config)
{
    return _impl->Connect(config);
}

void DeviceController::Disconnect()
{
    _impl->Disconnect();
}

Status DeviceController::Reboot()
{
    return _impl->Reboot();
}

Status DeviceController::QueryFirmwareVersion(std::string& version)
{
    return _impl->QueryFirmwareVersion(version);
}

Status DeviceController::QuerySerialNumber(std::string& serial)
{
    return _impl->QuerySerialNumber(serial);
}

Status DeviceController::QueryOtpVersion(uint8_t& version)
{
    return _impl->QueryOtpVersion(version);
}

Status DeviceController::Ping()
{
    return _impl->Ping();
}

Status DeviceController::FetchLog(std::string& log)
{
    return _impl->FetchLog(log);
}

Status DeviceController::GetTemperature(float& soc, float& board)
{
    return _impl->GetTemperature(soc, board);
}

Status DeviceController::GetColorGains(int& red, int& blue)
{
    return _impl->GetColorGains(red, blue);
}

Status DeviceController::SetColorGains(int red, int blue)
{
    return _impl->SetColorGains(red, blue);
}

Status DeviceController::QueryBspVer(std::string& bspver)
{
    return _impl->QueryBspVer(bspver);
}

} // namespace RealSenseID