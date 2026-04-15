// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/DiscoverDevices.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace RealSenseID;

// Fixture that discovers a device, creates FaceAuthenticator with the correct DeviceType,
// and connects. Tests branch on device_type to verify appropriate behavior for each device.
struct DetectFixture
{
    DeviceType device_type = DeviceType::Unknown;
    FaceAuthenticator fa;

    DetectFixture() : fa(GetDeviceType())
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

        device_type = devices[0].deviceType;

        SerialConfig config;
        config.port = devices[0].serialPort;

        if (fa.Connect(config) != Status::Ok)
        {
            FAIL("Connect failed on port " << config.port);
        }
    }

    ~DetectFixture()
    {
        fa.Disconnect();
    }

    bool IsF45x() const
    {
        return device_type == DeviceType::F45x;
    }

private:
    static DeviceType GetDeviceType()
    {
        static const auto devices = RealSenseID::DiscoverDevices();
        if (devices.empty())
            return DeviceType::Unknown;
        return devices[0].deviceType;
    }
};

// --- DetectPersons ---

TEST_CASE_METHOD(DetectFixture, "DetectPersons single", "[detect][device]")
{
    auto cb = [&](const std::vector<PersonRect>& persons, unsigned int, AuthenticateStatus status) {
        if (!persons.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        return true;
    };

    auto result = fa.DetectPersons(cb, false);

    if (IsF45x())
    {
        CHECK(result == Status::NotSupported);
    }
    else
    {
        CHECK(result == Status::Ok);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectPersons loop stops on callback cancel", "[detect][device]")
{
    int call_count = 0;
    auto cb = [&](const std::vector<PersonRect>& persons, unsigned int, AuthenticateStatus status) {
        if (!persons.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        call_count++;
        return false;
    };

    auto status = fa.DetectPersons(cb, true);

    if (IsF45x())
    {
        CHECK(status == Status::NotSupported);
    }
    else
    {
        CHECK(status == Status::Ok);
        CHECK(call_count >= 1);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectPersons loop stops on Cancel", "[detect][device]")
{
    using namespace std::chrono_literals;

    if (IsF45x())
    {
        auto cb = [](const std::vector<PersonRect>&, unsigned int, AuthenticateStatus) { return true; };
        CHECK(fa.DetectPersons(cb, true) == Status::NotSupported);
        return;
    }

    std::thread t([this]() {
        std::this_thread::sleep_for(250ms);
        fa.Cancel();
    });

    auto cb = [](const std::vector<PersonRect>&, unsigned int, AuthenticateStatus) { return true; };
    auto status = fa.DetectPersons(cb, true);
    CHECK(status == Status::Ok);
    t.join();
}

// --- DetectPoses ---

TEST_CASE_METHOD(DetectFixture, "DetectPoses single", "[detect][device]")
{
    auto cb = [&](const std::vector<PersonPose>& poses, unsigned int, AuthenticateStatus status) {
        if (!poses.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        return true;
    };

    auto result = fa.DetectPoses(cb, false);

    if (IsF45x())
    {
        CHECK(result == Status::NotSupported);
    }
    else
    {
        CHECK(result == Status::Ok);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectPoses loop stops on callback cancel", "[detect][device]")
{
    int call_count = 0;
    auto cb = [&](const std::vector<PersonPose>& poses, unsigned int, AuthenticateStatus status) {
        if (!poses.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        call_count++;
        return false;
    };

    auto status = fa.DetectPoses(cb, true);

    if (IsF45x())
    {
        CHECK(status == Status::NotSupported);
    }
    else
    {
        CHECK(status == Status::Ok);
        CHECK(call_count >= 1);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectPoses loop stops on Cancel", "[detect][device]")
{
    using namespace std::chrono_literals;

    if (IsF45x())
    {
        auto cb = [](const std::vector<PersonPose>&, unsigned int, AuthenticateStatus) { return true; };
        CHECK(fa.DetectPoses(cb, true) == Status::NotSupported);
        return;
    }

    std::thread t([this]() {
        std::this_thread::sleep_for(250ms);
        fa.Cancel();
    });

    auto cb = [](const std::vector<PersonPose>&, unsigned int, AuthenticateStatus) { return true; };
    auto status = fa.DetectPoses(cb, true);
    CHECK(status == Status::Ok);
    t.join();
}

// --- DetectBodyParts ---

TEST_CASE_METHOD(DetectFixture, "DetectBodyParts single", "[detect][device]")
{
    auto cb = [&](const std::vector<PersonRect>& parts, unsigned int, AuthenticateStatus status) {
        if (!parts.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        return true;
    };

    auto result = fa.DetectBodyParts(cb, false);

    if (IsF45x())
    {
        CHECK(result == Status::NotSupported);
    }
    else
    {
        CHECK(result == Status::Ok);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectBodyParts loop stops on callback cancel", "[detect][device]")
{
    int call_count = 0;
    auto cb = [&](const std::vector<PersonRect>& parts, unsigned int, AuthenticateStatus status) {
        if (!parts.empty())
        {
            CHECK(status == AuthenticateStatus::PersonFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::PersonFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        call_count++;
        return false;
    };

    auto status = fa.DetectBodyParts(cb, true);

    if (IsF45x())
    {
        CHECK(status == Status::NotSupported);
    }
    else
    {
        CHECK(status == Status::Ok);
        CHECK(call_count >= 1);
    }
}

TEST_CASE_METHOD(DetectFixture, "DetectBodyParts loop stops on Cancel", "[detect][device]")
{
    using namespace std::chrono_literals;

    if (IsF45x())
    {
        auto cb = [](const std::vector<PersonRect>&, unsigned int, AuthenticateStatus) { return true; };
        CHECK(fa.DetectBodyParts(cb, true) == Status::NotSupported);
        return;
    }

    std::thread t([this]() {
        std::this_thread::sleep_for(250ms);
        fa.Cancel();
    });

    auto cb = [](const std::vector<PersonRect>&, unsigned int, AuthenticateStatus) { return true; };
    auto status = fa.DetectBodyParts(cb, true);
    CHECK(status == Status::Ok);
    t.join();
}

// --- DecodeBarcodes ---

TEST_CASE_METHOD(DetectFixture, "DecodeBarcodes single", "[detect][device]")
{
    auto cb = [&](const std::vector<std::string>& barcodes, unsigned int, AuthenticateStatus status) {
        if (!barcodes.empty())
        {
            CHECK(status == AuthenticateStatus::BarcodeFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::BarcodeFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        return true;
    };

    auto result = fa.DecodeBarcodes(cb, false);

    if (IsF45x())
    {
        CHECK(result == Status::NotSupported);
    }
    else
    {
        CHECK(result == Status::Ok);
    }
}

TEST_CASE_METHOD(DetectFixture, "DecodeBarcodes loop stops on callback cancel", "[detect][device]")
{
    int call_count = 0;
    auto cb = [&](const std::vector<std::string>& barcodes, unsigned int, AuthenticateStatus status) {
        if (!barcodes.empty())
        {
            CHECK(status == AuthenticateStatus::BarcodeFound);
        }
        else
        {
            CHECK(status != AuthenticateStatus::BarcodeFound);
            CHECK(status != AuthenticateStatus::Success);
        }
        call_count++;
        return false;
    };

    auto status = fa.DecodeBarcodes(cb, true);

    if (IsF45x())
    {
        CHECK(status == Status::NotSupported);
    }
    else
    {
        CHECK(status == Status::Ok);
        CHECK(call_count >= 1);
    }
}

TEST_CASE_METHOD(DetectFixture, "DecodeBarcodes loop stops on Cancel", "[detect][device]")
{
    using namespace std::chrono_literals;

    if (IsF45x())
    {
        auto cb = [](const std::vector<std::string>&, unsigned int, AuthenticateStatus) { return true; };
        CHECK(fa.DecodeBarcodes(cb, true) == Status::NotSupported);
        return;
    }

    std::thread t([this]() {
        std::this_thread::sleep_for(250ms);
        fa.Cancel();
    });

    auto cb = [](const std::vector<std::string>&, unsigned int, AuthenticateStatus) { return true; };
    auto status = fa.DecodeBarcodes(cb, true);
    CHECK(status == Status::Ok);
    t.join();
}
