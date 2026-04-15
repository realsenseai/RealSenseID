// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "WindowsSerial.h"
#include "SerialPacket.h"
#include "CommonTypes.h"
#include "Logger.h"

#include <string>
#include <stdexcept>

static const char* LOG_TAG = "WindowsSerial";

static void ThrowWinError(const std::string& msg)
{
    throw std::runtime_error(msg + ". GetLastError: " + std::to_string(::GetLastError()));
}

namespace RealSenseID
{
namespace PacketManager
{
WindowsSerial::WindowsSerial(const SerialConfig& config) : _config {config}
{
    DCB dcbSerialParams = {0};
    const std::string port = std::string(R"(\\.\)") + _config.port;
    LOG_DEBUG(LOG_TAG, "Opening serial port %s", config.port);
    _handle = ::CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (_handle == INVALID_HANDLE_VALUE)
    {
        ThrowWinError("Failed to open serial port");
    }

    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(_handle, &dcbSerialParams))
    {
        ::CloseHandle(_handle);
        ThrowWinError("Failed to open serial port");
    }
    dcbSerialParams.BaudRate = _config.baudrate;
    dcbSerialParams.ByteSize = _config.bytesize;
    dcbSerialParams.Parity = _config.parity;
    dcbSerialParams.StopBits = _config.stopbits;

    if (!SetCommState(_handle, &dcbSerialParams))
    {
        ::CloseHandle(_handle);
        ThrowWinError("Failed to open serial port");
    }

    COMMTIMEOUTS timeouts = {0};

    // waits 200ms for 1st byte and then proceeds adding 5ms for each bytes received
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.ReadTotalTimeoutMultiplier = 5;
    timeouts.WriteTotalTimeoutConstant = 200;
    timeouts.WriteTotalTimeoutMultiplier = 5;

    if (!SetCommTimeouts(_handle, &timeouts))
    {
        ::CloseHandle(_handle);
        ThrowWinError("Failed to open serial port");
    }

    Clear();
}

WindowsSerial::~WindowsSerial()
{
    try
    {
        LOG_DEBUG(LOG_TAG, "Closing serial port");
        if (_handle == INVALID_HANDLE_VALUE)
        {
            return;
        }
        ::CloseHandle(_handle);
    }
    catch (...)
    {
    }
}

SerialStatus WindowsSerial::SendBytes(const char* buffer, size_t n_bytes)
{
    DWORD bytes_to_write = static_cast<DWORD>(n_bytes);
    DWORD bytes_written = 0;

    DEBUG_SERIAL(LOG_TAG, "[snd]", buffer, n_bytes);

    if (!::WriteFile(_handle, buffer, bytes_to_write, &bytes_written, nullptr) || bytes_written != bytes_to_write)
    {
        LOG_ERROR(LOG_TAG, "Error while writing to serial port");
        return SerialStatus::SendFailed;
    }
    else
    {
        return SerialStatus::Ok;
    }
}

SerialStatus WindowsSerial::RecvBytes(char* buffer, size_t n_bytes)
{
    DWORD bytes_to_read = static_cast<DWORD>(n_bytes);
    DWORD bytes_actual_read = 0;

    if (!::ReadFile(_handle, buffer, bytes_to_read, &bytes_actual_read, nullptr))
    {
        LOG_ERROR(LOG_TAG, "Error while reading from serial port. Last error: %x", ::GetLastError());
        return SerialStatus::RecvFailed;
    }

    if (bytes_actual_read != bytes_to_read)
    {
        if (bytes_to_read != 1)
        {
            // log only if not waiting for sync bytes, where it is expected to timeout sometimes
            LOG_DEBUG(LOG_TAG, "Timeout reading %lu bytes. Got only %lu", bytes_to_read, bytes_actual_read);
        }
        return SerialStatus::RecvTimeout;
    }
    else
    {
        DEBUG_SERIAL(LOG_TAG, "[rcv]", buffer, bytes_actual_read);
        return SerialStatus::Ok;
    }
}

void WindowsSerial::Clear()
{
    // PurgeComm clears the buffers and terminates pending operations
    // flags:
    // PURGE_RXABORT: Terminates all outstanding read operations
    // PURGE_RXCLEAR: Clears the input buffer
    // PURGE_TXABORT: Terminates all outstanding write operations
    // PURGE_TXCLEAR: Clears the output buffer
    if (!::PurgeComm(_handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR))
    {
        LOG_ERROR(LOG_TAG, "Error flushing serial port. Last error: %x", ::GetLastError());
    }
}
} // namespace PacketManager
} // namespace RealSenseID
