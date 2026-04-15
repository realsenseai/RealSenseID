// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/SignatureCallback.h"
#include "RealSenseID/FaceAuthenticator.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/asn1.h"
#include "mbedtls/asn1write.h"

//
// Example of user defined signature callbacks.
//
namespace RealSenseID
{
namespace Samples
{
class SignHelper : public RealSenseID::SignatureCallback
{
public:
    SignHelper();
    ~SignHelper();

    // Sign the buffer and copy the signature to the out_sig buffer (64 bytes)
    // Return true on success, false otherwise.
    bool Sign(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig) override;

    // Verify the given buffer and signature
    // Return true on success, false otherwise.
    bool Verify(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig, const unsigned int sign_len) override;

    // Update device's ECDSA public key
    void UpdateDevicePubKey(const unsigned char* pubKey);

    // Get host's ECDSA public key
    const unsigned char* GetHostPubKey() const;

private:
    bool _initialized = false;
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_ecdsa_context _ecdsa_host_context;
    mbedtls_ecdsa_context _ecdsa_device_context;
};
} // namespace Samples
} // namespace RealSenseID
