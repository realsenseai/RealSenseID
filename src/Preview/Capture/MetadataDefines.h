// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#pragma pack(push)
#pragma pack(1)

namespace RealSenseID
{
namespace Capture
{

struct uvc_header
{
    uint8_t length;
    uint8_t info;
    uint32_t timestamp;
    uint8_t source_clock[6];
};

typedef struct
{
    uint64_t header;
    uint32_t ver;
    uint32_t flags;
    uint32_t frame_count;
    uint64_t sensor_timestamp;
    uint32_t exposure;
    uint16_t gain;
    uint8_t led_status;
    uint8_t projector_status;
    uint8_t preset_id;
    uint8_t sensor_id; // (left =0 , right = 1)
    uint8_t status;    // enroll status
    uint8_t extra[32];
} md_middle_level;

constexpr uint8_t md_middle_level_size = sizeof(md_middle_level);

} // namespace Capture
} // namespace RealSenseID

#pragma pack(pop)