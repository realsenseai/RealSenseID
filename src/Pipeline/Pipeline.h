#pragma once

#include "FaceBox.h"
#include "RealSenseID/RealSenseIDExports.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace RealSenseID
{
namespace Pipeline
{

// Image with two construction modes:
// - Non-owning view (from raw pointer): no copy, caller must keep the pointer valid.
// - Owning (from std::vector): takes ownership of the buffer via move.
struct Image
{
    int width = 0;
    int height = 0;
    enum Format
    {
        BGR,
        RGB
    } format = BGR;


    Image() = default;

    // Non-owning view — caller must keep ptr alive for the lifetime of this Image
    Image(int w, int h, const uint8_t* ptr, Format fmt) : width {w}, height {h}, format {fmt}, _view {ptr}
    {
        if (w <= 0 || h <= 0)
            throw std::runtime_error("Image: invalid dimensions");

        if (ptr == nullptr)
            throw std::runtime_error("Image: null buffer pointer");
    }

    // Owning — takes ownership of buf
    Image(int w, int h, std::vector<uint8_t> buf, Format fmt) : width {w}, height {h}, format {fmt}, _buffer {std::move(buf)}
    {
        if (w <= 0 || h <= 0)
            throw std::runtime_error("Image: invalid dimensions");
    }

    const uint8_t* data() const
    {
        return _view ? _view : _buffer.data();
    }

private:
    std::vector<uint8_t> _buffer;   // owned buffer (if constructed with vector)
    const uint8_t* _view = nullptr; // non-owning view (if constructed with raw pointer)
};

enum class FacePolicy
{
    SingleFace, // Allow only single face
    MultiFace,  // Select best face if multiple detected
};

enum class FaceResult
{
    Success,
    NoFaceDetected,
    MultipleFacesDetected,
    FaceTooFarToTheTop,
    FaceTooFarToTheBottom,
    FaceTooFarToTheRight,
    FaceTooFarToTheLeft
};

// Detect the best face and return its bounding box. Uses FaceSelector when multiple faces are detected.
// When expand_roi is true, the returned box is expanded by 40% on each side (clamped to image bounds).
RSID_API bool DetectFace(const Image& image, FaceBox& result, bool expand_roi);

// Detect face, validate (reject if too close to edge),
// scale down if the face is large, expand ROI by 25% per side, and return the cropped region.
RSID_API FaceResult DetectAndCropFace(const Image& image, Image& result, FacePolicy policy);

} // namespace Pipeline
} // namespace RealSenseID
