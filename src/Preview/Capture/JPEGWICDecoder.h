// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "JPEGDecoder.h"
#include "RealSenseID/Preview.h"
#pragma once

struct IWICImagingFactory;

namespace RealSenseID
{
namespace Capture
{

class JPEGWICDecoder
{
public:
    JPEGWICDecoder();
    ~JPEGWICDecoder();
    JPEGWICDecoder(const JPEGWICDecoder&) = delete;
    JPEGWICDecoder& operator=(const JPEGWICDecoder&) = delete;
    bool DecodeJpeg(Image* res, buffer frame_buffer, size_t max_height, size_t max_width) const;

private:
    IWICImagingFactory* _IWICFactory = nullptr;
};

} // namespace Capture
} // namespace RealSenseID