// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "SerialConnection.h"
#include <windows.h>

namespace RealSenseID
{
namespace PacketManager
{
class WindowsSerial : public SerialConnection
{
public:
    explicit WindowsSerial(const SerialConfig& config);
    ~WindowsSerial() override;

    WindowsSerial(const WindowsSerial&) = delete;
    WindowsSerial(const WindowsSerial&&) = delete;
    void operator=(const WindowsSerial&) = delete;
    void operator=(const WindowsSerial&&) = delete;

    // send all bytes and return status
    SerialStatus SendBytes(const char* buffer, size_t n_bytes) final;

    // receive all bytes and copy to the buffer
    SerialStatus RecvBytes(char* buffer, size_t n_bytes) final;

    // clear the serial buffers
    void Clear() final;

private:
    SerialConfig _config;
    HANDLE _handle = INVALID_HANDLE_VALUE;
};
} // namespace PacketManager
} // namespace RealSenseID
