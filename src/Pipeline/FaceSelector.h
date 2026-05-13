#pragma once

#include "FaceBox.h"
#include <vector>

namespace RealSenseID
{
namespace Pipeline
{

struct RectD
{
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
    double score = 0;

    RectD(double x1, double y1, double x2, double y2, double score) : x1(x1), y1(y1), x2(x2), y2(y2), score(score)
    {
    }
};

class FaceSelector
{
public:
    static FaceBox Select(const std::vector<FaceBox>& faces, int width, int height);

private:
    static RectD SelectImpl(const std::vector<RectD>& faces, int width, int height);
    static constexpr double mHighQualityTh = 0.5;
    static constexpr double mMinScoreDelta = 0.05;
};

} // namespace Pipeline
} // namespace RealSenseID
