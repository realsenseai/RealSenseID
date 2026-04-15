// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "FaceAuthenticatorCommon.h"

namespace RealSenseID
{
namespace Impl
{
class FaceAuthenticatorF45x : public FaceAuthenticatorCommon
{
public:
    explicit FaceAuthenticatorF45x(SignatureCallback* callback = nullptr);
    Status DumpAndMount() override;
    Status MountDebug() override;
    Status DetectPersons(const RealSenseID::FaceAuthenticator::PersonCallback& callback, bool loop) override;
    Status DetectPoses(const RealSenseID::FaceAuthenticator::PoseCallback& callback, bool loop) override;
    Status DetectBodyParts(const RealSenseID::FaceAuthenticator::BodyPartCallback& callback, bool loop) override;
    Status DecodeBarcodes(const RealSenseID::FaceAuthenticator::BarcodeCallback& callback, bool loop) override;
};

} // namespace Impl
} // namespace RealSenseID
