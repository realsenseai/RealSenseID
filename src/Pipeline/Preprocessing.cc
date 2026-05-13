#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "Preprocessing.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <Logger.h>

static const char* LOG_TAG = "Pipeline";

namespace RealSenseID
{
namespace Pipeline
{
// Resize to max_img_size if larger.
// If smaller than min_img_size, resize up to min_img_size. Otherwise, return 1.0 (no resize).
static float GetResizeFactor(int input_w, int input_h, int min_target_size, int max_target_size)
{
    auto img_size = std::max(input_w, input_h);
    if (img_size <= 0)
        return 1.0f;

    if (img_size > max_target_size)
        return float(max_target_size) / float(img_size);
    else if (img_size < min_target_size)
        return float(min_target_size) / float(img_size);
    else
        return 1.0f;
}


PreprocessResult PreprocessForDetection(const Image& image, int min_target_size, int target_size)
{
    // Model expects RGB — convert if needed
    Image rgb_buf;
    const Image& rgb = (image.format == Image::BGR) ? (rgb_buf = Bgr2Rgb(image)) : image;

    int width = rgb.width;
    int height = rgb.height;

    if (width <= 0 || height <= 0 || min_target_size <= 0 || target_size <= 0)
        return {};

    auto scale = GetResizeFactor(width, height, min_target_size, target_size);
    int new_w = static_cast<int>(std::round(width * scale));
    int new_h = static_cast<int>(std::round(height * scale));

    // Clamp to target_size (rounding may overshoot by 1)
    new_w = std::min(new_w, target_size);
    new_h = std::min(new_h, target_size);

    // Resize to new dimensions
    std::vector<uint8_t> resized(new_w * new_h * 3);
    stbir_resize_uint8_linear(rgb.data(), width, height, 0, resized.data(), new_w, new_h, 0, static_cast<stbir_pixel_layout>(STBIR_RGB));

    // Allocate target_size x target_size x 3 float buffer, fill with pad value
    const float pad_value = 127.5f;
    const int total = target_size * target_size * 3;
    std::vector<float> output(total, pad_value);

    // Center the resized image in the canvas
    int pad_left = (target_size - new_w) / 2;
    int pad_top = (target_size - new_h) / 2;

    // Copy resized uint8 pixels into float buffer
    for (int y = 0; y < new_h; ++y)
    {
        const uint8_t* src_row = resized.data() + y * new_w * 3;
        float* dst_row = output.data() + (pad_top + y) * target_size * 3 + pad_left * 3;
        for (int x = 0; x < new_w * 3; ++x)
        {
            dst_row[x] = static_cast<float>(src_row[x]);
        }
    }

    PreprocessResult result;
    result.tensor_data = std::move(output);
    result.pad_left = pad_left;
    result.pad_top = pad_top;
    result.resized_width = new_w;
    result.resized_height = new_h;
    return result;
}

ResizedImage ResizeImage(const Image& image, float scale)
{
    int width = image.width;
    int height = image.height;
    const uint8_t* data = image.data();

    if (width <= 0 || height <= 0 || scale <= 0.0f || data == nullptr)
    {
        LOG_ERROR(LOG_TAG, "ResizeImage: invalid input parameters");
        return {};
    }

    int new_w = static_cast<int>(std::round(width * scale));
    int new_h = static_cast<int>(std::round(height * scale));

    if (new_w <= 0)
        new_w = 1;
    if (new_h <= 0)
        new_h = 1;

    ResizedImage result;
    result.width = new_w;
    result.height = new_h;
    result.data.resize(static_cast<size_t>(new_w) * new_h * 3);

    auto pixel_layout = (image.format == Image::BGR) ? STBIR_BGR : STBIR_RGB;
    if (stbir_resize_uint8_linear(data, width, height, 0, result.data.data(), new_w, new_h, 0, pixel_layout) == 0)
    {
        LOG_ERROR(LOG_TAG, "ResizeImage: stbir_resize_uint8_linear failed");
        return {};
    }
    return result;
}

} // namespace Pipeline
} // namespace RealSenseID
