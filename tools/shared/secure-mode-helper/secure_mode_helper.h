// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "secure_mode_helper_export.h"
#include "rsid_c/rsid_client.h"

#ifdef __cplusplus
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
namespace Examples
{
class RSID_SIG_EXAMPLE_API SignHelper : public RealSenseID::SignatureCallback
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
    // Exchanges public keys between the host and the device and saves
    // The device public key for later use (calls UpdateDevicePubKey())
    Status ExchangeKeys(FaceAuthenticator* faceAuthenticator);

private:
    bool _initialized = false;
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_ecdsa_context _ecdsa_host_context;
    mbedtls_ecdsa_context _ecdsa_device_context;
};
} // namespace Examples
} // namespace RealSenseID

extern "C"
{
#endif //__cplusplus
    /*
     * c example implementation of user defined signature callbacks.
     * See examples/rsid_c_example on usage.
     */

    /* Create sig callback struct and return pointer to it*/
    RSID_SIG_EXAMPLE_API rsid_signature_clbk* rsid_create_example_sig_clbk();

    /* Release sig callback struct */
    RSID_SIG_EXAMPLE_API void rsid_destroy_example_sig_clbk(rsid_signature_clbk* clbk);

    /*
     * Called to sign the buffer before sending to the device.
     * Should return 1 on success, 0 otherwise.
     */
    RSID_SIG_EXAMPLE_API int rsid_sign_example(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig,
                                               void* ctx);

    /*
     * Called to verify buffer received from the device.
     * Should return 1 if the given buffer and signature match, 0 otherwise.
     */
    RSID_SIG_EXAMPLE_API int rsid_verify_example(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig,
                                                 const unsigned int siglen, void* ctx);
    /* Retrieve hosts public key*/
    RSID_SIG_EXAMPLE_API const unsigned char* rsid_get_host_pubkey_example(rsid_signature_clbk* clbk);

    /* Store device's public key so we can use it later */
    RSID_SIG_EXAMPLE_API void rsid_update_device_pubkey_example(rsid_signature_clbk* clbk, const unsigned char* pubkey);

#ifdef RSID_SECURE
    /* Create pairing args helper */
    RSID_SIG_EXAMPLE_API rsid_pairing_args* rsid_create_pairing_args_example(rsid_signature_clbk* args);

    /* Destroy pairing args helper */
    RSID_SIG_EXAMPLE_API void rsid_destroy_pairing_args_example(rsid_pairing_args* args);
#endif // RSID_SECURE

#ifdef __cplusplus
}
#endif //__cplusplus
