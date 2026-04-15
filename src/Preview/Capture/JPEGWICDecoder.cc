// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "JPEGWICDecoder.h"
#include "Logger.h"
#include <Wincodec.h>
#include <sstream>
#include <mutex>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")

namespace RealSenseID
{
namespace Capture
{

static const char* LOG_TAG = "JPEGWICDecoder";

JPEGWICDecoder::JPEGWICDecoder()
{
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&_IWICFactory))))
    {
        throw std::runtime_error("Failed to create IWICImagingFactory");
    }
}

JPEGWICDecoder::~JPEGWICDecoder()
{
    if (_IWICFactory)
    {
        _IWICFactory->Release();
    }
}


#define RESOURCE_CLEANUP                                                                                                                   \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (stream)                                                                                                                        \
            stream->Release();                                                                                                             \
        if (bitmap_decoder)                                                                                                                \
            bitmap_decoder->Release();                                                                                                     \
        if (frame_decode)                                                                                                                  \
            frame_decode->Release();                                                                                                       \
        if (format_converter)                                                                                                              \
            format_converter->Release();                                                                                                   \
        if (bitmap)                                                                                                                        \
            bitmap->Release();                                                                                                             \
        if (bitmap_lock)                                                                                                                   \
            bitmap_lock->Release();                                                                                                        \
    } while ((void)0, 0)

#define RETURN_FALSE_IF_FAILED(what, hr)                                                                                                   \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (FAILED(hr))                                                                                                                    \
        {                                                                                                                                  \
            std::stringstream err_stream;                                                                                                  \
            err_stream << (what) << " failed. HR: " << std::hex << static_cast<unsigned long>(hr);                                         \
            LOG_ERROR("DecodeJpeg", err_stream.str().c_str());                                                                             \
            RESOURCE_CLEANUP;                                                                                                              \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while ((void)0, 0)

bool JPEGWICDecoder::DecodeJpeg(Image* res, buffer frame_buffer, const size_t max_height, const size_t max_width) const
{
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* bitmap_decoder = nullptr;
    IWICBitmapFrameDecode* frame_decode = nullptr;
    IWICFormatConverter* format_converter = nullptr;
    IWICBitmap* bitmap = nullptr;
    IWICBitmapLock* bitmap_lock = nullptr;


    RETURN_FALSE_IF_FAILED("Create stream", _IWICFactory->CreateStream(&stream));

    RETURN_FALSE_IF_FAILED("Init stream", stream->InitializeFromMemory(static_cast<unsigned char*>(frame_buffer.data), frame_buffer.size));

    RETURN_FALSE_IF_FAILED("Create bitmap decoder",
                           _IWICFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &bitmap_decoder));

    RETURN_FALSE_IF_FAILED("Get frame from decoder", bitmap_decoder->GetFrame(0, &frame_decode));

    RETURN_FALSE_IF_FAILED("Create format converter", _IWICFactory->CreateFormatConverter(&format_converter));

    RETURN_FALSE_IF_FAILED("Init format converter",
                           format_converter->Initialize(frame_decode, GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone, nullptr, 0.0f,
                                                        WICBitmapPaletteTypeCustom));

    RETURN_FALSE_IF_FAILED("Create bitmap", _IWICFactory->CreateBitmapFromSource(format_converter, WICBitmapCacheOnDemand, &bitmap));

    unsigned int width, height;
    RETURN_FALSE_IF_FAILED("Get bitmap size", bitmap->GetSize(&width, &height));

    if (height > max_height || width > max_width)
    {
        LOG_ERROR(LOG_TAG, "jpeg decoded dimensions are bigger than expected");
        RESOURCE_CLEANUP;
        return false;
    }

    WICRect rect = {0, 0, static_cast<int>(width), static_cast<int>(height)};
    RETURN_FALSE_IF_FAILED("bitmap_lock bitmap", bitmap->Lock(&rect, WICBitmapLockRead, &bitmap_lock));

    unsigned int bitmap_data_size = 0;
    unsigned char* bitmap_data = nullptr;
    RETURN_FALSE_IF_FAILED("Get data pointer", bitmap_lock->GetDataPointer(&bitmap_data_size, &bitmap_data));
    if (bitmap_data == nullptr)
    {
        LOG_ERROR(LOG_TAG, "Got null bitmap_data");
        RESOURCE_CLEANUP;
        return false;
    }

    unsigned int stride;
    RETURN_FALSE_IF_FAILED("Get stride", bitmap_lock->GetStride(&stride));
    ::memcpy(res->buffer, bitmap_data, res->size < bitmap_data_size ? res->size : bitmap_data_size);
    res->width = width;
    res->height = height;
    res->stride = stride;

    RESOURCE_CLEANUP;
    return true;
}


} // namespace Capture
} // namespace RealSenseID