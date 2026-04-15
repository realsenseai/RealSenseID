// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/aes.h"
#include "mbedtls/hkdf.h"

#include <functional>

#define ECC_P256_KEY_SIZE_BYTES        64
#define ECC_P256_SIG_SIZE_BYTES        64
#define ECC_P256_KEY_X_Y_Z_SIZE_BYTES  32
#define AES_CTR_256_BIT_KEY_SIZE_BYTES 32
#define AES_CTR_IV_SIZE_BYTES          16
#define HMAC_256_SIZE_BYTES            32
#define DATA_PACKET_CONTENT_SIZE       ECC_P256_KEY_SIZE_BYTES + ECC_P256_SIG_SIZE_BYTES
#define SIGNED_PUBKEY_SIZE             ECC_P256_KEY_SIZE_BYTES * 2
#define LICENSE_SESSION_TOKEN_SIZE     16
#define SERIAL_NUMBER_SIZE             28


namespace RealSenseID
{
namespace PacketManager
{
class MbedtlsWrapper
{
public:
    using SignCallback = std::function<bool(const unsigned char*, const unsigned int, unsigned char*)>;
    using VerifyCallback = std::function<bool(const unsigned char*, const unsigned int, const unsigned char*, const unsigned int)>;

    MbedtlsWrapper();
    ~MbedtlsWrapper();

    MbedtlsWrapper(const MbedtlsWrapper&) = delete;
    MbedtlsWrapper& operator=(const MbedtlsWrapper&) = delete;

    bool IsMaEnabled(bool& isMaEnabled);
    size_t GetSignedEcdhPubkeySize();
    unsigned char* GetSignedEcdhPubkey(SignCallback signCallback);
    bool VerifyEcdhSignedKey(const unsigned char* ecdhSignedPubKey, VerifyCallback verifyCallback);
    bool Encrypt(const unsigned char* iv, const unsigned char* input, unsigned char* output, const unsigned int length);
    bool Decrypt(const unsigned char* iv, const unsigned char* input, unsigned char* output, const unsigned int length);
    bool CalcHmac(const unsigned char* input, const unsigned int length, unsigned char* hmac);

private:
    void Reset();
    bool GenerateEcdhKey();
    bool AesCtr256(const unsigned char* iv, const unsigned char* input, unsigned char* output, const unsigned int length);

    bool _ecdh_generate_key;
    mbedtls_entropy_context _entropy_ctx;
    mbedtls_ctr_drbg_context _ctr_drbg_ctx;
    mbedtls_ecdh_context _edch_ctx;
    mbedtls_aes_context _aes_ctx;
    const mbedtls_md_info_t* _md;
    unsigned char _shared_secret[ECC_P256_KEY_X_Y_Z_SIZE_BYTES];
    unsigned char _aes_key[ECC_P256_KEY_X_Y_Z_SIZE_BYTES];
    unsigned char _hmac_key[ECC_P256_KEY_X_Y_Z_SIZE_BYTES];
    unsigned char _ecdh_signed_pubkey[SIGNED_PUBKEY_SIZE];
};
} // namespace PacketManager
} // namespace RealSenseID
