// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "LibUVCCapture.h"
#include "Logger.h"
#include <vector>
#include <cerrno>
#include <sstream>
#include <iomanip>
#include <memory>
#include <numeric>
#ifdef __ANDROID__
#include <android/api-level.h>
#include <libusb/libusb.h>
#endif
#include "libuvc/libuvc.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>

// Enable for debugging. Noisy.
// #define RSID_DEBUG_UVC 1

namespace RealSenseID
{
namespace Capture
{
static const char* LOG_TAG = "LibUVCCapture";
constexpr int MAX_CAM_INDEX {100};

static std::vector<std::pair<std::string, std::string>> SupportedDevices()
{
    return {{"04d8", "00dd"}, {"2aad", "6373"}, {"414c", "6578"}};
}

static void ThrowIfFailed(const char* call, uvc_error_t status)
{
    if (status != UVC_SUCCESS)
    {
        std::stringstream err_stream;
        err_stream << call << "(...) failed with: " << uvc_strerror(status);
        throw std::runtime_error(err_stream.str().c_str());
    }
}

struct ContextWrapper
{
    uvc_context_t* ctx {nullptr};

    ContextWrapper() : ctx()
    {
#ifdef __ANDROID__
        libusb_context* usb_context = nullptr;

        // For some reason, in API level 23 (AOSP 6.0) uvc_init should be called with
        // NULL usb_context, while in later API levels we tested, weak authority was required
        // to be set.
        // This condition was added to fulfill the different requirements.
        // As more API levels will be tested this condition might change.
        if (android_get_device_api_level() != 23)
        {
            int ret = libusb_set_option(usb_context, LIBUSB_OPTION_WEAK_AUTHORITY);
            if (0 < ret)
            {
                std::stringstream err_stream;
                err_stream << LOG_TAG << " - ERROR in libusb_set_option: " << ret;
                throw std::runtime_error(err_stream.str());
            }
        }

        // TODO: Try this?
        // From the docs
        //     The method libusb_set_option(&ctx, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, NULL) does not affect the ctx.
        //     It allows initializing libusb on unrooted Android devices by skipping the device enumeration.

        // And finally init
        ThrowIfFailed("uvc_init", uvc_init(&ctx, NULL));
#else
        ThrowIfFailed("uvc_init", uvc_init(&ctx, nullptr));
#endif
    }

    ~ContextWrapper()
    {
        if (ctx)
        {
            LOG_DEBUG(LOG_TAG, "Destroying UVC context");
            uvc_exit(ctx);
        }
    }
};

class UVCStreamer
{
public:
    UVCStreamer(int camera_number, const StreamAttributes& stream_attributes);

    ~UVCStreamer();

    uvc_frame_t* Read() const;

    void SetStopping(const bool stopping)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _stopping.store(stopping);
    }

private:
    StreamAttributes _stream_attributes;
    ContextWrapper _ctx_wrapper;

    void OpenAndStartStream();
    void CloseStream();
    void OpenDevice();

    void CheckDevice() const;

