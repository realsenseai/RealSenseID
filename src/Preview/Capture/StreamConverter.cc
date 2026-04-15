
#include "StreamConverter.h"
#include "Logger.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include "MetadataDefines.h"
#include <cassert>

namespace RealSenseID
{
namespace Capture
{

// f45x resolutions
static constexpr int F450_MJPEG_1080P_WIDTH = 1920;
static constexpr int F450_MJPEG_1080P_HEIGHT = 1056;
static constexpr int F450_MJPEG_720P_WIDTH = 1280;
static constexpr int F450_MJPEG_720P_HEIGHT = 704;

// f50x resolutions
static constexpr int F500_MJPEG_1080P_WIDTH = 1920;
static constexpr int F500_MJPEG_1080P_HEIGHT = 1080;
static constexpr int F500_MJPEG_720P_WIDTH = 1280;
static constexpr int F500_MJPEG_720P_HEIGHT = 720;

// raw10 resolutions
static constexpr int RAW10_1080P_WIDTH = 1920;
static constexpr int RAW10_1080P_HEIGHT = 1080;

// metadata ver
static constexpr uint32_t MD_CAPTURE_INFO_VER = 0x80081005;


static const char* LOG_TAG = "StreamConverter";
static const StreamAttributes GetStreamAttributesByMode(PreviewConfig config)
{
    unsigned int width_1080, width_720, height_1080, height_720;

    switch (config.deviceType)
    {
    case DeviceType::Unknown: // Use F45x device type be default
    case DeviceType::F45x:
        width_1080 = F450_MJPEG_1080P_WIDTH;
        height_1080 = F450_MJPEG_1080P_HEIGHT;
        width_720 = F450_MJPEG_720P_WIDTH;
        height_720 = F450_MJPEG_720P_HEIGHT;
        break;
    case DeviceType::F50x:
        width_1080 = F500_MJPEG_1080P_WIDTH;
        height_1080 = F500_MJPEG_1080P_HEIGHT;
        width_720 = F500_MJPEG_720P_WIDTH;
        height_720 = F500_MJPEG_720P_HEIGHT;
        break;
    default:
        throw std::invalid_argument("Unknown device type");
    }

    switch (config.previewMode)
    {
    case PreviewMode::MJPEG_1080P:
        return config.portraitMode ? StreamAttributes {height_1080, width_1080, MJPEG}  // portrait 1080p
                                   : StreamAttributes {width_1080, height_1080, MJPEG}; // lansscape 1080p
    case PreviewMode::MJPEG_720P:
        return config.portraitMode ? StreamAttributes {height_720, width_720, MJPEG}  // portrait 720p
                                   : StreamAttributes {width_720, height_720, MJPEG}; // landscape 720p
    case PreviewMode::RAW10_1080P:
        return StreamAttributes {RAW10_1080P_WIDTH, RAW10_1080P_HEIGHT, RAW}; // raw10 1080p
    default:
        throw std::invalid_argument("Got non-legal PreviewMode. using Default");
    }
}

static ImageMetadata ExtractMetadataFromMDBuffer(const buffer& buffer, bool to_milli)
{
    ImageMetadata md;
    int divide_ts = to_milli ? 1000 : 1; // from micro to milli

    if (buffer.size < md_middle_level_size || buffer.data == nullptr)
        return md;

    auto tmp_md = reinterpret_cast<md_middle_level*>(buffer.data + buffer.offset);

    if (tmp_md->ver != MD_CAPTURE_INFO_VER)
    {
        LOG_ERROR(LOG_TAG, "Metadata version doesn't match. Expected: %x, found: %x", MD_CAPTURE_INFO_VER, tmp_md->ver);
        return md;
    }

    if (tmp_md->exposure == 0 && tmp_md->gain == 0)
    {
        LOG_DEBUG(LOG_TAG, "Ignoring sync frame (exposure = 0 && gain = 0)");
        return md;
    }

    constexpr unsigned int snapshot_jpeg_attribute = (1u << 7);

    md.timestamp = static_cast<int>(tmp_md->sensor_timestamp / divide_ts);
    md.exposure = tmp_md->exposure;
    md.gain = tmp_md->gain;
    md.led = static_cast<char>(tmp_md->led_status);
    md.sensor_id = tmp_md->sensor_id;
    md.status = tmp_md->status;
    md.is_snapshot = (tmp_md->flags & snapshot_jpeg_attribute) ? 1 : 0;

    // Enable for debugging metadata. (Too noisy)
    /*
    LOG_DEBUG(LOG_TAG, "[Frame metadata] timestamp: %i, exposure: %i, gain: %i, led: %i, "
                       "sensor: %i, status: %i, snapshot: %i",
              md.timestamp, md.exposure, md.gain, md.led, md.sensor_id, md.status, md.is_snapshot);
    */
    return md;
}

// StreamConverter
StreamConverter::StreamConverter(PreviewConfig config) : _portrait_mode(config.portraitMode)
{
#ifdef _WIN32
    _jpeg_decoder = std::make_unique<JPEGWICDecoder>();
#else
    _jpeg_decoder = std::make_unique<JPEGTurboDecoder>();
#endif
    _attributes = GetStreamAttributesByMode(config);
    LOG_INFO(LOG_TAG, "%s %s, %s, %dx%d,", Description(config.deviceType), _attributes.format == MJPEG ? "mjpeg" : "raw10",
             config.portraitMode ? "portrait" : "landscape", _attributes.width, _attributes.height);
    _result_image.width = _attributes.width;
    _result_image.height = _attributes.height;
    _result_image.size = (_attributes.format == MJPEG) ? _result_image.width * _result_image.height * RGB_PIXEL_SIZE
                                                       : (_result_image.width * _result_image.height / 4) * 5;
    _result_image.stride = _result_image.size / _result_image.height;
    _result_image.buffer = new unsigned char[_result_image.size];
    _skip_decode = config.skip_decode;
}

StreamConverter::~StreamConverter()
{
    if (_result_image.buffer != nullptr)
    {
        delete[] _result_image.buffer;
        _result_image.buffer = nullptr;
    }
}

bool StreamConverter::Buffer2Image(Image* res, const buffer& frame_buffer, const buffer& md_buffer)
{
    *res = _result_image;
    switch (_attributes.format) // process image by mode
    {
    case MJPEG:
        try
        {
            res->metadata = ExtractMetadataFromMDBuffer(md_buffer, true /* convert to millis */);
            if (!_skip_decode)
            {
                return _jpeg_decoder->DecodeJpeg(res, frame_buffer, _result_image.height, _result_image.width);
            }
            else
            {
                // when not decoding, just copy the jpeg buffer as is
                // encoded jpeg buffer should be smaller than our allocated raw rgb buffer size
                assert(frame_buffer.size <= res->size);
                auto copy_size = static_cast<size_t>(std::min(res->size, frame_buffer.size));
                ::memcpy(res->buffer, frame_buffer.data, copy_size);
                res->size = static_cast<unsigned int>(copy_size);
                res->width = _result_image.width;
                res->height = _result_image.height;
                res->stride = 0; // jpeg buffer has no stride
                return true;
            }
        }
        catch (const std::exception& ex)
        {
            LOG_WARNING(LOG_TAG, "Buffer2Image: %s", ex.what());
            return false;
        }
        break;
    case RAW:
        res->metadata = ExtractMetadataFromMDBuffer(md_buffer, false /* keep in micros */);
        if (res->metadata.timestamp == 0) // don't return non-dumped images
        {
            LOG_DEBUG(LOG_TAG, "Frame timestamp = 0. Discarded frame.");
            return false;
        }
        ::memcpy(res->buffer, frame_buffer.data, frame_buffer.size);
        return true;
        break;
    default:
        LOG_ERROR(LOG_TAG, "Unsupported preview mode");
        return false;
    }
}

StreamAttributes StreamConverter::GetStreamAttributes()
{
    return _attributes;
}

} // namespace Capture
} // namespace RealSenseID
