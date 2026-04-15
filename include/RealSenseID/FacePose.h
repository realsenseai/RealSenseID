// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * Required face poses to complete the enrollment operation.
 */
enum class RSID_API FacePose
{
    Center,
    Up,
    Down,
    Left,
    Right
};

/**
 * Return c string description of the status
 *
 * @param status to describe.
 */
RSID_API const char* Description(FacePose status);

template <typename OStream>
inline OStream& operator<<(OStream& os, const FacePose& pose)
{
    os << Description(pose);
    return os;
}
} // namespace RealSenseID
