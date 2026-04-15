// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "Cmds.h"
#include <sstream>

namespace RealSenseID
{
namespace FwUpdateF45x
{
namespace F45xCmds
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

std::string dlinit(const std::string& name, const std::string& version, size_t size, bool start_session, uint32_t crc, uint32_t block_size)
{
    std::ostringstream oss;
    oss << "\ndlinit " << name << " ver=" << version << " sz=" << size << " blksz=" << block_size << " crc=" << std::hex << crc;

    if (start_session)
        oss << " session";

    return oss.str();
}

std::string dl(size_t n)
{
    std::ostringstream oss;
    oss << "\ndl " << n;

    return oss.str();
}

std::string dlact(bool is_last)
{
    std::ostringstream oss;
    oss << "\ndlact ";

    if (is_last)
        oss << " session reboot";

    return oss.str();
}

std::string dlsize(const std::string& module_name, size_t size)
{
    std::ostringstream oss;
    oss << "\ndlsize " << module_name << " sz=" << size;

    return oss.str();
}

std::string dlnew(const std::string& module_name, size_t size)
{
    std::ostringstream oss;
    oss << "\ndlnew " << module_name << " sz=" << size;

    return oss.str();
}
} // namespace F45xCmds
} // namespace FwUpdateF45x
} // namespace RealSenseID