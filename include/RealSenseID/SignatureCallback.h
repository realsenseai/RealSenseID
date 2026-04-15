// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * User defined callback for signing and verifying buffer.
 */
class RSID_API SignatureCallback
{
public:
    virtual ~SignatureCallback() = default;

    /**
     * Called to sign buffer.
     * Signing is done with host's ECDSA private key.
     *
     * @param[in] buffer Buffer to sign.
     * @param[in] bufferLen Length of buffer.
     * @param[out] outSig Output signature (64 bytes).
     * @return True if succeeded and false otherwise.
     */
    virtual bool Sign(const unsigned char* buffer, const unsigned int bufferLen, unsigned char* outSig) = 0;

    /**
     * Called to verify the buffer and the given signature.
     * Verification is done with device's ECDSA public key.
     *
     * @param[in] buffer Signed buffer.
     * @param[in] bufferLen Length of buffer.
     * @param[in] sig Buffer signature.
     * @param[in] sigLen Length of signature.
     * @return True if succeeded and verified, false otherwise.
     */
    virtual bool Verify(const unsigned char* buffer, const unsigned int bufferLen, const unsigned char* sig, const unsigned int sigLen) = 0;
};
} // namespace RealSenseID
