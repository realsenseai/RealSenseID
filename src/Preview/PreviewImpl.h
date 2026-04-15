// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/Preview.h"
#include <memory>
#include <thread>
#include <atomic>

namespace RealSenseID
{
namespace Capture
{
class CaptureHandle;
class RawHelper;
} // namespace Capture

class PreviewImpl
{
public:
    ~PreviewImpl();
    explicit PreviewImpl(const PreviewConfig& config);
    bool StartPreview(PreviewImageReadyCallback& callback);
    bool PausePreview();
    bool ResumePreview();
    bool StopPreview();

private:
    PreviewConfig _config;
    std::thread _worker_thread;
    std::atomic_bool _canceled {false};
    std::atomic_bool _paused {false};
    PreviewImageReadyCallback* _callback = nullptr;
    std::unique_ptr<Capture::CaptureHandle> _capture;
    std::unique_ptr<Capture::RawHelper> _raw_helper;
};
} // namespace RealSenseID
