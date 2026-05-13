// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2026 RealSense, Inc. All Rights Reserved.

#include "DownloadImage.h"
#include "Logger.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"

namespace RealSenseID
{
namespace Impl
{
namespace
{
static const char* LOG_TAG = "DownloadImage";
} // namespace

void DownloadImage::OnStart(uint32_t total_compressed_size, uint32_t timestamp)
{
    compressed_buffer_.clear();
    compressed_buffer_.reserve(total_compressed_size);
    buffer.clear();
    w = 0;
    h = 0;
    ts = timestamp;
    last_chunk = 0;
}

bool DownloadImage::AddChunk(unsigned short chunk_n, unsigned char* data, size_t size)
{
    if (size == 0)
    {
        LOG_ERROR(LOG_TAG, "Got empty chunk");
        return false;
    }
    auto expected_chunk_n = compressed_buffer_.empty() ? 0u : last_chunk + 1;
    if (chunk_n != expected_chunk_n)
    {
        LOG_ERROR(LOG_TAG, "Got invalid chunk number. Expected %u. Got %u", expected_chunk_n, chunk_n);
        return false;
    }

    if (compressed_buffer_.size() + size > MAX_COMPRESSED_IMG_SIZE)
    {
        LOG_ERROR(LOG_TAG, "Max compressed size exceeded");
        return false;
    }

    compressed_buffer_.insert(compressed_buffer_.end(), data, data + size);
    last_chunk = chunk_n;
    return true;
}

bool DownloadImage::Decode()
{
    int img_w = 0, img_h = 0, channels = 0;
    // JPEG-only decode (stb_image built with STBI_ONLY_JPEG)
    unsigned char* decoded =
        stbi_load_from_memory(compressed_buffer_.data(), static_cast<int>(compressed_buffer_.size()), &img_w, &img_h, &channels, 3);

    compressed_buffer_.clear();
    compressed_buffer_.shrink_to_fit();

    if (!decoded || img_w <= 0 || img_h <= 0)
    {
        LOG_ERROR(LOG_TAG, "Image decode failed");
        if (decoded)
            stbi_image_free(decoded);
        return false;
    }

    size_t decoded_size = static_cast<size_t>(img_w) * img_h * 3;
    if (decoded_size > MAX_RAW_IMG_SIZE)
    {
        LOG_ERROR(LOG_TAG, "Decoded image too large: %zu bytes", decoded_size);
        stbi_image_free(decoded);
        return false;
    }

    buffer.assign(decoded, decoded + decoded_size);
    stbi_image_free(decoded);

    w = static_cast<unsigned int>(img_w);
    h = static_cast<unsigned int>(img_h);
    return true;
}

} // namespace Impl
} // namespace RealSenseID
