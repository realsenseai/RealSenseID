// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "StreamConverter.h"

namespace RealSenseID
{
namespace Capture
{
class RawHelper
{
public:
    RawHelper(bool rotate_raw, bool portrait_mode) : _rotate_raw(rotate_raw & portrait_mode), _rotate_rgb(portrait_mode) {};
    ~RawHelper();
    Image ConvertToRgb(const Image& src_img);
    Image RotateRaw(Image& src_img);

private:
    void InitBuffer(int width, int height);
    Image _result_img;
    bool _rotate_raw;
    bool _rotate_rgb;
};


} // namespace Capture
} // namespace RealSenseID