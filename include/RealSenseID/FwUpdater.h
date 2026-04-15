// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/RealSenseIDExports.h"
#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/Status.h"
#include "RealSenseID/Version.h"


#include <string>
#include <vector>

namespace RealSenseID
{
namespace Impl
{
class IFwUpdater; // internal impl
}


/**
 * FwUpdater class.
 * Handles firmware update operations.
 */
class RSID_API FwUpdater
{
public:
    /**
     * Firmware update related settings.
     */
    struct Settings
    {
        SerialConfig serial_config; // serial port to perform the update on
        bool force_full = false;    // if true update all modules and blocks regardless of crc checks
    };

    /**
     * Holds per-field compatibility results populated by CheckCompatibility.
     * All fields default to -1 (not applicable / not queried).
     * F45x fills only the otpSku fields; F460/F500 fills only the secureBoot fields.
     * The Is*Compatible helpers skip the check when either value is -1.
     */
    struct FwCompatibilityInfo
    {
        // F45x only: OTP-encryption SKU number (1=SKU1, 2=SKU2); -1 = not applicable.
        int expectedOtpSku = -1, deviceOtpSku = -1;
        // F460/F500 only: CSS-signed firmware flag (0=unsigned, 1=CSS-signed); -1 = not applicable.
        int expectedSecureBoot = -1, deviceSecureBoot = -1;
        int expectedDbVer = -1, deviceDbVer = -1;
        int expectedDeviceType = -1, connectedDeviceType = -1;

        bool IsOtpSkuCompatible() const
        {
            // -1 means not applicable for this device type; skip the check.
            if (expectedOtpSku < 0 || deviceOtpSku < 0)
                return true;
            return expectedOtpSku == deviceOtpSku;
        }
        bool IsSecureBootCompatible() const
        {
            // -1 means not applicable for this device type; skip the check.
            if (expectedSecureBoot < 0 || deviceSecureBoot < 0)
                return true;
            return expectedSecureBoot == deviceSecureBoot;
        }
        bool IsDbCompatible() const
        {
            // If either value is -1 (not queried, e.g. device stuck in bootloader), skip the check.
            if (deviceDbVer < 0 || expectedDbVer < 0)
                return true;
            return expectedDbVer == deviceDbVer;
        }
        bool IsDeviceTypeCompatible() const
        {
            // If connectedDeviceType < 0 the device did not report a type (e.g. stuck in bootloader).
            // In that case the check is skipped and treated as compatible.
            if (connectedDeviceType < 0)
                return true;
            return expectedDeviceType >= 0 && expectedDeviceType == connectedDeviceType;
        }
        bool IsAllCompatible() const
        {
            return IsOtpSkuCompatible() && IsSecureBootCompatible() && IsDbCompatible() && IsDeviceTypeCompatible();
        }
    };

    /**
     * User defined callback for firmware update events.
     * Callback will be used to provide feedback to the client.
     */
    struct EventHandler
    {
        virtual ~EventHandler() = default;

        /**
         * Called to inform the client of the overall firmware update progress.
         *
         * @param[in] progress Current firmware update progress, range: 0.0f - 1.0f.
         */
        virtual void OnProgress(float progress) = 0;
    };

    // Create fw updater for the given device
    explicit FwUpdater(DeviceType deviceType);
    ~FwUpdater() = default;
    FwUpdater(const FwUpdater&) = delete;
    FwUpdater& operator=(const FwUpdater& other) = delete;
    FwUpdater(FwUpdater&&) = delete;
    FwUpdater& operator=(FwUpdater&& other) = delete;

    /**
     * Extracts the firmware and recognition version from the firmware package, as well as all the modules names.
     *
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] outFwVersion Operational firmware (OPFW) version string.
     * @param[out] outRecognitionVersion Recognition model version string.
     * @param[out] moduleNames Names of modules found in the binary file.
     * @return True if extraction succeeded and false otherwise.
     */
    bool ExtractFwInformation(const char* binPath, std::string& outFwVersion, std::string& outRecognitionVersion,
                              std::vector<std::string>& moduleNames) const;


    /**
     * Performs a firmware update using the given firmware file.
     *
     * @param[in] handler Responsible for handling events triggered during the update.
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @return OK if update succeeded matching error status if it failed.
     */
    Status UpdateModules(EventHandler* handler, Settings settings, const char* binPath) const;

    /**
     * Check OTP-encryption SKU compatibility between the firmware binary and the connected F45x device.
     * Not meaningful for F460/F500 — always returns true for those devices.
     *
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] expectedOtpSku OTP SKU number embedded in the binary (1=SKU1, 2=SKU2); -1 if not applicable.
     * @param[out] deviceOtpSku OTP SKU number reported by the device; -1 if not applicable or undetectable.
     * @return True if the SKUs match or if the check is not applicable.
     */
    bool IsOtpSkuCompatible(const Settings& settings, const char* binPath, int& expectedOtpSku, int& deviceOtpSku) const;

    /**
     * Check CSS-signing (secure boot) compatibility between the firmware binary and the connected F460/F500 device.
     * Not meaningful for F45x — always returns true for those devices.
     *
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] expectedSecureBoot Secure boot flag in the binary (0=unsigned, 1=CSS-signed); -1 if not applicable.
     * @param[out] deviceSecureBoot Secure boot state of the device; -1 if not applicable or undetectable.
     * @return True if the secure boot flags match or if the check is not applicable.
     */
    bool IsSecureBootCompatible(const Settings& settings, const char* binPath, int& expectedSecureBoot, int& deviceSecureBoot) const;

    /**
     * Check DB format version used in the binary file and answer whether it matches the device.
     * Queries bspver from the device and parses the DB version field.
     *
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] expectedDbVer DB version embedded in the firmware binary.
     * @param[out] deviceDbVer DB version reported by the device.
     * @return True if expectedDbVer == deviceDbVer and false otherwise.
     */
    bool IsDbCompatible(const Settings& settings, const char* binPath, int& expectedDbVer, int& deviceDbVer) const;

    /**
     * Check that the firmware binary targets the connected device type.
     *
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] expectedDeviceType Device type embedded in the firmware binary (0=F450, 1=F460, 2=F500).
     * @param[out] connectedDeviceType Device type of the connected device.
     * @return True if expectedDeviceType == connectedDeviceType and false otherwise.
     */
    bool IsDeviceTypeCompatible(const Settings& settings, const char* binPath, int& expectedDeviceType, int& connectedDeviceType) const;

    /**
     * Performs all compatibility checks (OTP SKU, secure boot, DB version, device type) in a single
     * device connection. Prefer this over calling the individual Is*Compatible functions when more
     * than one check is needed.
     *
     * @param[in]  settings Firmware update settings.
     * @param[in]  binPath  Path to the firmware binary file.
     * @param[out] info     Populated with expected and device-side values for each check.
     * @return True if all checks pass, false if any check fails or a connection/parse error occurs.
     */
    bool CheckCompatibility(const Settings& settings, const char* binPath, FwCompatibilityInfo& info) const;

private:
    Impl::IFwUpdater* _impl;
};
} // namespace RealSenseID
