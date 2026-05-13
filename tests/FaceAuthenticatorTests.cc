#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/AuthenticationCallback.h"
#include "RealSenseID/EnrollmentCallback.h"
#include "RealSenseID/AuthFaceprintsExtractionCallback.h"
#include "RealSenseID/EnrollFaceprintsExtractionCallback.h"
#include "RealSenseID/DiscoverDevices.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>

using namespace RealSenseID;

// Device setup for each test.
// * Connect to a single device
// * Wipe DB
// * Set default config with DeviceConfig::AlgoFlow::All
struct DeviceFixture
{
    FaceAuthenticator fa;

    DeviceFixture()
    {
        static const auto devices = RealSenseID::DiscoverDevices();

        if (devices.empty())
        {
            FAIL("No device detected.");
        }


        if (devices.size() > 1)
        {
            FAIL("Multiple devices connected. Please connect only one device for testing.");
        }

        SerialConfig config;
        config.port = devices[0].serialPort;

        if (fa.Connect(config) != Status::Ok)
        {
            FAIL("Connect failed on port " << config.port);
        }

        // Wipe DB to ensure test isolation
        if (fa.RemoveAll() != Status::Ok)
        {
            FAIL("RemoveAll failed");
        }

        // Set device config to default state
        DeviceConfig dc;
        dc.algo_flow = DeviceConfig::AlgoFlow::All;
        if (fa.SetDeviceConfig(dc) != Status::Ok)
        {
            FAIL("SetDeviceConfig failed.");
        }
    }

    ~DeviceFixture()
    {
        fa.Disconnect();
    }
};

// --- Mocks ---
struct MockEnrollmentCallback : public EnrollmentCallback
{
    EnrollStatus status = EnrollStatus::Failure;
    void OnResult(const EnrollStatus s) override
    {
        this->status = s;
    }
    void OnProgress(const FacePose) override
    {
    }
    void OnHint(const EnrollStatus, float) override
    {
    }
};

struct MockAuthenticationCallback : public AuthenticationCallback
{
    AuthenticateStatus status = AuthenticateStatus::Failure;
    std::string user_id = "";
    void OnResult(AuthenticateStatus s, const char* userId, short) override
    {
        this->status = s;
        if (s == AuthenticateStatus::Success && userId != nullptr)
        {
            user_id = std::string(userId);
        }
    }
    void OnHint(AuthenticateStatus, float) override
    {
    }
};

struct MockAuthFaceprintsExtractionCallback : public AuthFaceprintsExtractionCallback
{
    void OnResult(const AuthenticateStatus, const ExtractedFaceprints*) override
    {
    }
    void OnHint(const AuthenticateStatus, float) override
    {
    }
};

struct MockEnrollFaceprintsExtractionCallback : public EnrollFaceprintsExtractionCallback
{
    void OnResult(const EnrollStatus, const ExtractedFaceprints*) override
    {
    }
    void OnProgress(const FacePose) override
    {
    }
    void OnHint(const EnrollStatus, float) override
    {
    }
};

// --- Tests ---

TEST_CASE("Offline API checks", "[authenticator]")
{
    SECTION("Move semantics")
    {
        FaceAuthenticator fa;
        FaceAuthenticator fa2 = std::move(fa);
    }

    SECTION("Double disconnect")
    {
        FaceAuthenticator fa;
        fa.Disconnect();
        fa.Disconnect();
    }

    SECTION("Invalid COM port")
    {
        FaceAuthenticator fa;
        SerialConfig config;
        config.port = "COM0"; // Reserved/Invalid
        CHECK(fa.Connect(config) != Status::Ok);
    }
}

TEST_CASE_METHOD(DeviceFixture, "Config Read/Write", "[authenticator][config]")
{
    DeviceConfig dc;
    dc.camera_rotation = DeviceConfig::CameraRotation::Rotation_0_Deg;
    dc.security_level = DeviceConfig::SecurityLevel::Medium;
    dc.algo_flow = DeviceConfig::AlgoFlow::All;
    dc.face_selection_policy = DeviceConfig::FaceSelectionPolicy::Single;
    dc.dump_mode = DeviceConfig::DumpMode::None;
    dc.max_spoofs = 42;
    dc.detection_rois[0] = {10, 10, 500, 500};
    dc.num_rois = 1;

    REQUIRE(fa.SetDeviceConfig(dc) == Status::Ok);

    DeviceConfig queried;
    REQUIRE(fa.QueryDeviceConfig(queried) == Status::Ok);
    CHECK(queried.camera_rotation == dc.camera_rotation);
    CHECK(queried.max_spoofs == dc.max_spoofs);
    CHECK(queried.num_rois == dc.num_rois);
    CHECK(queried.detection_rois[0].x == dc.detection_rois[0].x);
}

TEST_CASE_METHOD(DeviceFixture, "Basic Enrollment", "[authenticator][users]")
{
    MockEnrollmentCallback enrollCb;
    REQUIRE(fa.Enroll(enrollCb, "test_user") == Status::Ok);
}

TEST_CASE_METHOD(DeviceFixture, "Empty DB Auth", "[authenticator]")
{
    MockAuthenticationCallback authCb;
    // Auth on fresh device should fail or detect no face
    REQUIRE(fa.Authenticate(authCb) == Status::Ok);
    CHECK(authCb.status != AuthenticateStatus::Success);
}

TEST_CASE_METHOD(DeviceFixture, "Abort Loop", "[authenticator][loop]")
{
    using namespace std::chrono_literals;
    MockAuthenticationCallback authCb;

    auto start = std::chrono::steady_clock::now();

    std::thread t([this]() {
        std::this_thread::sleep_for(400ms);
        fa.Cancel();
    });

    // Loop should block until Cancel() is called
    REQUIRE(fa.AuthenticateLoop(authCb) == Status::Ok);

    auto end = std::chrono::steady_clock::now();
    CHECK(end - start < 2s);
    t.join();
}

TEST_CASE_METHOD(DeviceFixture, "DB Operations", "[authenticator][users]")
{
    unsigned int count = 99;
    REQUIRE(fa.QueryNumberOfUsers(count) == Status::Ok);
    CHECK(count == 0);

    // Idempotency check
    CHECK(fa.RemoveUser("non_existent") == Status::Ok);
}

TEST_CASE_METHOD(DeviceFixture, "Host-side Matcher", "[authenticator][host-mode]")
{
    MatchElement new_fp;
    Faceprints existing_fp;
    Faceprints updated_fp;

    std::memset(&new_fp, 0, sizeof(new_fp));
    std::memset(&existing_fp, 0, sizeof(existing_fp));

    auto result = fa.MatchFaceprints(new_fp, existing_fp, updated_fp);
    CHECK_FALSE(result.success);
}

TEST_CASE_METHOD(DeviceFixture, "Image Enrollment failure cases", "[authenticator]")
{
    // 320x240 black frame
    std::vector<unsigned char> empty_frame(320 * 240 * 3, 0);

    // Expect NoFace or Failure
    auto status = fa.EnrollImage("img_user", empty_frame.data(), 320, 240);
    CHECK(status != EnrollStatus::Success);
}