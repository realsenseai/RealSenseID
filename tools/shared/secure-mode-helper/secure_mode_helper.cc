// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "secure_mode_helper.h"
#include "mbedtls/sha256.h"

#include <cstring>
#include <RealSenseID/FaceAuthenticator.h>

#define SHA_256_DIGEST_SIZE_BYTES 32
#define PRI_KEY_SIZE              32
#define PUB_X_Y_SIZE              32
#define PUB_KEY_SIZE              64

// Example of host's private key. Should be replaced in production with your own secret key
static unsigned char SAMPLE_HOST_PRI_KEY[PRI_KEY_SIZE] = {0xb9, 0xad, 0xfe, 0x0e, 0x6d, 0xd4, 0xfb, 0x6f, 0x76, 0xdf, 0x53,
                                                          0x92, 0x87, 0x4e, 0x58, 0x39, 0xdd, 0x51, 0xd1, 0xaa, 0x79, 0x94,
                                                          0x5e, 0xa8, 0x36, 0x8f, 0xb5, 0xdf, 0xa8, 0x28, 0x26, 0x53};

// Example of host's public key. Should be replaced in production with your own key
static unsigned char SAMPLE_HOST_PUB_KEY[PUB_KEY_SIZE] = {
    0xa9, 0x19, 0xcd, 0x93, 0x0f, 0xfb, 0x3e, 0x95, 0x5e, 0xf2, 0x94, 0xa5, 0x90, 0xca, 0x0e, 0x82, 0x19, 0x08, 0x72, 0x23, 0x8d, 0xec,
    0x49, 0x97, 0xb4, 0x7d, 0x1c, 0x81, 0x6f, 0x18, 0x4e, 0xe7, 0x86, 0xf5, 0x69, 0x7a, 0xde, 0x6a, 0x69, 0xac, 0x64, 0xa2, 0xcd, 0xdf,
    0x8c, 0xe1, 0x7a, 0xea, 0x4d, 0xf7, 0xc6, 0xd6, 0x10, 0xa2, 0xc5, 0x33, 0xe6, 0x0c, 0x2f, 0xce, 0x55, 0x6e, 0x1c, 0xf8};

// Device's public key. Will be received from device after pairing
static unsigned char DEVICE_PUB_KEY[PUB_KEY_SIZE] = {0};

