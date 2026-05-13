// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <iostream>
#include <stdio.h>

class MyAuthClbk : public RealSenseID::AuthenticationCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id, short score) override
    {
        if (status == RealSenseID::AuthenticateStatus::Success)
            std::cout << "Authenticated " << user_id << std::endl;
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        std::cout << "OnHint " << hint << std::endl;
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

    void OnPersonDetected(const std::vector<RealSenseID::PersonRect>& persons, unsigned int ts) override
    {
        for (const auto& person : persons)
        {
            printf("** Detected person (id %u) %u,%u %ux%u (timestamp %u) score=%.3f\n", person.id, person.x, person.y, person.w, person.h,
                   ts, person.score);
        }
    }

    void OnBarcodeDecoded(const std::vector<std::string>& barcodes, unsigned int ts) override
    {
        for (const auto& barcode : barcodes)
        {
            printf("** Detected barcode %s (timestamp %u)\n", barcode.c_str(), ts);
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
    MyAuthClbk auth_clbk;
    authenticator.Authenticate(auth_clbk);
}
