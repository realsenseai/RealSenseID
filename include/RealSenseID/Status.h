// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * Serial communication statuses.
 */
enum class RSID_API Status
{
    Ok = 100,            /* Operation succeeded */
    Error,               /* Operation failed */
    SerialError,         /* Error communication on the serial line */
    SecurityError,       /* Error during secure session protocol */
    VersionMismatch,     /* Version mismatch between host and device */
    CrcError,            /* CRC error indicates packet corruption */
    TooManySpoofs,       /* Too many consecutive spoof attempts. Need to call unlock() API call */
    NotSupported,        /* The operation is not supported for this device */
    DatabaseFull,        /* Cannot add user to a full database */
    DuplicateUserId,     /* Cannot add an already existing user Id to the database */
    DuplicateFaceprints, /* Cannot add user with already existing faceprints to the database */
    InvalidSettings      /* The command cannot be executed with the specified settings */
};


/**
 * Return c string description of the status
 *
 * @param status to describe.
 */
RSID_API const char* Description(Status status);

template <typename OStream>
inline OStream& operator<<(OStream& os, const Status& status)
{
    os << Description(status);
    return os;
}
} // namespace RealSenseID
