// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "PreviewImpl.h"
#include "RawHelper.h"
#include "Logger.h"
#include "RealSenseID/DiscoverDevices.h"

#ifdef _WIN32
#include "MSMFCapture.h"
#else
#ifdef RSID_LIBUVC
#include "LibUVCCapture.h"
#else
#include "V4L2Capture.h"
#endif
#endif


#include <chrono>
#include <stdexcept>
#include <system_error>

static const char* LOG_TAG = "Preview";

namespace RealSenseID
{
PreviewImpl::PreviewImpl(const PreviewConfig& config) : _config(config)
{
    if (config.cameraNumber == -1) // auto detection
    {
        std::vector<int> camera_numbers;
        try
        {
            camera_numbers = DiscoverCapture();
        }
        catch (const std::exception& ex)
        {
            throw std::runtime_error(std::string("UVC device detection failed:") + ex.what());
        }
        if (camera_numbers.empty())
        {
            throw std::runtime_error(std::string("UVC device detection failed: No UVC devices found."));
        }
        _config.cameraNumber = camera_numbers[0];
    }
}

PreviewImpl::~PreviewImpl()
{
    try
    {
        StopPreview();
    }
    catch (...)
    {
    }
}

bool PreviewImpl::StartPreview(PreviewImageReadyCallback& callback)
{
    _callback = &callback;

    _paused = false;
    _canceled = false;

    if (_worker_thread.joinable())
    {
        return false;
    }

    // RAW10/W10 is only supported with libuvc, not V4L2
#ifndef _WIN32
#ifndef RSID_LIBUVC
    if (_config.previewMode == PreviewMode::RAW10_1080P)
    {
        LOG_ERROR(LOG_TAG, "RAW10/W10 preview mode is not supported with the V4L2 backend");
        return false;
    }
#endif // RSID_LIBUVC
#endif // _WIN32

    _worker_thread = std::thread([&]() {
        try
        {
            _capture = std::make_unique<Capture::CaptureHandle>(_config);
            if (_config.previewMode == PreviewMode::RAW10_1080P)
                _raw_helper = std::make_unique<Capture::RawHelper>(_config.rotateRaw, _config.portraitMode);
            unsigned int frameNumber = 0;
            LOG_DEBUG(LOG_TAG, "Preview started!");
            while (!_canceled)
            {
                if (_paused)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds {100});
                    continue;
                }
                RealSenseID::Image container;
                bool res = _capture->Read(&container);
                if (_canceled)
                {
                    break;
                }
                if (_paused)
                {
                    continue;
                }
                if (res)
                {
                    container.number = frameNumber++;
                    if (container.metadata.is_snapshot)
                    {
                        LOG_DEBUG(LOG_TAG, "Received snapshot frame. timestamp=%u  sensor=%d  status=%u  snapshot=%d",
                                  container.metadata.timestamp, container.metadata.sensor_id, container.metadata.status,
                                  container.metadata.is_snapshot);
                    }
                    if (_config.previewMode == PreviewMode::RAW10_1080P)
                    {
                        // send preview image even if is snapshot to facilitate preview of snapshots in w10 format
                        _callback->OnPreviewImageReady(_raw_helper->ConvertToRgb(container));
                        if (container.metadata.is_snapshot)
                            _callback->OnSnapshotImageReady(_raw_helper->RotateRaw(container));
                    }
                    else
                    {
                        if (container.metadata.is_snapshot)
                            _callback->OnSnapshotImageReady(container);
                        else
                            _callback->OnPreviewImageReady(container);
                    }
                }
                else
                {
                    // Not result yet. retry shortly
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR(LOG_TAG, "Streaming ERROR : %s", ex.what());
            _canceled = true;
        }
        catch (...)
        {
            LOG_ERROR(LOG_TAG, "Streaming unknown exception");
            _canceled = true;
        }
        _capture.reset();
    });
    return true;
}

bool PreviewImpl::PausePreview()
{
    _paused = true;
    return true;
}

bool PreviewImpl::ResumePreview()
{
    _paused = false;
    return true;
}

bool PreviewImpl::StopPreview()
{
    _canceled = true;
    if (_worker_thread.joinable())
    {
        _worker_thread.join();
    }
    _capture.reset();

    return true;
}

} // namespace RealSenseID
