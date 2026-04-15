// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"
#include <cstddef>
#include <cstdint>

#pragma pack(push)
#pragma pack(1)

namespace RealSenseID
{
/**
 * Detected face info
 * Upper left corner, width and height
 */
struct RSID_API FaceRect
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
};

/**
 * Detected face landamrks
 * Left eye, right eye, nose tip, mouth left, mouth right
 */
#define NUM_FACE_LANDMARKS 5
struct RSID_API FaceLandmarks
{
    uint32_t lm_x[NUM_FACE_LANDMARKS] = {0};
    uint32_t lm_y[NUM_FACE_LANDMARKS] = {0};
};

/**
 * Detected person info
 */
struct RSID_API PersonRect
{
    enum class BodyPart
    {
        Person = 0,
        Foot = 1,
        Arm = 2,
        Leg = 3,
        Hand = 4,
        Torso = 5
    };

    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t id = 0;
    uint32_t distance = 0;
    BodyPart body_part = BodyPart::Person;
};

#define NUM_POSE_LANDMARKS 17

/**
 * @brief Detected person pose with 17 body keypoints.
 *
 * @details Landmark indices follow the COCO keypoint layout:
 * @verbatim
 *   0  Nose             1  Left Eye         2  Right Eye
 *   3  Left Ear         4  Right Ear
 *   5  Left Shoulder    6  Right Shoulder
 *   7  Left Elbow       8  Right Elbow
 *   9  Left Wrist      10  Right Wrist
 *  11  Left Hip        12  Right Hip
 *  13  Left Knee       14  Right Knee
 *  15  Left Ankle      16  Right Ankle
 * @endverbatim
 */
struct RSID_API PersonPose
{
    uint32_t x = 0;                           // bounding box upper-left x
    uint32_t y = 0;                           // bounding box upper-left y
    uint32_t w = 0;                           // bounding box width
    uint32_t h = 0;                           // bounding box height
    uint32_t lm_x[NUM_POSE_LANDMARKS] = {0};  // landmark x coordinates
    uint32_t lm_y[NUM_POSE_LANDMARKS] = {0};  // landmark y coordinates
    float lm_score[NUM_POSE_LANDMARKS] = {0}; // landmark confidence scores
};

} // namespace RealSenseID

#pragma pack(pop)
