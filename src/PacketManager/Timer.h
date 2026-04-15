// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

// timeout helper to help keep track of timeouts.
// uses std::chrono::steady_clock

#include "CommonTypes.h"
#include <chrono>

namespace RealSenseID
{
namespace PacketManager
{
class Timer
{
public:
    using clock = std::chrono::steady_clock;

    Timer();
    explicit Timer(timeout_t timeout);

    timeout_t Elapsed() const;
    timeout_t TimeLeft() const;
    bool ReachedTimeout() const;
    void Reset();

private:
    timeout_t _timeout;
    std::chrono::time_point<clock> _start_tp;
};
} // namespace PacketManager
} // namespace RealSenseID
