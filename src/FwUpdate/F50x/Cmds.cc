// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "Cmds.h"
#include <sstream>

namespace RealSenseID
{
namespace FwUpdateF50x
{
namespace F50xCmds
{
std::string dlver()
{
    return "\ndlver";
}

std::string dlspd(long spd)
{
    std::ostringstream oss;
    oss << "\ndlspd " << spd;
    return oss.str();
}

std::string dlinfo(const std::string& module_name)
{
    std::ostringstream oss;
    oss << "\ndlinfo " << module_name;
    return oss.str();
}

std::string dlinit(const std::string& name, size_t size)
{
    std::ostringstream oss;
    oss << "\ndlinit " << name << " sz=" << size;
    return oss.str();
}

std::string dl(const std::string& name, size_t n)
{
    std::ostringstream oss;
    oss << "\ndl " << name << " " << n;

    return oss.str();
}

std::string dlclean()
{
    return "\ndlclean";
}

} // namespace F50xCmds
} // namespace FwUpdateF50x
} // namespace RealSenseID