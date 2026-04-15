// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once


#include "../IFwUpdater.h"

#include <string>
#include <vector>

namespace RealSenseID
{
namespace FwUpdateF50x
{

/**
 * FwUpdater class.
 * Handles firmware update operations for F50x devices.
 */
class FwUpdaterF50x : public Impl::IFwUpdater
{
public:
    FwUpdaterF50x() = default;
    ~FwUpdaterF50x() override = default;


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
                              std::vector<std::string>& moduleNames) const override;


    /**
     * Performs a firmware update using the given firmware file.
     *
     * @param[in] handler Responsible for handling events triggered during the update.
     * @param[in] settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @return OK if update succeeded matching error status if it failed.
     */
    Status UpdateModules(FwUpdater::EventHandler* handler, FwUpdater::Settings settings, const char* binPath) const override;

    bool CheckCompatibility(const FwUpdater::Settings& settings, const char* binPath, FwUpdater::FwCompatibilityInfo& info) const override;
};
} // namespace FwUpdateF50x
} // namespace RealSenseID
