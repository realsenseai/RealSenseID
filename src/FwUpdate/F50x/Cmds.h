// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include <string>
#include <stdint.h>

namespace RealSenseID
{
namespace FwUpdateF50x
{
namespace F50xCmds
{
/**
 * retrieves information for all modules
 *
 * returns a formatted dlver command
 */
std::string dlver();

/**
 * controls baud rate for the firmware update process
 *
 * spd - baud rate for firmware upgrade
 *
 * returns a formatted dlspd command
 */
std::string dlspd(long spd);

/**
 * retrieves specific module extended information
 *
 * module_name - name of requested module
 *
 * returns a formatted dlinfo command
 */
std::string dlinfo(const std::string& module_name);

/**
 * starts a module update process resizes the slot if needed
 *
 * module_name      - updated module name
 * size             - updated module size
 *
 * returns a formatted dlinit command
 */
std::string dlinit(const std::string& name, size_t size);

/**
 * update specific block number of updated module
 *
 * n - block number to update
 *
 * returns a formatted dl command
 */
std::string dl(const std::string& name, size_t n);

/**
 * Clean old modules.
 */
std::string dlclean();
} // namespace F50xCmds
} // namespace FwUpdateF50x
} // namespace RealSenseID
