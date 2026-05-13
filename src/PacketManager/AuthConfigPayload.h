// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

namespace RealSenseID
{
namespace PacketManager
{
// Packed struct matching wire protocol (60 bytes) avoiding alignment/padding issues.
#pragma pack(push, 1)
struct AuthConfigPayload
{
    struct RoiEntry
    {
        uint16_t x;
        uint16_t y;
        uint16_t w;
        uint16_t h;
    };

    uint8_t camera_rotation; /* AuthConfigCore::CameraRotation */
    uint8_t security_level;  /* AuthConfigCore::SecurityLevel */
    uint8_t algo_flow;       /* AuthConfigCore::AlgoFlow */
    uint8_t gpio_auth_toggling;
    uint8_t dump_mode;           /* AuthConfigCore::DumpMode */
    uint8_t frontal_face_policy; /* AuthConfigCore::FrontalFacePolicy */
    uint8_t person_motion_mode;  /* AuthConfigCore::PersonMotionMode */
    uint8_t max_spoofs;
    uint16_t match_thresh;
    uint8_t face_selection_policy; /* AuthConfigCore::FaceSelectionPolicy */
    uint16_t manual_exposure_time_us;
    uint16_t manual_gain;
    uint8_t rect_enable;
    uint8_t landmarks_enable;
    static constexpr uint8_t MAX_ROIS = 5; // must match DeviceConfig::MAX_ROIS
    RoiEntry detection_rois[MAX_ROIS];
    uint8_t distance_limit_cm; /* 0 = no limit, 1-150 = cm */
    uint8_t distance_enabled;
    uint8_t num_rois;
};
#pragma pack(pop)
static_assert(sizeof(AuthConfigPayload) == 60, "AuthConfigPayload size mismatch");
static_assert(sizeof(AuthConfigPayload::RoiEntry) == 8, "RoiEntry size mismatch");
} // namespace PacketManager
} // namespace RealSenseID
