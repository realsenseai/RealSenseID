// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// Sample demonstrating pose estimation using the DetectPoses API (F500 and above).
// Runs detection loop for a fixed duration, then stops.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <chrono>
#include <iostream>
#include <vector>

static const int DURATION_SECONDS = 10;

static const char* LANDMARK_NAMES[NUM_POSE_LANDMARKS] = {
    "nose",       "left_eye",    "right_eye", "left_ear",  "right_ear", "left_shoulder", "right_shoulder", "left_elbow",  "right_elbow",
    "left_wrist", "right_wrist", "left_hip",  "right_hip", "left_knee", "right_knee",    "left_ankle",     "right_ankle",
};

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

    auto callback = [&](const std::vector<RealSenseID::PersonPose>& poses, unsigned int ts,
                        RealSenseID::AuthenticateStatus detectionStatus) -> bool {
        std::cout << "Detected " << poses.size() << " pose(s) (ts=" << ts << ", status=" << detectionStatus << ")\n";
        for (size_t i = 0; i < poses.size(); i++)
        {
            const auto& p = poses[i];
            printf("  [%zu] bbox: %u,%u  %ux%u\n", i, p.x, p.y, p.w, p.h);
            for (int l = 0; l < NUM_POSE_LANDMARKS; l++)
            {
                printf("    %-16s (x,y)=(%4u,%4u)  score=%.4f\n", LANDMARK_NAMES[l], p.lm_x[l], p.lm_y[l], p.lm_score[l]);
            }
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= DURATION_SECONDS)
        {
            std::cout << "Stopping after " << DURATION_SECONDS << " seconds.\n";
            return false;
        }
        return true;
    };

    authenticator.DetectPoses(callback, /*loop=*/true);
    authenticator.Disconnect();
    return 0;
}
