// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RawHelper.h"
#include <bitset>
#include <cstdint>
#include <cstring>

namespace RealSenseID
{
namespace Capture
{

inline uint8_t Raw8Value(const uint8_t* src, int baseoff, int mod5)
{
    return (mod5 >= 4) ? 0 : *(src + baseoff);
}

void RawHelper::InitBuffer(int width, int height)
{
    if (_result_img.buffer != nullptr)
    {
        delete[] _result_img.buffer;
        _result_img.buffer = nullptr;
    }
    _result_img.width = width;
    _result_img.height = height;
    _result_img.size = width * height * RGB_PIXEL_SIZE;
    _result_img.stride = _result_img.size / height;
    _result_img.buffer = new unsigned char[_result_img.size];
}

RawHelper::~RawHelper()
{
    if (_result_img.buffer != nullptr)
    {
        delete[] _result_img.buffer;
        _result_img.buffer = nullptr;
    }
}
// rotation tools


inline void setBit(unsigned char* byte, bool val, int index)
{
    (*byte) = static_cast<char>(val ? (*byte) | (1 << index) : (*byte) & ~(1 << index));
}

inline bool getBit(unsigned char* byte, int index)
{
    return (*byte) & (1 << index);
}

// get data from the px_num pixel in the array
// returns the 10-bit data in uint16_t.
// evrey 5-th byte in raw10 buffer containes the 2 lsb bits of the 4 former bytes in memory.
static std::bitset<10> get10BitAligned(unsigned char* arr, unsigned int px_num)
{
    std::bitset<10> res(0);
    int position = px_num % 4;
    int main_loc = px_num + (px_num / 4);       // location of the 8 msb bits of the pixel
    int lsb_bits_loc = main_loc + 4 - position; // location of the 2 lsb bits of the pixel
    int bit_ptr_out = 0, bit_ptr_in = position * 2;

    // get 2 lsb bits
    res[bit_ptr_out++] = getBit(arr + lsb_bits_loc, bit_ptr_in++);
    res[bit_ptr_out++] = getBit(arr + lsb_bits_loc, bit_ptr_in++);

    // get other 8 bits
    for (bit_ptr_in = 0; bit_ptr_in < 8; bit_ptr_in++)
        res[bit_ptr_out++] = getBit(arr + main_loc, bit_ptr_in);

    return res;
}

// set data from into the px_num pixel in the array
// returns the 10-bit data in uint16_t.
// evrey 5-th byte in raw10 buffer containes the 2 lsb bits of the 4 former bytes in memory.
static void set10BitAligned(unsigned char* arr, unsigned int px_num, std::bitset<10> data)
{
    int position = px_num % 4;
    int main_loc = px_num + (px_num / 4);       // location of the 8 msb bits of the pixel
    int lsb_bits_loc = main_loc + 4 - position; // location of the 2 lsb bits of the pixel

    int bit_ptr_out = 0, bit_ptr_in = position * 2;

    // set 2 lsb bits
    setBit(arr + lsb_bits_loc, data[bit_ptr_out++], bit_ptr_in++);
    setBit(arr + lsb_bits_loc, data[bit_ptr_out++], bit_ptr_in++);

    // set other 8 bits
    for (bit_ptr_in = 0; bit_ptr_in < 8; bit_ptr_in++)
        setBit(arr + main_loc, data[bit_ptr_out++], bit_ptr_in);
}

Image RawHelper::RotateRaw(Image& src_img)
{
    if (_rotate_raw == false)
        return src_img;
    if (src_img.width != _result_img.width || src_img.height != _result_img.height)
    {
        InitBuffer(src_img.width, src_img.height);
    }
    int n = src_img.height;
    int m = src_img.width;
    auto input = src_img.buffer;
    auto temp = _result_img.buffer;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            set10BitAligned(temp, n * (m - j - 1) + i, get10BitAligned(input, i * m + j));
        }
    }

    ::memcpy(input, temp, src_img.size);
    src_img.height = m;
    src_img.width = n;
    src_img.stride = src_img.size / src_img.height;
    return src_img;
}