    mutable std::mutex _mutex;
    uvc_device_handle_t* _dev_handle {nullptr};
    uvc_stream_handle_t* _stream_handle {nullptr};
    int _camera_index {0};
    std::atomic<bool> _stopping;
};

void UVCStreamer::OpenDevice()
{
    _stopping.store(false);
#ifdef __ANDROID__
    // On Android, _camera_index is actually the file descriptor
    ThrowIfFailed("uvc_wrap", uvc_wrap(_camera_index, _ctx_wrapper.ctx, &_dev_handle));
#else // __ANDROID__
    uvc_device_t** list {nullptr};
    uvc_device_t* dev {nullptr};
    try
    {
        ThrowIfFailed("uvc_get_device_list", uvc_get_device_list(_ctx_wrapper.ctx, &list));

        int idx = 0;
        while ((dev = list[idx]) != nullptr)
        {
            if (_camera_index == idx)
            {
                break;
            }
            idx++;
        }

        if (!dev)
        {
            throw std::runtime_error("[OpenDevice] Requested camera index is out of range.");
        }
        ThrowIfFailed("uvc_open", uvc_open(dev, &_dev_handle));
    }
    catch (std::exception&)
    {
        if (list)
        {
            uvc_free_device_list(list, 1);
        }
        throw;
    }
    if (list)
    {
        uvc_free_device_list(list, 1);
    }
#endif
}

void UVCStreamer::CheckDevice() const
{
    const auto dev = uvc_get_device(_dev_handle);
    uvc_device_descriptor_t* desc {nullptr};
    bool supported {false};
    std::exception_ptr ex_ptr {nullptr};

    try
    {
        ThrowIfFailed("uvc_get_device_descriptor", uvc_get_device_descriptor(dev, &desc));

        for (const auto& device : SupportedDevices())
        {
            const auto expected_vid = std::stoul(device.first, nullptr, 16);
            const auto expected_pid = std::stoul(device.second, nullptr, 16);
            if (desc->idVendor == expected_vid && desc->idProduct == expected_pid)
            {
                supported = true;
                break;
            }
        }
    }
    catch (const std::exception&)
    {
        ex_ptr = std::current_exception();
    }

    // Cleanup
    if (dev)
    {
        uvc_unref_device(dev);
    }
    if (desc)
    {
        uvc_free_device_descriptor(desc);
    }

    // Rethrow exception if caught
    if (ex_ptr)
    {
        std::rethrow_exception(ex_ptr);
    }

    if (!supported)
    {
        throw std::runtime_error("Device at index " + std::to_string(_camera_index) + " is *not* supported.");
    }
    else
    {
        LOG_DEBUG(LOG_TAG, "Device at index %d is recognized and supported.", _camera_index);
    }
}

UVCStreamer::UVCStreamer(int camera_number, const StreamAttributes& stream_attributes) :
    _stream_attributes(stream_attributes), _camera_index(camera_number)
{
    // On Android, camera_number is actually the file descriptor. So we don't check it.
#ifndef __ANDROID__
    if (_camera_index < 0 || _camera_index > MAX_CAM_INDEX)
    {
        throw std::runtime_error("[UVCStreamer] Requested camera index is out of range!");
    }
#endif

    try
    {
        OpenAndStartStream();
    }
    catch (const std::exception& ex)
    {
        // Retry once on transient open failures (e.g. device temporarily busy).
        LOG_WARNING(LOG_TAG, "UVC open failed (%s). Retrying.", ex.what());
        CloseStream();
        if (_dev_handle != nullptr)
        {
            uvc_close(_dev_handle);
            _dev_handle = nullptr;
        }
        // Retry — if it fails again the exception propagates to the caller.
        OpenAndStartStream();
    }
}

void UVCStreamer::OpenAndStartStream()
{
    uvc_stream_ctrl_t ctrl;

    OpenDevice();
#ifdef RSID_DEBUG_UVC
    uvc_print_diag(_dev_handle, stderr);
#endif
    CheckDevice();

    uvc_error_t status = UVC_ERROR_OTHER;
    if (_stream_attributes.format == MJPEG)
    {
        LOG_DEBUG(LOG_TAG, "Request capture format: MJPEG");
        std::vector<int> fps_range {30, 15, 14, 13}; // libuvc FPS range for MJPEG

        for (auto fps : fps_range)
        {
            status = uvc_get_stream_ctrl_format_size(_dev_handle, &ctrl, UVC_FRAME_FORMAT_MJPEG, static_cast<int>(_stream_attributes.width),
                                                     static_cast<int>(_stream_attributes.height), fps);
            if (status == UVC_SUCCESS)
            {
                LOG_INFO(LOG_TAG, "uvc_get_stream_ctrl_format_size: Found requested format at %d fps", fps);
                break;
            }
        }
    }
    else if (_stream_attributes.format == RAW)
    {
        LOG_DEBUG(LOG_TAG, "Request capture format: RAW/W10");
        std::vector<int> fps_range {8, 6, 5, 4}; // libuvc FPS range for RAW/W10

        for (auto fps : fps_range)
        {
            // UVC_FRAME_FORMAT_W10 frame format was added to bundled libuvc-0.0.x-custom
            // Look for comments marked with `RSID_W10` in libuvc for changes made to libuvc
            status = uvc_get_stream_ctrl_format_size(_dev_handle, &ctrl, UVC_FRAME_FORMAT_W10, static_cast<int>(_stream_attributes.width),
                                                     static_cast<int>(_stream_attributes.height), fps);
            if (status == UVC_SUCCESS)
            {
                LOG_INFO(LOG_TAG, "uvc_get_stream_ctrl_format_size: Found requested format at %d fps", fps);
                break;
            }
        }
    }
    else
    {
        LOG_ERROR(LOG_TAG, "Request capture format: UNKNOWN");
        throw std::runtime_error("Unknown format requested!");
    }

    ThrowIfFailed("uvc_get_stream_ctrl_format_size", status);

#ifdef RSID_DEBUG_UVC
    uvc_print_stream_ctrl(&ctrl, stderr);
#endif

    ThrowIfFailed("uvc_stream_open_ctrl", uvc_stream_open_ctrl(_dev_handle, &_stream_handle, &ctrl));

    // The streaming interface is now claimed. Send SET_INTERFACE(streaming_if, alt=0) to reset
    // the bulk IN endpoint and flush any stale frame data left by a previously crashed session.
    // This is safe here — no transfers have been submitted yet — and only affects the UVC
    // streaming interface, not any other interface on the composite device (e.g. serial/ACM).
    LOG_DEBUG(LOG_TAG, "Resetting streaming interface");
    uvc_reset_streaming(_dev_handle);

    ThrowIfFailed("uvc_stream_start", uvc_stream_start(_stream_handle, nullptr, nullptr, 0));

    if (_stream_attributes.format == MJPEG)
    {
        (void)Read(); // Read a single frame to clean up the first noisy frame.
    }
}

void UVCStreamer::CloseStream()
{
    if (_stream_handle != nullptr)
    {
        uvc_stream_close(_stream_handle);
        _stream_handle = nullptr;
    }
}

UVCStreamer::~UVCStreamer()
{
    _stopping.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give Read time to settle

    // Wait for any ongoing Read() operations to complete
    {
        const std::lock_guard<std::mutex> lock(_mutex);
    }

    if (_stream_handle)
    {
        LOG_DEBUG(LOG_TAG, "Stopping stream.");
        // Don't throw in destructor - handle errors gracefully
        uvc_error_t stop_result = uvc_stream_stop(_stream_handle);
        if (stop_result != UVC_SUCCESS)
        {
            LOG_WARNING(LOG_TAG, "uvc_stream_stop failed: %s", uvc_strerror(stop_result));
        }

        uvc_stream_close(_stream_handle);
        _stream_handle = nullptr;

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Give the device time to fully stop streaming
    }

    if (_dev_handle)
    {
        LOG_DEBUG(LOG_TAG, "Closing device handle.");
        uvc_close(_dev_handle);
        _dev_handle = nullptr;
    }
}

uvc_frame_t* UVCStreamer::Read() const
{
    if (_stopping)
    {
        return nullptr;
    }

    uvc_frame_t* frame {nullptr};
    constexpr int32_t wait_usec {100000};
    uvc_error_t ret = uvc_stream_get_frame(_stream_handle, &frame, wait_usec);

    const std::lock_guard<std::mutex> lock(_mutex);
    if (_stopping)
    {
        if (frame != nullptr)
        {
            uvc_free_frame(frame);
        }
        return nullptr;
    }

    if (ret == UVC_SUCCESS && frame != nullptr)
    {
#if RSID_DEBUG_UVC
        LOG_DEBUG(LOG_TAG, "seq = %lu, frame_format = %d, width = %d, height = %d, length = %lu, metadata = %lu", frame->sequence,
                  frame->frame_format, frame->width, frame->height, frame->data_bytes, frame->metadata_bytes);
#endif

        return frame;
    }
    else if (ret != UVC_ERROR_TIMEOUT)
    {
        LOG_ERROR(LOG_TAG, "uvc_stream_get_frame: %s", uvc_strerror(ret));
        return nullptr;
    }

    return nullptr;
}

CaptureHandle::CaptureHandle(const PreviewConfig& config) : _config(config)
{
    _stream_converter = std::make_unique<StreamConverter>(_config);
    try
    {
        StreamAttributes attr = _stream_converter->GetStreamAttributes();
        _uvc_streamer = std::make_unique<UVCStreamer>(_config.cameraNumber, attr);
    }
    catch (const std::exception&)
    {
        _uvc_streamer.reset();
        throw;
    }
}

CaptureHandle::~CaptureHandle()
{
    _uvc_streamer->SetStopping(true);
    _uvc_streamer.reset();
    _stream_converter.reset();
}

bool CaptureHandle::Read(RealSenseID::Image* res) const
{
    bool valid_read {false};

    // read frame
    const auto frame = _uvc_streamer->Read();
    if (frame != nullptr)
    {
        buffer md_buffer;
        buffer frame_buffer;
        frame_buffer.data = static_cast<unsigned char*>(frame->data);
        frame_buffer.size = static_cast<unsigned int>(frame->data_bytes);
        frame_buffer.offset = 0;
        md_buffer.data = static_cast<unsigned char*>(frame->metadata);
        md_buffer.size = static_cast<unsigned int>(frame->metadata_bytes);
        md_buffer.offset = 0;

        if (frame_buffer.size == 0)
        {
            return false;
        }

        StreamAttributes attr = _stream_converter->GetStreamAttributes();
        if (attr.format == RAW && md_buffer.size == 0)
        {
            return false;
        }

        try
        {
            valid_read = _stream_converter->Buffer2Image(res, frame_buffer, md_buffer);
        }
        catch (...)
        {
            valid_read = false;
        }
    }

    return valid_read;
}
} // namespace Capture
} // namespace RealSenseID
