// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include "PacketManager/SerialConnection.h"
#include "RealSenseID/SerialConfig.h"

#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

namespace RealSenseID
{
namespace FwUpdateF50x
{
class FwUpdaterCommF50x
{
public:
    static constexpr size_t ReadBufferSize = 128 * 1024;
    explicit FwUpdaterCommF50x(const SerialConfig& config);
    ~FwUpdaterCommF50x();


    // scan pointer
    char* GetScanPtr() const;
    // set ScanIndex to ReadIndex
    void ConsumeScanned();

    // input buffer (pass end of all read data)
    // throw std::runtime_error if failed
    char* ReadBuffer() const;

    // wait until no more input (100 ms without any new bytes). return index to current data
    // throw std::runtime_error if failed
    size_t WaitForIdle();

    // write bytes to the serial port
    // throw std::runtime_error if failed
    void WriteBinary(const char* buf, size_t n_bytes);

    // 1. Wait until input drained
    // 2. Send the command
    // 3 Waif for cmd "ack" upto 1 second, if wait_response is true
    // throw std::runtime_error if failed
    void WriteCmd(const std::string& cmd, bool wait_response = true);

    // Wait until str appears in the serial input
    // throw std::runtime_error if failed
    void WaitForStr(const char* str, std::chrono::milliseconds timeout);

    // Stop and join the reading thread.
    // Needed to avoid errors while connection is about to be closed befor reboot device
    void StopReaderThread();

    // Save communications from the FW to log file fw-update.log
    void DumpSession(const char* filename);


private:
    std::unique_ptr<PacketManager::SerialConnection> _serial;
    std::thread _reader_thread;
    std::atomic<bool> _should_stop_thread {false};
    std::atomic<size_t> _read_index {0};
    std::atomic<size_t> _scan_index {0};
    std::unique_ptr<char[]> _read_buffer;

    void ReaderThreadLoop();
};
} // namespace FwUpdateF50x
} // namespace RealSenseID
