#pragma once

#include "Preprocessing.h"

namespace RealSenseID
{
namespace Pipeline
{

// Convert a BGR image to RGB. Returns an owning Image with the converted data.
Image Bgr2Rgb(const Image& src);

} // namespace Pipeline
} // namespace RealSenseID
