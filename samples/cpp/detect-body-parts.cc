// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// Sample demonstrating body part detection using the DetectBodyParts API (F500 and above).
// Runs detection loop for a fixed duration, then stops.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <chrono>
#include <iostream>
#include <vector>

static const int DURATION_SECONDS = 10;

static const char* bodyPartName(RealSenseID::PersonRect::BodyPart part)
{
    switch (part)
    {
    case RealSenseID::PersonRect::BodyPart::Person:
        return "Person";
    case RealSenseID::PersonRect::BodyPart::Foot:
        return "Foot";
    case RealSenseID::PersonRect::BodyPart::Arm:
        return "Arm";
    case RealSenseID::PersonRect::BodyPart::Leg:
        return "Leg";
    case RealSenseID::PersonRect::BodyPart::Hand:
        return "Hand";
    case RealSenseID::PersonRect::BodyPart::Torso:
        return "Torso";
    default:
        return "Unknown";
    }
}

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

    auto callback = [&](const std::vector<RealSenseID::PersonRect>& bodyParts, unsigned int ts,
                        RealSenseID::AuthenticateStatus detectionStatus) -> bool {
        std::cout << "Detected " << bodyParts.size() << " body part(s) (ts=" << ts << ", status=" << detectionStatus << ")\n";
        for (size_t i = 0; i < bodyParts.size(); i++)
        {
            const auto& bp = bodyParts[i];
            printf("  [%zu] %-7s  id=%u  %u,%u  %ux%u  distance=%u  score=%.3f\n", i, bodyPartName(bp.body_part), bp.id, bp.x, bp.y, bp.w,
                   bp.h, bp.distance, bp.score);
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= DURATION_SECONDS)
        {
            std::cout << "Stopping after " << DURATION_SECONDS << " seconds.\n";
            return false;
        }
        return true;
    };

    authenticator.DetectBodyParts(callback, /*loop=*/true);
    authenticator.Disconnect();
    return 0;
}
