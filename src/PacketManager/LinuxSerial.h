// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "SerialConnection.h"

namespace RealSenseID
{
namespace PacketManager
{
class LinuxSerial : public SerialConnection
{
public:
    explicit LinuxSerial(const SerialConfig& config);
    ~LinuxSerial() override;

    LinuxSerial(const LinuxSerial&) = delete;
    LinuxSerial(const LinuxSerial&&) = delete;
    LinuxSerial operator=(const LinuxSerial&) = delete;
    LinuxSerial operator=(const LinuxSerial&&) = delete;

    // send all bytes and return status
    SerialStatus SendBytes(const char* buffer, size_t n_bytes) final;

    // receive all bytes and copy to the buffer
    SerialStatus RecvBytes(char* buffer, size_t n_bytes) final;

    // clear the serial buffers
    void Clear() final;

private:
    SerialConfig _config;
    int _handle = -1;
};
} // namespace PacketManager
} // namespace RealSenseID
