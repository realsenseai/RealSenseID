// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "AndroidSerial.h"
#include "CommonTypes.h"
#include "SerialPacket.h"
#include "Timer.h"
#include "Logger.h"
#include <string>
#include <stdexcept>
#include <thread>
#include <cassert>
#include <android/api-level.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <errno.h>

static constexpr const char* LOG_TAG = "AndroidSerial";

namespace RealSenseID
{
namespace PacketManager
{

constexpr int XIOCTL_RETRIES = 5;

static void ThrowAndroidError(std::string msg)
{
    throw std::runtime_error(msg + ". GetLastError: " + std::to_string(errno));
}

inline int xioctl(int fh, int request, void* arg)
{
    int retries = XIOCTL_RETRIES;
    int retval = -1;

    // clang-format off
    do {
        retval = ioctl(fh, request, arg);
        if (retval >= 0) break;

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // This sleep is intentionally tiny. In most scenarios, it is not even needed.
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    } while (retries-- && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
    // clang-format on
    return retval;
}

AndroidSerial::~AndroidSerial()
{
    // Signal that object is being destroyed
    _is_valid.store(false);

    // Wait for running tasks to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Wait for any pending operations to complete
    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        // Lock and clear buffer to prevent any ongoing operations
        {
            std::lock_guard<std::mutex> lock(_buffer_mutex);
            _internal_buffer.clear();
        }
    }
    catch (...)
    {
    }
}

void AndroidSerial::BufferIncomingData()
{
    if (!_is_valid.load())
    { // Object is being destroyed
        return;
    }

    const size_t read_buffer_size = 16384;
    unsigned char temp_buffer[read_buffer_size];

    int timeout_ms = 100;

    struct usbdevfs_bulktransfer bulk;
    memset(&bulk, 0, sizeof(bulk));
    bulk.ep = _config.readEndpoint;
    bulk.len = read_buffer_size;
    bulk.data = (void*)temp_buffer;
    bulk.timeout = timeout_ms;

    int ioctl_result = xioctl(_config.fileDescriptor, USBDEVFS_BULK, &bulk);
    size_t transferred = static_cast<size_t>(ioctl_result);

    if (ioctl_result > 0 && transferred > 0)
    {
        if (_is_valid.load())
        {
            // Lock buffer_mutex before modifying buffer
            std::lock_guard<std::mutex> lock(_buffer_mutex);

            size_t bytes_received = static_cast<size_t>(transferred);
            _internal_buffer.insert(_internal_buffer.end(), reinterpret_cast<char*>(temp_buffer),
                                    reinterpret_cast<char*>(temp_buffer) + bytes_received);

            DEBUG_SERIAL(LOG_TAG, "[buf]", reinterpret_cast<const char*>(temp_buffer), bytes_received);
        }
    }
}

size_t AndroidSerial::ReadFromInternalBuffer(char* buffer, size_t n_bytes)
{
    if (_is_valid.load())
    {
        std::lock_guard<std::mutex> lock(_buffer_mutex);
        size_t bytes_to_copy = std::min(n_bytes, _internal_buffer.size());
        if (bytes_to_copy > 0)
        {
            std::memcpy(buffer, _internal_buffer.data(), bytes_to_copy);
            _internal_buffer.erase(_internal_buffer.begin(), _internal_buffer.begin() + bytes_to_copy);
        }
        return bytes_to_copy;
    }

    return 0;
}

void AndroidSerial::FlushUsbEndpoint(int fd, unsigned char endpoint, const char* endpoint_name)
{
    // Flush USB endpoint buffers (similar to tcflush for serial ports)

    // This code is disabled at this time. Sending too many USBDEVFS_CLEAR_HALT to the device
    // seems to cause it to crash and reboot. TODO: Review with hardware team.
    //
    // Use USBDEVFS_CLEAR_HALT to clear the endpoint and discard buffered data
    // unsigned int ep = endpoint;
    // int ret = ::ioctl(fd, USBDEVFS_CLEAR_HALT, &ep);
    // if (ret < 0)
    // {
    //     // Log but don't fail - endpoint might not be halted
    //     LOG_DEBUG(LOG_TAG, "[init] Clear halt on %s endpoint (0x%02x) returned %d (errno %d: %s)",
    //               endpoint_name, endpoint, ret, errno, strerror(errno));
    // }
    // else
    // {
    //     LOG_DEBUG(LOG_TAG, "[init] Flushed %s endpoint (0x%02x)", endpoint_name, endpoint);
    // }

    // Attempt to discard any pending data by doing a non-blocking read with short timeout
    if (endpoint & 0x80) // Check if it's an IN endpoint (read)
    {
        unsigned char discard_buffer[4096];
        struct usbdevfs_bulktransfer bulk = {};
        bulk.ep = endpoint;
        bulk.len = sizeof(discard_buffer);
        bulk.timeout = 10;
        bulk.data = discard_buffer;

        // Keep reading until nothing left
        int attempts = 0;
        while (attempts++ < 10) // Max 10 attempts to prevent infinite loop
        {
            int result = ::ioctl(fd, USBDEVFS_BULK, &bulk);
            if (result <= 0)
                break; // No more data or error
            LOG_DEBUG(LOG_TAG, "[init] Discarded %d bytes from %s endpoint", result, endpoint_name);
        }
    }
    else
    {
        auto status = SendBytes("\x03", 1); // Send CTRL+C using escape sequence
        if (status != SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "[drain] failed to send CTRL+C");
        }
        // In case of a message being printed to the console, we need to clear the terminal input buffer
        std::string data(1, '\n'); // \b backspace characters - clear terminal input line buffer
        // data += '\n';                         // append newline
        status = SendBytes(data.c_str(), data.length());
        if (status != SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "[drain] failed to drain write buffer");
        }
    }
}

