// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "FaceAuthenticatorF45x.h"

namespace RealSenseID
{
namespace Impl
{
FaceAuthenticatorF45x::FaceAuthenticatorF45x(SignatureCallback* callback) : FaceAuthenticatorCommon(callback)
{
}

Status FaceAuthenticatorF45x::DumpAndMount()
{
    return Status::NotSupported;
}

Status FaceAuthenticatorF45x::MountDebug()
{
    return Status::NotSupported;
}

Status FaceAuthenticatorF45x::DetectPersons(const RealSenseID::FaceAuthenticator::PersonCallback& callback, bool loop)
{
    return Status::NotSupported;
}

Status FaceAuthenticatorF45x::DetectPoses(const RealSenseID::FaceAuthenticator::PoseCallback& callback, bool loop)
{
    return Status::NotSupported;
}

Status FaceAuthenticatorF45x::DetectBodyParts(const RealSenseID::FaceAuthenticator::BodyPartCallback& callback, bool loop)
{
    return Status::NotSupported;
}

Status FaceAuthenticatorF45x::DecodeBarcodes(const RealSenseID::FaceAuthenticator::BarcodeCallback& callback, bool loop)
{
    return Status::NotSupported;
}

} // namespace Impl
} // namespace RealSenseID
