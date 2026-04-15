// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// Sample demonstrating person detection using the DetectPersons API (F500 and above).
// Runs detection loop for a fixed duration, then stops.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <chrono>
#include <iostream>
#include <vector>

static const int DURATION_SECONDS = 10;

int main()
{
    auto devices = RealSenseID::DiscoverDevices();
    if (devices.empty())
    {
        std::cout << "No device detected" << std::endl;
        return 1;
    }
    auto& device = devices.front();
    std::cout << "Using device on port " << device.serialPort << std::endl;

    RealSenseID::FaceAuthenticator authenticator(device.deviceType);
    auto status = authenticator.Connect({device.serialPort});
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed connecting with status " << status << std::endl;
        return 1;
    }

    auto start = std::chrono::steady_clock::now();

    auto callback = [&](const std::vector<RealSenseID::PersonRect>& persons, unsigned int ts,
                        RealSenseID::AuthenticateStatus detectionStatus) -> bool {
        std::cout << "Detected " << persons.size() << " person(s) (ts=" << ts << ", status=" << detectionStatus << ")\n";
        for (size_t i = 0; i < persons.size(); i++)
        {
            const auto& p = persons[i];
            printf("  [%zu] id=%u  %u,%u  %ux%u  distance=%u\n", i, p.id, p.x, p.y, p.w, p.h, p.distance);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= DURATION_SECONDS)
        {
            std::cout << "Stopping after " << DURATION_SECONDS << " seconds.\n";
            return false;
        }
        return true;
    };

    authenticator.DetectPersons(callback, /*loop=*/true);
    authenticator.Disconnect();
    return 0;
}