AndroidSerial::AndroidSerial(const SerialConfig& config) : _config {config}
{
    _is_valid.store(true);

    if (_config.fileDescriptor < 0)
    {
        throw std::runtime_error("[init] Invalid file descriptor: " + std::to_string(_config.fileDescriptor));
    }

    if (_config.readEndpoint == 0 || _config.writeEndpoint == 0)
    {
        throw std::runtime_error("[init] Invalid endpoints - read: 0x" + std::to_string(_config.readEndpoint) + ", write: 0x" +
                                 std::to_string(_config.writeEndpoint));
    }

    // Flush USB endpoints to discard any existing data (similar to tcflush for serial ports)
    FlushUsbEndpoint(_config.fileDescriptor, _config.writeEndpoint, "write");
    FlushUsbEndpoint(_config.fileDescriptor, _config.readEndpoint, "read");

    DEBUG_SERIAL(LOG_TAG, "[init] AndroidSerial initialized successfully");
}

SerialStatus AndroidSerial::SendBytes(const char* buffer, size_t n_bytes)
{
    if (buffer == nullptr)
    {
        LOG_ERROR(LOG_TAG, "[snd] Null buffer provided");
        return SerialStatus::SendFailed;
    }

    if (n_bytes == 0)
    {
        LOG_ERROR(LOG_TAG, "[snd] Attempt to send 0 bytes");
        return SerialStatus::SendFailed;
    }

    DEBUG_SERIAL(LOG_TAG, "[snd] buffer %s", buffer);

    if (!_is_valid.load())
    {
        LOG_ERROR(LOG_TAG, "[snd] Cancelling send. Object is being destroyed.");
        return SerialStatus::SendFailed;
    }

    struct usbdevfs_bulktransfer bulk;
    memset(&bulk, 0, sizeof(bulk));
    bulk.ep = _config.writeEndpoint;
    bulk.len = n_bytes;
    bulk.data = (void*)buffer;
    bulk.timeout = 200 + 4 * n_bytes;
    auto number_of_bytes_sent = xioctl(_config.fileDescriptor, USBDEVFS_BULK, &bulk);

    if (0 > number_of_bytes_sent)
    {
        int error_number = errno;
        LOG_ERROR(LOG_TAG, "[snd] ioctl failed with error: 0x%x", error_number);
        return SerialStatus::SendFailed;
    }

    if (n_bytes != number_of_bytes_sent)
    {
        LOG_ERROR(LOG_TAG, "[snd] Error while writing to port");
        return SerialStatus::SendFailed;
    }
    DEBUG_SERIAL(LOG_TAG, "[snd]", buffer, n_bytes);

    return SerialStatus::Ok;
}

SerialStatus AndroidSerial::RecvBytes(char* buffer, size_t n_bytes)
{
    try
    {
        if (buffer == nullptr)
        {
            LOG_ERROR(LOG_TAG, "[rcv] Null buffer provided");
            return SerialStatus::RecvFailed;
        }

        if (n_bytes == 0)
        {
            LOG_ERROR(LOG_TAG, "[rcv] Attempt to recv 0 bytes");
            return SerialStatus::RecvFailed;
        }

        if (!_is_valid.load())
        {
            // LOG_ERROR(LOG_TAG, "[rcv] Object is being destroyed");
            return SerialStatus::RecvFailed;
        }

        std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            LOG_ERROR(LOG_TAG, "[snd] Failed to acquire lock");
            return SerialStatus::RecvFailed;
        }

        Timer timer {std::chrono::milliseconds {4000 + 5 * n_bytes}};
        unsigned int total_bytes_read = 0;

        while (!timer.ReachedTimeout())
        {
            if (!_is_valid.load())
            {
                LOG_ERROR(LOG_TAG, "[rcv] Object being destroyed during recv");
                return SerialStatus::RecvFailed;
            }

            // First, try to read from internal buffer
            size_t bytes_from_buffer = ReadFromInternalBuffer(buffer + total_bytes_read, n_bytes - total_bytes_read);
            total_bytes_read += static_cast<unsigned int>(bytes_from_buffer);

            if (total_bytes_read == n_bytes)
            {
                return SerialStatus::Ok;
            }

            // Buffer data from USB
            BufferIncomingData();

            // Small delay to avoid busy-waiting
            if (total_bytes_read < n_bytes)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Reached timeout
        if (n_bytes != 1) // Don't log for SyncBytes
        {
            LOG_WARNING(LOG_TAG, "[rcv] Timeout recv %zu bytes. Got only %u bytes", n_bytes, total_bytes_read);
        }

        return SerialStatus::RecvTimeout;
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return SerialStatus::RecvFailed;
    }
}

void AndroidSerial::Clear()
{
    // not implemented
}

} // namespace PacketManager
} // namespace RealSenseID