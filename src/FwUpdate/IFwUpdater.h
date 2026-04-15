// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/Version.h"
#include "RealSenseID/FwUpdater.h"

#include <string>
#include <vector>

namespace RealSenseID
{
namespace Impl
{

/**
 * Internal FwUpdater interface
 * Handles firmware update operations.
 */
class IFwUpdater
{
public:
    virtual ~IFwUpdater() = default;

    /**
     * Extracts the firmware and recognition version from the firmware package, as well as all the modules names.
     *
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] outFwVersion Operational firmware (OPFW) version string.
     * @param[out] outRecognitionVersion Recognition model version string.
     * @param[out] moduleNames Names of modules found in the binary file.
     * @return True if extraction succeeded and false otherwise.
     */
    virtual bool ExtractFwInformation(const char* binPath, std::string& outFwVersion, std::string& outRecognitionVersion,
                                      std::vector<std::string>& moduleNames) const = 0;


    /**
     * Performs a firmware update using the given firmware file.
     *
     * @param[in] handler Responsible for handling events triggered during the update.
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @return OK if update succeeded matching error status if it failed.
     */
    virtual Status UpdateModules(FwUpdater::EventHandler* handler, FwUpdater::Settings settings, const char* binPath) const = 0;


    /**
     * Performs all compatibility checks (SKU, DB version, device type) in a single device
     * connection and a single bspver query. Populates all fields of info.
     * Returns true only if all checks pass; individual fields in info can be inspected for details.
     */
    virtual bool CheckCompatibility(const FwUpdater::Settings& settings, const char* binPath,
                                    FwUpdater::FwCompatibilityInfo& info) const = 0;
};

} // namespace Impl
} // namespace RealSenseID
