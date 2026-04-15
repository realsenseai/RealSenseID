// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2024 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/Preview.h"
#include "StreamConverter.h"
#include <memory>

namespace RealSenseID
{
namespace Capture
{

class V4L2Streamer;

class CaptureHandle
{
public:
    explicit CaptureHandle(const PreviewConfig& config);
    ~CaptureHandle();
    bool Read(RealSenseID::Image* res) const;

    // prevent copy or assignment
    // only single connection is allowed to a capture device.
    CaptureHandle(const CaptureHandle&) = delete;
    void operator=(const CaptureHandle&) = delete;

private:
    std::unique_ptr<StreamConverter> _stream_converter;
    std::unique_ptr<V4L2Streamer> _v4l2_streamer;
    PreviewConfig _config;
};
} // namespace Capture
} // namespace RealSenseID
