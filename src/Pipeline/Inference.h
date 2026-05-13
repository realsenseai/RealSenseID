#pragma once

#include <cstddef>
#include <vector>

namespace RealSenseID
{
namespace Pipeline
{

class Inference
{
public:
    struct DetectedFace
    {
        float x1, y1, x2, y2, score;
    };

    // Run face detection on a preprocessed float tensor (NHWC layout, 3 channels).
    // Returns raw detections in normalized coordinates.
    static std::vector<DetectedFace> RunFaceDetect(float* input_data, size_t count, int width, int height);

#ifdef __ANDROID__
    // Set the AAssetManager for loading model files from APK assets.
    // Must be called before any inference on Android.
    static void SetAssetManager(void* asset_manager);
#endif
};

} // namespace Pipeline
} // namespace RealSenseID
