// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <iostream>
#include <stdio.h>

class MyEnrollClbk : public RealSenseID::EnrollmentCallback
{
public:
    void OnResult(const RealSenseID::EnrollStatus status) override
    {
        std::cout << "Result " << status << std::endl;
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        std::cout << "OnProgress " << pose << std::endl;
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        std::cout << "Hint " << hint << std::endl;
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        for (auto& face : faces)
        {
            printf("** Detected face %u,%u %ux%u (timestamp %u)\n", face.x, face.y, face.w, face.h, ts);
        }
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        for (auto& lms : landmarks)
        {
            printf("** Detected landmarks (timestamp %u)\n", ts);
            for (size_t l = 0; l < NUM_FACE_LANDMARKS; l++)
            {
                printf("    x[%zu]=%d, y[%zu]=%d\n", l, lms.lm_x[l], l, lms.lm_y[l]);
            }
        }
    }
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
    MyEnrollClbk enroll_clbk;
    authenticator.Enroll(enroll_clbk, "john");
}
