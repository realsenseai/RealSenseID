#pragma once

#include "Pipeline.h"
#include <cstdint>
#include <vector>

namespace RealSenseID
{
namespace Pipeline
{

struct PreprocessResult
{
    std::vector<float> tensor_data;
    int pad_left = 0;
    int pad_top = 0;
    int resized_width = 0;
    int resized_height = 0;
};

PreprocessResult PreprocessForDetection(const Image& image, int min_target_size, int target_size);

struct ResizedImage
{
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
};

ResizedImage ResizeImage(const Image& image, float scale);

} // namespace Pipeline
} // namespace RealSenseID
