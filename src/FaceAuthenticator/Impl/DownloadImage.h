// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2026 RealSense, Inc. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RealSenseID
{
namespace Impl
{
static constexpr unsigned int MAX_RAW_IMG_SIZE = 1500 * 1024; // 800x600x3 bytes
static constexpr unsigned int MAX_COMPRESSED_IMG_SIZE = MAX_RAW_IMG_SIZE / 2;

struct DownloadImage
{
    std::vector<unsigned char> buffer; // decoded RGB24 output (stbi_load_from_memory returns RGB order)
    unsigned int w = 0;                // decoded width (set after Decode)
    unsigned int h = 0;                // decoded height (set after Decode)
    unsigned int ts = 0;
    unsigned int last_chunk = 0;

    void OnStart(uint32_t total_compressed_size, uint32_t timestamp);
    bool AddChunk(unsigned short chunk_n, unsigned char* data, size_t size);
    bool Decode();

private:
    std::vector<unsigned char> compressed_buffer_; // accumulated compressed data
};
} // namespace Impl
} // namespace RealSenseID
