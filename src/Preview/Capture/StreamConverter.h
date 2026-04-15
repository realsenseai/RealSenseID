#pragma once
#include "RealSenseID/Preview.h"
#include <memory>
#include <functional>
#ifdef _WIN32
#include "JPEGWICDecoder.h"
#else
#include "JPEGTurboDecoder.h"
#endif

namespace RealSenseID
{
namespace Capture
{

static constexpr int RGB_PIXEL_SIZE = 3;

enum StreamFormat
{
    MJPEG,
    RAW
};

struct StreamAttributes
{
    unsigned int width = 0;
    unsigned int height = 0;
    StreamFormat format = MJPEG;
};

class StreamConverter
{
public:
    explicit StreamConverter(PreviewConfig config);
    ~StreamConverter();
    bool Buffer2Image(Image* res, const buffer& frame_buffer, const buffer& metadata_buffer);
    StreamAttributes GetStreamAttributes();

private:
    StreamAttributes _attributes;
    Image _result_image;
    bool _portrait_mode;
    bool _skip_decode;
#ifdef _WIN32
    std::unique_ptr<JPEGWICDecoder> _jpeg_decoder = nullptr;
#else
    std::unique_ptr<JPEGTurboDecoder> _jpeg_decoder = nullptr;
#endif
};
} // namespace Capture
} // namespace RealSenseID
