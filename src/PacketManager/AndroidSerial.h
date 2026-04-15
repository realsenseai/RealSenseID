// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once
#include "SerialConnection.h"
#include <vector>
#include <thread>
#include <mutex>

namespace RealSenseID
{
namespace PacketManager
{
class AndroidSerial : public SerialConnection
{
public:
    explicit AndroidSerial(const SerialConfig& config);
    ~AndroidSerial();

    // prevent copy or assignment
    AndroidSerial(const AndroidSerial&) = delete;
    void operator=(const AndroidSerial&) = delete;

    // send all bytes and return status
    SerialStatus SendBytes(const char* buffer, size_t n_bytes) final;

    // receive all bytes and copy to the buffer
    SerialStatus RecvBytes(char* buffer, size_t n_bytes) final;

    // clear the serial buffers
    void Clear() final;

private:
    void BufferIncomingData();
    size_t ReadFromInternalBuffer(char* buffer, size_t n_bytes);
    void FlushUsbEndpoint(int fd, unsigned char endpoint, const char* endpoint_name);

    SerialConfig _config;

    std::vector<char> _internal_buffer;
    mutable std::mutex _mutex;
    mutable std::mutex _buffer_mutex;
    std::atomic<bool> _is_valid {true};
};

} // namespace PacketManager
} // namespace RealSenseID