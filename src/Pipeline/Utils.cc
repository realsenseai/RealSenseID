#include "Utils.h"

namespace RealSenseID
{
namespace Pipeline
{

Image Bgr2Rgb(const Image& src)
{
    size_t sz = static_cast<size_t>(src.width) * src.height * 3;
    std::vector<uint8_t> buf(sz);

    const uint8_t* in = src.data();
    uint8_t* out = buf.data();
    for (size_t i = 0; i < sz; i += 3)
    {
        out[i + 0] = in[i + 2];
        out[i + 1] = in[i + 1];
        out[i + 2] = in[i + 0];
    }

    return Image(src.width, src.height, std::move(buf), Image::RGB);
}

} // namespace Pipeline
} // namespace RealSenseID