namespace RealSenseID
{
namespace Examples
{
SignHelper::SignHelper()
{
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
    mbedtls_ecdsa_init(&_ecdsa_host_context);
    mbedtls_ecdsa_init(&_ecdsa_device_context);

    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;

    if (mbedtls_ecdsa_genkey(&_ecdsa_host_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.Q.X, SAMPLE_HOST_PUB_KEY, PUB_X_Y_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.Q.Y, SAMPLE_HOST_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
        return;

    if (mbedtls_ecdsa_genkey(&_ecdsa_device_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.X, DEVICE_PUB_KEY, PUB_X_Y_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.Y, DEVICE_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
        return;
    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;

    _initialized = true;
}

SignHelper::~SignHelper()
{
    mbedtls_entropy_free(&_entropy);
    mbedtls_ctr_drbg_free(&_ctr_drbg);
    mbedtls_ecdsa_free(&_ecdsa_host_context);
    mbedtls_ecdsa_free(&_ecdsa_device_context);
}

bool SignHelper::Sign(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig)
{
    if (!_initialized)
        return false;

    unsigned char digest[SHA_256_DIGEST_SIZE_BYTES];
    int ret = mbedtls_sha256_ret(buffer, buffer_len, digest, 0);
    if (ret != 0)
    {
        printf("[ERROR] Failed! mbedtls_sha256_ret returned %d", ret);
        return false;
    }

    unsigned char signature[MBEDTLS_ECDSA_MAX_LEN];
    size_t sign_len, lenTag;
    ret = mbedtls_ecdsa_write_signature(&_ecdsa_host_context, MBEDTLS_MD_SHA256, digest, SHA_256_DIGEST_SIZE_BYTES, signature, &sign_len,
                                        mbedtls_ctr_drbg_random, &_ctr_drbg);

    if (ret != 0)
    {
        printf("[ERROR] mbedtls_ecdsa_write_signature failed with status %d", ret);
        return false;
    }

    unsigned char* ptrSig = signature;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    ret = mbedtls_asn1_get_tag(&ptrSig, signature + sign_len, &lenTag, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (!ret)
    {
        ret = mbedtls_asn1_get_mpi(&ptrSig, signature + sign_len, &r);
        if (!ret)
        {
            ret = mbedtls_asn1_get_mpi(&ptrSig, signature + sign_len, &s);
            if (!ret)
            {
                ret = mbedtls_mpi_write_binary(&r, out_sig, PUB_X_Y_SIZE);
                if (!ret)
                {
                    ret = mbedtls_mpi_write_binary(&s, out_sig + PUB_X_Y_SIZE, PUB_X_Y_SIZE);
                }
            }
        }
    }

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret == 0; // return true on success
}

bool SignHelper::Verify(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig, const unsigned int sign_len)
{
    if (!_initialized)
        return false;

    (void)sign_len; // unused

    unsigned char buf[MBEDTLS_ECDSA_MAX_LEN];
    unsigned char* p = buf + sizeof(buf);

    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int ret = mbedtls_mpi_read_binary(&r, sig, PUB_X_Y_SIZE);
    if (!ret)
    {
        ret = mbedtls_mpi_read_binary(&s, sig + PUB_X_Y_SIZE, PUB_X_Y_SIZE);
        if (!ret)
        {
            int len1 = mbedtls_asn1_write_mpi(&p, buf, &s);
            if (len1 < 0)
            {
                mbedtls_mpi_free(&r);
                mbedtls_mpi_free(&s);
                printf("[ERROR] Failed! mbedtls_asn1_write_mpi returned %d", len1);
                return false;
            }

            int len2 = mbedtls_asn1_write_mpi(&p, buf, &r);
            if (len2 < 0)
            {
                mbedtls_mpi_free(&r);
                mbedtls_mpi_free(&s);
                printf("[ERROR] Failed! mbedtls_asn1_write_mpi returned %d", len2);
                return false;
            }

            int len3 = mbedtls_asn1_write_len(&p, buf, len1 + len2);
            if (len3 < 0)
            {
                mbedtls_mpi_free(&r);
                mbedtls_mpi_free(&s);
                printf("[ERROR] Failed! mbedtls_asn1_write_len returned %d", len3);
                return false;
            }

            int len4 = mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
            if (len4 < 0)
            {
                mbedtls_mpi_free(&r);
                mbedtls_mpi_free(&s);
                printf("[ERROR] Failed! mbedtls_asn1_write_tag returned %d", len4);
                return false;
            }

            unsigned char digest[SHA_256_DIGEST_SIZE_BYTES];
            ret = mbedtls_sha256_ret(buffer, buffer_len, digest, 0);
            if (ret != 0)
            {
                mbedtls_mpi_free(&r);
                mbedtls_mpi_free(&s);
                printf("[ERROR] Failed! mbedtls_sha256_ret returned %d", ret);
                return false;
            }

            size_t len = len1 + len2 + len3 + len4;
            ret = mbedtls_ecdsa_read_signature(&_ecdsa_device_context, digest, SHA_256_DIGEST_SIZE_BYTES, p, len);
        }
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret == 0; // return true on success
};

void SignHelper::UpdateDevicePubKey(const unsigned char* pubKey)
{
    ::memcpy(DEVICE_PUB_KEY, pubKey, PUB_KEY_SIZE);

    if (mbedtls_ecdsa_genkey(&_ecdsa_device_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.X, DEVICE_PUB_KEY, PUB_X_Y_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.Y, DEVICE_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
        return;
    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;
}

const unsigned char* SignHelper::GetHostPubKey() const
{
    return SAMPLE_HOST_PUB_KEY;
}

Status SignHelper::ExchangeKeys(FaceAuthenticator* faceAuthenticator)
{
    rsid_pairing_args rv = {0};
    ::memcpy(rv.host_pubkey, GetHostPubKey(), sizeof(rv.host_pubkey));
    Status s = faceAuthenticator->Pair(rv.host_pubkey, rv.host_pubkey_sig, rv.device_pubkey_result);
    if (s == Status::Ok)
        UpdateDevicePubKey((unsigned char*)rv.device_pubkey_result);
    return s;
}

} // namespace Examples
} // namespace RealSenseID

/*
 * c example implementation of signature callbacks.
 *   1. create SignHelper instance.
 *   2. store it in the *ctx field of the rsid_signature_clbk struct.
 *   3. when the c callback are called, SignHelper* is extracted from the *ctx field,
 *      and used to perform the actual sign/verify callbacks.
 */
rsid_signature_clbk* rsid_create_example_sig_clbk()
{
    auto* clbk = new (rsid_signature_clbk);
    clbk->ctx = static_cast<void*>(new RealSenseID::Examples::SignHelper());
    clbk->sign_clbk = rsid_sign_example;
    clbk->verify_clbk = rsid_verify_example;
    return clbk;
}

/* destroy security context */
void rsid_destroy_example_sig_clbk(rsid_signature_clbk* clbk)
{
    if (clbk == nullptr)
        return;
    if (clbk->ctx != nullptr)
    {
        delete (reinterpret_cast<RealSenseID::Examples::SignHelper*>(clbk->ctx));
    }
    delete (clbk);
}

int rsid_sign_example(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig, void* ctx)
{
    auto* sig_clbk = static_cast<RealSenseID::Examples::SignHelper*>(ctx);
    return static_cast<int>(sig_clbk->Sign(buffer, buffer_len, out_sig));
}

int rsid_verify_example(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig, const unsigned int siglen,
                        void* ctx)
{
    auto* sig_clbk = static_cast<RealSenseID::Examples::SignHelper*>(ctx);
    return static_cast<int>(sig_clbk->Verify(buffer, buffer_len, sig, siglen));
}

const unsigned char* rsid_get_host_pubkey_example(rsid_signature_clbk* clbk)
{
    auto* sig_clbk = static_cast<RealSenseID::Examples::SignHelper*>(clbk->ctx);
    return sig_clbk->GetHostPubKey();
}

void rsid_update_device_pubkey_example(rsid_signature_clbk* clbk, const unsigned char* pubkey)
{
    auto* sig_clbk = static_cast<RealSenseID::Examples::SignHelper*>(clbk->ctx);
    sig_clbk->UpdateDevicePubKey(pubkey);
}

#ifdef RSID_SECURE
/* Create pairing args using the given rsid_signature_clbk helper */
rsid_pairing_args* rsid_create_pairing_args_example(rsid_signature_clbk* clbk)
{
    auto* rv = new rsid_pairing_args();
    auto* sig_clbk = static_cast<RealSenseID::Examples::SignHelper*>(clbk->ctx);
    ::memcpy(rv->host_pubkey, sig_clbk->GetHostPubKey(), sizeof(rv->host_pubkey));
    ::memset(rv->host_pubkey_sig, 0, sizeof(rv->host_pubkey_sig));
    ::memset(rv->device_pubkey_result, 0, sizeof(rv->device_pubkey_result));
    return rv;
}

// Destroy paring args
void rsid_destroy_pairing_args_example(rsid_pairing_args* args)
{
    delete args;
}
#endif // RSID_SECURE
