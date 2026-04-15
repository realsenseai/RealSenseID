// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "CommonTypes.h"

namespace RealSenseID
{
namespace PacketManager
{
// Represents an open serial connection (raii over the os serial connection).
// Should open new connection on construction and close it on destruction.
// Should throw if connection could not be established on construction.
class SerialConnection
{
public:
    virtual ~SerialConnection() = default;

    // send all bytes and return status
    virtual SerialStatus SendBytes(const char* buffer, size_t n_bytes) = 0;

    // receive all bytes and copy to the buffer
    virtual SerialStatus RecvBytes(char* buffer, size_t n_bytes) = 0;

    // clear the serial buffers for this connection
    virtual void Clear() = 0;
};
} // namespace PacketManager
} // namespace RealSenseID
