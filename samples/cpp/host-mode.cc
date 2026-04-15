// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DiscoverDevices.h"
#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <string.h>
#include <stdio.h>

// map of user-id->faceprint_pair to demonstrate faceprints feature.
static std::map<std::string, RealSenseID::Faceprints> s_user_faceprint_db;

// Create FaceAuthenticator (after successfully connecting it to the device).
// If failed to connect, exit(1)
std::unique_ptr<RealSenseID::FaceAuthenticator> CreateAuthenticator(const RealSenseID::SerialConfig& serial_config,
                                                                    RealSenseID::DeviceType device_type)
{
    auto authenticator = std::make_unique<RealSenseID::FaceAuthenticator>(device_type);
    auto connect_status = authenticator->Connect(serial_config);
    if (connect_status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed connecting to port " << serial_config.port << " status:" << connect_status << std::endl;
        std::exit(1);
    }
    std::cout << "Connected to device" << std::endl;
    return authenticator;
}

// extract faceprints for new enrolled user
class EnrollClbk : public RealSenseID::EnrollFaceprintsExtractionCallback
{
    std::string _user_id;

public:
    EnrollClbk(const char* user_id) : _user_id(user_id)
    {
    }

    void OnResult(const RealSenseID::EnrollStatus status, const RealSenseID::ExtractedFaceprints* faceprints) override
    {
        std::cout << "on_result: status: " << status << std::endl;
        if (status == RealSenseID::EnrollStatus::Success)
        {
            s_user_faceprint_db[_user_id].data.version = faceprints->data.version;
            s_user_faceprint_db[_user_id].data.flags = faceprints->data.flags;
            s_user_faceprint_db[_user_id].data.featuresType = faceprints->data.featuresType;

            // handle with/without mask vectors properly (if needed).

            // set the full data for the enrolled object:
            size_t copySize = sizeof(faceprints->data.featuresVector);

            static_assert(sizeof(s_user_faceprint_db[_user_id].data.adaptiveDescriptorWithoutMask) ==
                              sizeof(faceprints->data.featuresVector),
                          "faceprints sizes does not match");
            ::memcpy(s_user_faceprint_db[_user_id].data.adaptiveDescriptorWithoutMask, faceprints->data.featuresVector, copySize);

            static_assert(sizeof(s_user_faceprint_db[_user_id].data.enrollmentDescriptor) == sizeof(faceprints->data.featuresVector),
                          "faceprints sizes does not match");
            ::memcpy(s_user_faceprint_db[_user_id].data.enrollmentDescriptor, faceprints->data.featuresVector, copySize);
        }
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        std::cout << "on_progress: pose: " << pose << std::endl;
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        std::cout << "on_hint: hint: " << hint << std::endl;
    }
};

void enroll_faceprints(const RealSenseID::SerialConfig& serial_config, RealSenseID::DeviceType device_type, const char* user_id)
{
    auto authenticator = CreateAuthenticator(serial_config, device_type);
    EnrollClbk enroll_clbk {user_id};
    auto status = authenticator->ExtractFaceprintsForEnroll(enroll_clbk);
    std::cout << "Status: " << status << std::endl << std::endl;
}

// authenticate with faceprints
class FaceprintsAuthClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
    RealSenseID::FaceAuthenticator* _authenticator;

public:
    FaceprintsAuthClbk(RealSenseID::FaceAuthenticator* authenticator) : _authenticator(authenticator)
    {
    }

    void OnResult(const RealSenseID::AuthenticateStatus status, const RealSenseID::ExtractedFaceprints* faceprints) override
    {
        std::cout << "on_result: status: " << status << std::endl;

        if (status != RealSenseID::AuthenticateStatus::Success)
        {
            std::cout << "ExtractFaceprints failed with status " << status << std::endl;
            return;
        }

        RealSenseID::MatchElement scanned_faceprint;
        scanned_faceprint.data.version = faceprints->data.version;
        scanned_faceprint.data.featuresType = faceprints->data.featuresType;
        int32_t vecFlags = (int32_t)faceprints->data.featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];
        int32_t opFlags = RealSenseID::FaOperationFlagsEnum::OpFlagAuthWithoutMask;
        scanned_faceprint.data.flags = opFlags;
        static_assert(sizeof(scanned_faceprint.data.featuresVector) == sizeof(faceprints->data.featuresVector),
                      "faceprints without mask sizes does not match");
        ::memcpy(scanned_faceprint.data.featuresVector, faceprints->data.featuresVector, sizeof(faceprints->data.featuresVector));

        // try to match the resulting faceprint to one of the faceprints stored in the db
        RealSenseID::Faceprints updated_faceprint;

        std::cout << "\nSearching " << s_user_faceprint_db.size() << " faceprints" << std::endl;

        int save_max_score = -1;
        int winning_index = -1;
        std::string winning_id_str = "";
        RealSenseID::MatchResultHost winning_match_result;
        RealSenseID::Faceprints winning_updated_faceprints;

        // use High by default.
        // should be taken from DeviceConfig.
        RealSenseID::ThresholdsConfidenceEnum matcher_confidence_level =
            RealSenseID::ThresholdsConfidenceEnum::ThresholdsConfidenceLevel_High;

        int users_index = 0;

        for (auto& iter : s_user_faceprint_db)
        {
            auto& user_id = iter.first;
            auto& existing_faceprint = iter.second;       // faceprints at the DB
            auto& updated_faceprint = existing_faceprint; // updated faceprints

            auto match =
                _authenticator->MatchFaceprints(scanned_faceprint, existing_faceprint, updated_faceprint, matcher_confidence_level);

            int current_score = (int)match.score;

            // save the best winner that matched.
            if (match.success)
            {
                if (current_score > save_max_score)
                {
                    save_max_score = current_score;
                    winning_match_result = match;
                    winning_index = users_index;
                    winning_id_str = user_id;
                    winning_updated_faceprints = updated_faceprint;
                }
            }
            users_index++;
        } // end of for() loop

        if (winning_index >= 0) // we have a winner so declare success!
        {
            std::cout << "\n******* Match success. user_id: " << winning_id_str << " *******\n" << std::endl;
            // apply adaptive-update on the db.
            if (winning_match_result.should_update)
            {
                // apply adaptive update
                s_user_faceprint_db[winning_id_str] = winning_updated_faceprints;
                std::cout << "DB adaptive apdate applied to user = " << winning_id_str << "." << std::endl;
            }
        }
        else // no winner, declare authentication failed!
        {
            std::cout << "\n******* Forbidden (no faceprint matched) *******\n" << std::endl;
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        std::cout << "on_hint: hint: " << hint << std::endl;
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


void authenticate_faceprints(const RealSenseID::SerialConfig& serial_config, RealSenseID::DeviceType device_type)
{
    auto authenticator = CreateAuthenticator(serial_config, device_type);
    FaceprintsAuthClbk clbk(authenticator.get());
    // extract faceprints of the user in front of the device
    auto status = authenticator->ExtractFaceprintsForAuth(clbk);
    if (status != RealSenseID::Status::Ok)
        std::cout << "Status: " << status << std::endl << std::endl;
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

    RealSenseID::SerialConfig config {device.serialPort};
    enroll_faceprints(config, device.deviceType, "my-username");
    authenticate_faceprints(config, device.deviceType);
}
