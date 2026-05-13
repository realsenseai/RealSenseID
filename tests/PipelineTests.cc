// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.

#include "Pipeline/Pipeline.h"
#include "Pipeline/FaceSelector.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace RealSenseID::Pipeline;

// --- Image ---

TEST_CASE("Image default constructor", "[Pipeline][Image]")
{
    Image img;
    REQUIRE(img.width == 0);
    REQUIRE(img.height == 0);
    REQUIRE(img.data() == nullptr);
}

TEST_CASE("Image non-owning view", "[Pipeline][Image]")
{
    uint8_t buf[12] = {};
    Image img(2, 2, buf, Image::BGR);
    REQUIRE(img.width == 2);
    REQUIRE(img.height == 2);
    REQUIRE(img.data() == buf);
}

TEST_CASE("Image owning buffer", "[Pipeline][Image]")
{
    std::vector<uint8_t> buf(12, 0xFF);
    const uint8_t* ptr = buf.data();
    Image img(2, 2, std::move(buf), Image::RGB);
    REQUIRE(img.width == 2);
    REQUIRE(img.height == 2);
    REQUIRE(img.data() != nullptr);
    REQUIRE(img.data()[0] == 0xFF);
}

TEST_CASE("Image rejects invalid dimensions", "[Pipeline][Image]")
{
    uint8_t buf[1] = {};
    REQUIRE_THROWS_AS(Image(0, 1, buf, Image::BGR), std::runtime_error);
    REQUIRE_THROWS_AS(Image(-1, 1, buf, Image::BGR), std::runtime_error);
    REQUIRE_THROWS_AS(Image(1, 0, buf, Image::BGR), std::runtime_error);
    REQUIRE_THROWS_AS(Image(1, -1, buf, Image::BGR), std::runtime_error);
}

TEST_CASE("Image rejects null pointer", "[Pipeline][Image]")
{
    REQUIRE_THROWS_AS(Image(1, 1, nullptr, Image::BGR), std::runtime_error);
}

TEST_CASE("Image owning rejects invalid dimensions", "[Pipeline][Image]")
{
    REQUIRE_THROWS_AS(Image(0, 1, std::vector<uint8_t>(3), Image::BGR), std::runtime_error);
    REQUIRE_THROWS_AS(Image(1, 0, std::vector<uint8_t>(3), Image::BGR), std::runtime_error);
}

// --- FaceBox ---

TEST_CASE("FaceBox default values", "[Pipeline][FaceBox]")
{
    FaceBox box;
    REQUIRE(box.x == 0);
    REQUIRE(box.y == 0);
    REQUIRE(box.w == 0);
    REQUIRE(box.h == 0);
    REQUIRE(box.score == 0.0f);
}

// --- FaceSelector ---

TEST_CASE("FaceSelector returns default for empty input", "[Pipeline][FaceSelector]")
{
    std::vector<FaceBox> empty;
    auto result = FaceSelector::Select(empty, 640, 480);
    REQUIRE(result.w == 0);
    REQUIRE(result.h == 0);
}

TEST_CASE("FaceSelector returns single face unchanged", "[Pipeline][FaceSelector]")
{
    std::vector<FaceBox> faces = {{100, 100, 50, 50, 0.9f}};
    auto result = FaceSelector::Select(faces, 640, 480);
    REQUIRE(result.x == 100);
    REQUIRE(result.y == 100);
    REQUIRE(result.w == 50);
    REQUIRE(result.h == 50);
    REQUIRE(result.score == 0.9f);
}

TEST_CASE("FaceSelector prefers centered larger face", "[Pipeline][FaceSelector]")
{
    // Large centered face vs small corner face, both high quality
    FaceBox centered = {280, 200, 80, 80, 0.8f};
    FaceBox corner = {10, 10, 30, 30, 0.8f};
    std::vector<FaceBox> faces = {centered, corner};
    auto result = FaceSelector::Select(faces, 640, 480);
    REQUIRE(result.x == centered.x);
    REQUIRE(result.y == centered.y);
}

TEST_CASE("FaceSelector returns highest score when no face is high quality", "[Pipeline][FaceSelector]")
{
    // Both below mHighQualityTh (0.5) — should return first (highest score)
    FaceBox a = {100, 100, 40, 40, 0.4f};
    FaceBox b = {200, 200, 40, 40, 0.3f};
    std::vector<FaceBox> faces = {a, b};
    auto result = FaceSelector::Select(faces, 640, 480);
    REQUIRE(result.x == a.x);
    REQUIRE(result.score == a.score);
}
