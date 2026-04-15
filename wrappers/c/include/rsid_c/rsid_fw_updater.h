// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "rsid_export.h"
#include "rsid_status.h"

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    typedef struct
    {
        void* _impl;
    } rsid_fw_updater;

    /* firmware update related settings */
    typedef struct
    {
        const char* port; // serial port to perform the update on
        int force_full;   // force full update of all modules
    } rsid_fw_update_settings;

    /*
     * User defined callback to handle firmware update progress.
     * Receives the progress as a float in the range of 0.0f-1.0f.
     */
    typedef void (*rsid_progress_callback)(float progress);

    typedef struct
    {
        rsid_progress_callback progress_callback; /* user defined progress callback */
    } rsid_fw_update_event_handler;

    typedef struct
    {
        rsid_update_policy update_policy;
        char intermediate_version[64];
    } rsid_firmware_update_policy;

    /* return new fw updater handle (or null on failure) */
    RSID_C_API rsid_fw_updater* rsid_create_fw_updater(rsid_device_type device_type);

    /* destroy the fw updater handle and free its resources */
    RSID_C_API void rsid_destroy_fw_updater(rsid_fw_updater* handle);

    /* check firmware compatibility with host */
    RSID_C_API int rsid_is_compatible_with_host(rsid_fw_updater* handle, const char* fw_version);

    /* extract version from firmware binary package */
    RSID_C_API rsid_status rsid_extract_firmware_version(rsid_fw_updater* handle, const char* bin_path, char* new_fw_version,
                                                         size_t new_fw_version_length, char* new_recognition_version,
                                                         size_t new_recognition_version_size);

    /* performs a firmware update */
    RSID_C_API rsid_status rsid_update_firmware(rsid_fw_updater* handle, const rsid_fw_update_event_handler* event_handler,
                                                rsid_fw_update_settings settings, const char* bin_path);

    /* Check OTP-encryption SKU compatibility for F45x. Not applicable to F460/F500 (returns 1). */
    RSID_C_API int rsid_is_otp_sku_compatible(rsid_fw_updater* handle, rsid_fw_update_settings settings, const char* bin_path,
                                              int* expected_otp_sku, int* device_otp_sku);

    /* Check CSS-signing (secure boot) compatibility for F460/F500. Not applicable to F45x (returns 1). */
    RSID_C_API int rsid_is_secure_boot_compatible(rsid_fw_updater* handle, rsid_fw_update_settings settings, const char* bin_path,
                                                  int* expected_secure_boot, int* device_secure_boot);

    /* all per-field compatibility results from rsid_check_compatibility; fields default to -1 (not applicable / not queried) */
    typedef struct
    {
        int expected_otp_sku; /* F45x only: OTP-encryption SKU number (1=SKU1, 2=SKU2); -1 = not applicable */
        int device_otp_sku;
        int expected_secure_boot; /* F460/F500 only: CSS-signed firmware flag (0=unsigned, 1=CSS-signed); -1 = not applicable */
        int device_secure_boot;
        int expected_db_ver;
        int device_db_ver;
        int expected_device_type;
        int connected_device_type;
    } rsid_fw_compat_info;

    /* perform all compatibility checks (SKU, DB version, device type) in one device connection */
    RSID_C_API int rsid_check_compatibility(rsid_fw_updater* handle, rsid_fw_update_settings settings, const char* bin_path,
                                            rsid_fw_compat_info* info);

#ifdef __cplusplus
}
#endif //__cplusplus
