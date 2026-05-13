#include "FaceSelector.h"

#include <cmath>
#include <algorithm>
#include <numeric>

namespace RealSenseID
{
namespace Pipeline
{

// Return distance to center for each face.
static void GetDistances(const std::vector<RectD>& rects, double img_cx, double img_cy, std::vector<double>& sizes,
                         std::vector<double>& dists)
{
    for (const RectD& rect : rects)
    {
        double w = rect.x2 - rect.x1;
        double h = rect.y2 - rect.y1;
        sizes.push_back(std::sqrt(w * h));

        double cx = rect.x1 + w / 2.0;
        double cy = rect.y1 + h / 2.0;
        dists.push_back(std::sqrt(std::pow(cx - img_cx, 2) + std::pow(cy - img_cy, 2)));
    }
}

// Returns the indices that would sort the array in ascending order.
template <typename T>
static std::vector<size_t> argsort(const std::vector<T>& array)
{
    std::vector<size_t> indices(array.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&array](size_t left, size_t right) { return array[left] < array[right]; });
    return indices;
}

// Select the best face from multiple detections.
// Priority: large, centered, high-confidence.
//  - Filter to high-quality faces (score > 0.5). If none qualify, return highest-score face.
//  - the largest face is also closest to image center, return it.
//  - If the two candidates (largest vs closest-to-center) differ in score by >5%, pick higher score.
//  - Tiebreaker: compare significance ratios of size and distance — pick whichever metric
//     shows a bigger gap between best and second-best candidate.
RectD FaceSelector::SelectImpl(const std::vector<RectD>& faces, int width, int height)
{
    if (faces.size() == 1)
        return faces[0];

    std::vector<RectD> faces_hq;
    for (const RectD& rect : faces)
    {
        if (rect.score > mHighQualityTh)
            faces_hq.push_back(rect);
    }

    // if there are no hq faces, return the first face (which has the highest score)
    if (faces_hq.empty())
        return faces[0];

    // if there is only one hq face, return it
    if (faces_hq.size() == 1)
        return faces_hq[0];

    std::vector<double> sizes, dists;
    GetDistances(faces_hq, width / 2.0, height / 2.0, sizes, dists);

    // ascending order sorting: min -> max
    std::vector<size_t> dists_sorted_idx = argsort(dists);
    std::vector<size_t> sizes_sorted_idx = argsort(sizes);

    size_t dist_min_idx = dists_sorted_idx[0];
    size_t size_max_idx = sizes_sorted_idx.back();

    if (dist_min_idx == size_max_idx)
        return faces_hq[dist_min_idx];

    // if score difference is higher than min_score_delta, return face with higher score
    double min_dist_score = faces_hq[dist_min_idx].score;
    double max_size_score = faces_hq[size_max_idx].score;
    double score_delta = (min_dist_score - max_size_score) / std::max(max_size_score, min_dist_score);
    if (std::abs(score_delta) > mMinScoreDelta)
    {
        size_t best_idx = (score_delta > 0) ? dist_min_idx : size_max_idx;
        return faces_hq[best_idx];
    }

    // Check which metric (size or distance) yields higher significance
    double dratio = dists[dists_sorted_idx[0]] / dists[dists_sorted_idx[1]];
    double sratio = sizes[sizes_sorted_idx[sizes_sorted_idx.size() - 2]] / sizes[sizes_sorted_idx.back()];

    size_t best_idx = (dratio < sratio) ? dist_min_idx : size_max_idx;
    return faces_hq[best_idx];
}

// Select the best face from multiple detections.
FaceBox FaceSelector::Select(const std::vector<FaceBox>& faces, int width, int height)
{
    if (faces.size() == 0)
        return FaceBox();

    if (faces.size() == 1)
        return faces[0];

    // Convert FaceBox -> RectD and call SelectImpl
    std::vector<RectD> rects;
    rects.reserve(faces.size());
    for (const auto& f : faces)
    {
        rects.emplace_back(static_cast<double>(f.x), static_cast<double>(f.y), static_cast<double>(f.x + f.w),
                           static_cast<double>(f.y + f.h), static_cast<double>(f.score));
    }

    RectD selected = SelectImpl(rects, width, height);

    // Convert RectD → FaceBox
    FaceBox result;
    result.x = static_cast<int>(selected.x1);
    result.y = static_cast<int>(selected.y1);
    result.w = static_cast<int>(selected.x2 - selected.x1);
    result.h = static_cast<int>(selected.y2 - selected.y1);
    result.score = static_cast<float>(selected.score);
    return result;
}

} // namespace Pipeline
} // namespace RealSenseID