Image RawHelper::ConvertToRgb(const Image& src_img)
{
    Image dst_img;
    if (src_img.width != _result_img.width || src_img.height != _result_img.height)
    {
        InitBuffer(src_img.width, src_img.height);
    }
    unsigned int dst_height, dst_width, src_x, src_y, dst_x, dst_y;
    unsigned int src_height = src_img.height, src_width = src_img.width;
    src_x = 0, src_y = 0;

    if (_rotate_rgb == false)
    {
        dst_height = src_height, dst_width = src_width;
        dst_x = 0, dst_y = 0;
    }
    else
    {
        dst_height = src_width, dst_width = src_height;
        dst_x = 0, dst_y = dst_height - 1;
    }

    unsigned int line = src_img.size / src_height;

    const unsigned char* src = src_img.buffer;
    unsigned char* dst = _result_img.buffer;

    unsigned int i;

    for (i = 0; i < src_img.size; i++)
    {
        int p[8];
        uint8_t hn, vn, di;
        uint8_t br0, br1, g;
        int mode;
        int hnext = 1, hprev = 1;

        /* skip every 5th byte */
        switch (i % 5)
        {
        case 0:
            hprev = 2;
            break;
        case 3:
            hnext = 2;
            break;
        case 4:
            continue;
        }

        /* Setup offsets to this pixel's neighbours. */
        p[0] = i - line - hprev;
        p[1] = i - line;
        p[2] = i - line + hnext;
        p[3] = i - hprev;
        p[4] = i + hnext;
        p[5] = i + line - hprev;
        p[6] = i + line;
        p[7] = i + line + hnext;

        /* Swap offsets if they are out of bounds. */
        if (!src_y)
        {
            p[0] = p[5];
            p[1] = p[6];
            p[2] = p[7];
        }
        else if (src_y == src_height - 1)
        {
            p[5] = p[0];
            p[6] = p[1];
            p[7] = p[2];
        }

        if (!src_x)
        {
            p[0] = p[2];
            p[3] = p[4];
            p[5] = p[7];
        }
        else if (src_x == src_width - 1)
        {
            p[2] = p[0];
            p[4] = p[3];
            p[7] = p[5];
        }

        /* Average matching neighbours. */
        hn = static_cast<uint8_t>(((Raw8Value(src, p[3], p[3] % 5) + Raw8Value(src, p[4], p[4] % 5)) / 2));
        vn = static_cast<uint8_t>(((Raw8Value(src, p[1], p[1] % 5) + Raw8Value(src, p[6], p[6] % 5)) / 2));
        di = static_cast<uint8_t>(((Raw8Value(src, p[0], p[0] % 5) + Raw8Value(src, p[2], p[2] % 5) + Raw8Value(src, p[5], p[5] % 5) +
                                    Raw8Value(src, p[7], p[7] % 5))) /
                                  4);

        /* Calculate RGB */
        mode = (src_x + src_y) & 0x01;
        // if (palette != 128) mode = ~(src_x + src_y) & 0x01;

        if (mode)
        {
            g = Raw8Value(src, i, i % 5);
            if (src_y & 0x01)
            {
                br1 = hn;
                br0 = vn;
            }
            else
            {
                br1 = vn;
                br0 = hn;
            }
        }
        else if (src_y & 0x01)
        {
            br1 = Raw8Value(src, i, i % 5);
            g = static_cast<uint8_t>((vn + hn) / 2);
            br0 = di;
        }
        else
        {
            br0 = Raw8Value(src, i, i % 5);
            g = static_cast<uint8_t>((vn + hn) / 2);
            br1 = di;
        }

        unsigned int cur_rgb_pixel_offset = RGB_PIXEL_SIZE * (dst_y * dst_width + dst_x);
        *(dst + cur_rgb_pixel_offset) = br1;
        *(dst + cur_rgb_pixel_offset + 1) = g;
        *(dst + cur_rgb_pixel_offset + 2) = br0;

        if (_rotate_rgb == false)
            dst_x++;
        else
            dst_y--;

        src_x++;

        /* Move to the next line*/
        if (src_x == src_width)
        {
            src_x = 0;
            src_y++;
            if (_rotate_rgb == false)
            {
                dst_y++;
                dst_x = 0;
            }
            else
            {
                dst_y = dst_height - 1;
                dst_x++;
            }
        }
    }

    dst_img.width = dst_width;
    dst_img.height = dst_height;
    dst_img.size = _result_img.size;
    dst_img.buffer = dst;
    dst_img.stride = dst_img.size / dst_img.height;
    dst_img.metadata = src_img.metadata;
    return dst_img;
}
} // namespace Capture
} // namespace RealSenseID