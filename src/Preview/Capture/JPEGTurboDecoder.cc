// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "JPEGTurboDecoder.h"
#include "Logger.h"
#include "jpeglib.h"
#include <cassert>
#include <cstdio>
#include <sstream>
#include <memory>

namespace RealSenseID
{
namespace Capture
{

static const char* LOG_TAG = "JPEGTurboDecoder";

// throw exception instead of exit on error
static void jpeg_exit_handler(j_common_ptr cinfo)
{
    char msg[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, msg);
    throw std::runtime_error(msg);
}

// use logger for jpeg messages
static void jpeg_output_handler(j_common_ptr cinfo)
{
    char msg[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, msg);
    LOG_WARNING(LOG_TAG, msg);
}

JPEGTurboDecoder::JPEGTurboDecoder()
{
    InitDecompressor();
}

void JPEGTurboDecoder::InitDecompressor()
{
    // cleanup previous decompressor if exists
    if (_decompressor_initialized)
    {
        jpeg_destroy_decompress(&_jpeg_dinfo);
    }
    _jpeg_dinfo.err = jpeg_std_error(&_jpeg_error_mgr);
    _jpeg_error_mgr.output_message = jpeg_output_handler;
    _jpeg_error_mgr.error_exit = jpeg_exit_handler;
    jpeg_create_decompress(&_jpeg_dinfo);
    _decompressor_initialized = true;
}

bool JPEGTurboDecoder::DecodeJpeg(Image* res, buffer frame_buffer, const std::size_t max_height, const std::size_t max_width)
{
    // check if frame starts with valid jpeg bytes before decoding
    if (frame_buffer.size < 2 || frame_buffer.data[0] != 0xFF || frame_buffer.data[1] != 0xD8)
    {
        LOG_DEBUG(LOG_TAG, "Ignoring invalid jpeg frame");
        return false;
    }

    try
    {
        ::jpeg_mem_src(&_jpeg_dinfo, frame_buffer.data, frame_buffer.size);
        auto rc = jpeg_read_header(&_jpeg_dinfo, TRUE);
        if (rc != JPEG_HEADER_OK)
        {
            throw std::runtime_error("Got invalid jpeg frame");
        }

        if (::jpeg_start_decompress(&_jpeg_dinfo) == FALSE)
        {
            throw std::runtime_error("jpeg_start_decompress failed");
        }

        const auto width = _jpeg_dinfo.output_width;
        const auto height = _jpeg_dinfo.output_height;
        if (height > max_height || width > max_width)
        {
            throw std::runtime_error("jpeg decoded dimensions are bigger than expected");
        }

        const auto pixel_size = _jpeg_dinfo.output_components;
        const auto row_stride = width * pixel_size;
        unsigned char* buffer_array[1];
        while (_jpeg_dinfo.output_scanline < _jpeg_dinfo.output_height)
        {
            buffer_array[0] = res->buffer + (_jpeg_dinfo.output_scanline) * row_stride;
            (void)::jpeg_read_scanlines(&_jpeg_dinfo, buffer_array, 1);
        }

        if (!::jpeg_finish_decompress(&_jpeg_dinfo))
        {
            throw std::runtime_error("jpeg_finish_decompress failed");
        }

        res->height = height;
        res->width = width;
        res->stride = row_stride;
        res->size = res->stride * res->height;
        return true;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(LOG_TAG, "DecodeJpeg: %s", ex.what());
        InitDecompressor();
        return false;
    }
}

JPEGTurboDecoder::~JPEGTurboDecoder()
{
    if (_decompressor_initialized)
    {
        jpeg_destroy_decompress(&_jpeg_dinfo);
    }
}

} // namespace Capture
} // namespace RealSenseID