#include "Pipeline.h"
#include "Inference.h"
#include "FaceSelector.h"
#include "Preprocessing.h"
#include "Logger.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

static const char* LOG_TAG = "Pipeline";

static constexpr int FD_INPUT_SIZE = 512;
static constexpr int FD_MIN_INPUT_SIZE = 200;
static constexpr float FD_SCORE_THRESHOLD = 0.3f;
static constexpr int FD_MIN_FACE_SIZE = 20;
static constexpr int EDGE_MARGIN = 5;
static constexpr int MAX_FACE_DIM = 160;
static constexpr float ROI_EXPAND = 0.40f;

namespace RealSenseID
{
namespace Pipeline
{

// Clamp face box to image bounds. Result always has non-negative width/height.
static FaceBox ClampToImage(const FaceBox& box, const Image& image)
{
    int x0 = std::clamp(box.x, 0, image.width);
    int y0 = std::clamp(box.y, 0, image.height);
    int x1 = std::clamp(x0 + box.w, x0, image.width);
    int y1 = std::clamp(y0 + box.h, y0, image.height);
    return {x0, y0, x1 - x0, y1 - y0, box.score};
}

// Expand face box by ROI_EXPAND fraction on each side and clamp to image bounds.
static FaceBox ExpandAndClampRoi(const FaceBox& face, const Image& image)
{
    float fw = static_cast<float>(face.w);
    float fh = static_cast<float>(face.h);
    FaceBox expanded;
    expanded.x = static_cast<int>(static_cast<float>(face.x) - fw * ROI_EXPAND);
    expanded.y = static_cast<int>(static_cast<float>(face.y) - fh * ROI_EXPAND);
    expanded.w = static_cast<int>(fw * (1.0f + 2.0f * ROI_EXPAND));
    expanded.h = static_cast<int>(fh * (1.0f + 2.0f * ROI_EXPAND));
    expanded.score = face.score;
    return ClampToImage(expanded, image);
}

static std::vector<FaceBox> DetectFaces(const Image& image)
{
    // Preprocess: ToRgb + resize + pad to FD_INPUT_SIZE x FD_INPUT_SIZE
    auto preprocessed = PreprocessForDetection(image, FD_MIN_INPUT_SIZE, FD_INPUT_SIZE);
    if (preprocessed.tensor_data.empty())
    {
        LOG_ERROR(LOG_TAG, "DetectFaces: preprocessing failed (invalid image?)");
        return {};
    }

    // Run inference
    auto faces_detected =
        Inference::RunFaceDetect(preprocessed.tensor_data.data(), preprocessed.tensor_data.size(), FD_INPUT_SIZE, FD_INPUT_SIZE);
    if (faces_detected.empty())
    {
        return {};
    }

    // Convert detection coordinates from model output space to the original image coords
    float original_w = static_cast<float>(image.width);
    float original_h = static_cast<float>(image.height);
    float resized_w = static_cast<float>(preprocessed.resized_width);
    float resized_h = static_cast<float>(preprocessed.resized_height);
    int pad_left = preprocessed.pad_left;
    int pad_top = preprocessed.pad_top;

    std::vector<FaceBox> faces;
    for (const auto& det : faces_detected)
    {
        if (det.score < FD_SCORE_THRESHOLD)
        {
            break; // sorted descending, can stop
        }

        float x1 = (det.x1 * FD_INPUT_SIZE - pad_left) / resized_w * original_w;
        float y1 = (det.y1 * FD_INPUT_SIZE - pad_top) / resized_h * original_h;
        float x2 = (det.x2 * FD_INPUT_SIZE - pad_left) / resized_w * original_w;
        float y2 = (det.y2 * FD_INPUT_SIZE - pad_top) / resized_h * original_h;

        FaceBox box;
        box.x = static_cast<int>(x1);
        box.y = static_cast<int>(y1);
        box.w = static_cast<int>(x2 - x1);
        box.h = static_cast<int>(y2 - y1);
        box.score = det.score;
        const FaceBox result = ClampToImage(box, image);

        if (result.w >= FD_MIN_FACE_SIZE && result.h >= FD_MIN_FACE_SIZE)
        {
            faces.push_back(result);
        }
    }

    return faces;
}

bool DetectFace(const Image& image, FaceBox& result, bool expand_roi)
{
    auto faces = DetectFaces(image);
    if (faces.empty())
        return false;

    result = FaceSelector::Select(faces, image.width, image.height);

    if (expand_roi)
        result = ExpandAndClampRoi(result, image);

    return true;
}

// Returns Success if face is not too close to any edge, or the specific edge violation.
static FaceResult CheckFaceEdgeMargin(const FaceBox& face, int image_w, int image_h)
{
    if (face.x < EDGE_MARGIN)
        return FaceResult::FaceTooFarToTheLeft;
    if (face.y < EDGE_MARGIN)
        return FaceResult::FaceTooFarToTheTop;
    if ((face.x + face.w + EDGE_MARGIN) > image_w)
        return FaceResult::FaceTooFarToTheRight;
    if ((face.y + face.h + EDGE_MARGIN) > image_h)
        return FaceResult::FaceTooFarToTheBottom;
    return FaceResult::Success;
}

// Algorithm:
// 1. Run face detection. Return false if none found.
// 2. Reject if multiple faces found.
// 3. Reject if face bbox is within EDGE_MARGIN pixels of image boundary.
// 4. Expand ROI by ROI_EXPAND fraction of face size on each side (e.g. 0.25 = 25% padding per side).
// 5. Clamp ROI to image bounds.
// 6. Crop the ROI into a new buffer.
// 7. If min(face.w, face.h) > MAX_FACE_DIM, scale down only the cropped ROI
//    so the smaller face dimension equals MAX_FACE_DIM.
// 8. Return cropped (and possibly resized) Image with owned buffer.
FaceResult DetectAndCropFace(const Image& image, Image& result, FacePolicy policy)
{
    auto faces = DetectFaces(image);
    if (faces.empty())
    {
        return FaceResult::NoFaceDetected;
    }

    auto face = faces[0];

    if (faces.size() > 1)
    {
        if (policy == FacePolicy::SingleFace)
        {
            LOG_ERROR(LOG_TAG, "Multiple %zu faces detected, rejecting (SingleFace policy)", faces.size());
            return FaceResult::MultipleFacesDetected;
        }
        face = FaceSelector::Select(faces, image.width, image.height);
        LOG_DEBUG(LOG_TAG, "Multiple %zu faces detected, selecting face at (%d,%d,%d,%d)", faces.size(), face.x, face.y, face.w, face.h);
    }


    // Reject if face too close to edge
    auto edgeCheck = CheckFaceEdgeMargin(face, image.width, image.height);
    if (edgeCheck != FaceResult::Success)
    {
        LOG_ERROR(LOG_TAG, "Face too close to image edge (%d)", static_cast<int>(edgeCheck));
        return edgeCheck;
    }

    // Expand ROI and clamp to image bounds
    auto roi = ExpandAndClampRoi(face, image);

    // Crop ROI row by row into a contiguous buffer
    const uint8_t* src_data = image.data();
    size_t row_bytes = static_cast<size_t>(roi.w) * 3;
    std::vector<uint8_t> cropped(row_bytes * roi.h);
    for (int y = 0; y < roi.h; ++y)
    {
        const uint8_t* src = src_data + (static_cast<size_t>(roi.y + y) * image.width + roi.x) * 3;
        std::memcpy(cropped.data() + static_cast<size_t>(y) * row_bytes, src, row_bytes);
    }

    Image cropped_image(roi.w, roi.h, std::move(cropped), image.format);
    // Scale down only the cropped ROI if face > MAX_FACE_DIM
    int min_face_dim = std::min(face.w, face.h);
    if (min_face_dim > MAX_FACE_DIM)
    {
        const auto scale = static_cast<float>(MAX_FACE_DIM) / static_cast<float>(min_face_dim);
        auto scaled = ResizeImage(cropped_image, scale);
        result = Image(scaled.width, scaled.height, std::move(scaled.data), image.format);
    }
    else
    {
        result = std::move(cropped_image);
    }

    return FaceResult::Success;
}

} // namespace Pipeline
} // namespace RealSenseID
