// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#pragma once

#include "JPEGDecoder.h"
#include "RealSenseID/Preview.h"
#include <cstddef>
#include <cstdio>
#include "jpeglib.h"

namespace RealSenseID
{
namespace Capture
{

class JPEGTurboDecoder
{
public:
    JPEGTurboDecoder();
    ~JPEGTurboDecoder();
    bool DecodeJpeg(Image* res, buffer frame_buffer, std::size_t max_height, std::size_t max_width);

private:
    void InitDecompressor();
    bool _decompressor_initialized = false;
    jpeg_decompress_struct _jpeg_dinfo {};
    jpeg_error_mgr _jpeg_error_mgr {};
};
} // namespace Capture
} // namespace RealSenseID
