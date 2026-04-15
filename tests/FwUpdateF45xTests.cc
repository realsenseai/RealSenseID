// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "FwUpdateTestHelpers.h"
#include "FwUpdate/F45x/Utilities.h"

#include <catch2/catch_test_macros.hpp>

// ============================================================================
// F45x DigestHeader constants and builder
// ============================================================================
static constexpr uint32_t F45X_DIGEST_HEADER_SIZE = 512;
static constexpr uint32_t F45X_DIGEST_HEADER_VERSION = 0x00000004;
static constexpr uint32_t F45X_DIGEST_HEADER_VERSION_SIZE = 12;

#pragma pack(push, 1)
struct F45xDigestHeader
{
    uint32_t ModuleHeader;
    uint32_t HeaderLen;
    uint32_t HeaderVersion;
    uint32_t ModuleID;
    uint32_t ModuleVendor;
    uint32_t Date;
    uint32_t Size;
    uint32_t PublicKeyLen;
    uint32_t SigRLen;
    uint32_t SigSLen;
    uint8_t Reserved[88];
    uint8_t Qx[0x10 * 2];
    uint8_t Qy[0x10 * 2];
    uint8_t signature[64];
    uint32_t ver;
    uint8_t id[8];
    uint8_t binVer[F45X_DIGEST_HEADER_VERSION_SIZE];
    uint32_t flags;
    uint32_t binSize;
    uint8_t iv[16];
    uint32_t orgSize;
    uint8_t padding[204];
};
#pragma pack(pop)

static_assert(sizeof(F45xDigestHeader) == F45X_DIGEST_HEADER_SIZE, "F45xDigestHeader must be 512 bytes");

// Builds F45x module data: DigestHeader + payload
static std::vector<unsigned char> MakeF45xModuleData(size_t payload_size, const std::string& id, const std::string& binVer,
                                                     uint32_t digest_ver = F45X_DIGEST_HEADER_VERSION, unsigned char fill = 0xCD)
{
    F45xDigestHeader dh {};
    dh.ver = digest_ver;

    std::memcpy(dh.id, id.c_str(), std::min(id.size(), sizeof(dh.id) - 1));
    dh.id[sizeof(dh.id) - 1] = '\0';

    std::memcpy(dh.binVer, binVer.c_str(), std::min(binVer.size(), sizeof(dh.binVer) - 1));
    dh.binVer[sizeof(dh.binVer) - 1] = '\0';

    std::vector<unsigned char> data(sizeof(dh) + payload_size, fill);
    std::memcpy(data.data(), &dh, sizeof(dh));
    return data;
}

// ============================================================================
// Tests: F45x ParseUfifMetadata
// ============================================================================
TEST_CASE("FwUpdate F45x - ParseUfifMetadata", "[FwUpdate][F45x]")
{
    SECTION("Returns all metadata fields from valid file")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.2.3");
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}}, UFIF_SIG, UFIF_VER, /*otpEncVer=*/7, /*dbVer=*/10, /*cfgVer=*/1,
                      /*deviceType=*/0);
        auto meta = RealSenseID::FwUpdateF45x::ParseUfifMetadata(tf.path);
        REQUIRE(meta.otpEncryptVersion == 7);
        REQUIRE(meta.dbVersion == 10);
        REQUIRE(meta.configVersion == 1);
        REQUIRE(meta.deviceType == 0);
    }

    SECTION("Throws on non-existent file")
    {
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifMetadata("__no_such_file__.bin"), std::runtime_error);
    }

    SECTION("Throws on invalid signature")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}}, 0xDEADBEEF);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifMetadata(tf.path), std::runtime_error);
    }
}

// ============================================================================
// Tests: F45x ParseUfifToModules - valid cases
// ============================================================================
TEST_CASE("FwUpdate F45x - ParseUfifToModules single module", "[FwUpdate][F45x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");

    BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);

    auto& m = modules[0];
    REQUIRE(m.name == "OPFW");
    REQUIRE(m.version == "1.0.0");
    REQUIRE(m.size == module_data.size());
    REQUIRE(m.crc != 0);
    REQUIRE(!m.blocks.empty());
}

TEST_CASE("FwUpdate F45x - ParseUfifToModules multi-module", "[FwUpdate][F45x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    auto data1 = MakeF45xModuleData(2048, "OPFW", "1.0.0", F45X_DIGEST_HEADER_VERSION, 0x11);
    auto data2 = MakeF45xModuleData(1024, "RECOG", "2.5.0", F45X_DIGEST_HEADER_VERSION, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("mod1"), data1},
        {std::string("mod2"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[0].version == "1.0.0");
    REQUIRE(modules[1].name == "RECOG");
    REQUIRE(modules[1].version == "2.5.0");
}

TEST_CASE("FwUpdate F45x - ParseUfifToModules multiple blocks", "[FwUpdate][F45x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    // DigestHeader is 512 bytes + 10000 bytes payload = 10512 total
    // 4K aligned: 12288 -> 3 blocks
    auto module_data = MakeF45xModuleData(10000, "RECOG", "1.0.0", F45X_DIGEST_HEADER_VERSION, 0x55);

    BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == module_data.size());
    auto expected_aligned = (module_data.size() + 4095) & 0xfffff000;
    REQUIRE(modules[0].aligned_size == expected_aligned);
    REQUIRE(modules[0].blocks.size() == expected_aligned / block_size);
}

TEST_CASE("FwUpdate F45x - Module size exactly 4K aligned", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    // Create data so total (header + payload) is exactly 4096
    size_t payload = 4096 - F45X_DIGEST_HEADER_SIZE;
    auto module_data = MakeF45xModuleData(payload, "OPFW", "1.0.0");

    BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == 4096);
    REQUIRE(modules[0].aligned_size == 4096);
    REQUIRE(modules[0].blocks.size() == 1);
}

TEST_CASE("FwUpdate F45x - Module size 4K+1 alignment", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    // Total = 4097 -> aligned to 8192 -> 2 blocks
    size_t payload = 4097 - F45X_DIGEST_HEADER_SIZE;
    auto module_data = MakeF45xModuleData(payload, "OPFW", "1.0.0");

    BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == 4097);
    REQUIRE(modules[0].aligned_size == 8192);
    REQUIRE(modules[0].blocks.size() == 2);
}

// ============================================================================
// Tests: F45x ParseUfifToModules - error cases
// ============================================================================
TEST_CASE("FwUpdate F45x - ParseUfifToModules error cases", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;

    SECTION("Invalid signature")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}}, 0xBADBAD);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Invalid version (major mismatch)")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}}, UFIF_SIG, 0x0200);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Invalid digest header version")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0", 0x00030000);
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("CRC mismatch")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
        ModuleSpec spec;
        spec.name = "module";
        spec.data = module_data;
        spec.crc_override = 0xDEADDEAD;
        BuildUfifFile(tf.path, 1, {spec});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Non-existent file")
    {
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules("__no_such_file__.bin", block_size), std::runtime_error);
    }

    SECTION("Missing version dot in binVer")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "100");
        BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }
}

// ============================================================================
// Tests: Malformed UFIF files (edge cases)
// ============================================================================
TEST_CASE("FwUpdate F45x - Malformed: truncated file", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;

    SECTION("File too short for header")
    {
        TempFile tf;
        std::vector<unsigned char> truncated(16, 0);
        WriteFile(tf.path, truncated);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Valid header but file truncated before entry headers")
    {
        TempFile tf;
        std::vector<unsigned char> buf;
        UfifFileHeader hdr {};
        hdr.sig = 0x46484655;
        hdr.ver = 0x0100;
        hdr.entryN = 2;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));
        WriteFile(tf.path, buf);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Valid header+entries but file truncated before module data")
    {
        TempFile tf;
        auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
        auto crc = ComputeModuleCRC(module_data);

        std::vector<unsigned char> buf;
        UfifFileHeader hdr {};
        hdr.sig = 0x46484655;
        hdr.ver = 0x0100;
        hdr.entryN = 1;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

        UfifEntryHeader entry {};
        std::snprintf(entry.name, sizeof(entry.name), "%s", "module");
        entry.size = static_cast<uint32_t>(module_data.size());
        entry.crc32 = crc;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));
        // No module data written
        WriteFile(tf.path, buf);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Module data shorter than DigestHeader")
    {
        TempFile tf;
        // 256 bytes < 512-byte DigestHeader
        std::vector<unsigned char> small_data(256, 0xAA);

        std::vector<unsigned char> buf;
        UfifFileHeader hdr {};
        hdr.sig = 0x46484655;
        hdr.ver = 0x0100;
        hdr.entryN = 1;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

        UfifEntryHeader entry {};
        std::snprintf(entry.name, sizeof(entry.name), "%s", "module");
        entry.size = 256;
        entry.crc32 = ComputeModuleCRC(small_data);
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));

        size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
        buf.resize(aligned, 0);
        buf.insert(buf.end(), small_data.begin(), small_data.end());
        WriteFile(tf.path, buf);

        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }
}

TEST_CASE("FwUpdate F45x - Malformed: empty file", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    WriteFile(tf.path, {});
    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F45x - block_size = 0 throws", "[FwUpdate][F45x]")
{
    TempFile tf;
    auto module_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");
    BuildUfifFile(tf.path, 1, {{std::string("module"), module_data}});
    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, 0), std::runtime_error);
}


TEST_CASE("FwUpdate F45x - Malformed: partial module data", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto full_data = MakeF45xModuleData(2048, "OPFW", "1.0.0");

    std::vector<unsigned char> buf;
    UfifFileHeader hdr {};
    hdr.sig = 0x46484655;
    hdr.ver = 0x0100;
    hdr.entryN = 1;
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

    UfifEntryHeader entry {};
    std::snprintf(entry.name, sizeof(entry.name), "%s", "module");
    entry.size = static_cast<uint32_t>(full_data.size());
    entry.crc32 = ComputeModuleCRC(full_data);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));

    size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
    buf.resize(aligned, 0);
    // Only 600 of the claimed bytes
    buf.insert(buf.end(), full_data.begin(), full_data.begin() + 600);
    WriteFile(tf.path, buf);

    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F45x - Duplicate module names", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto data1 = MakeF45xModuleData(2048, "OPFW", "1.0.0", F45X_DIGEST_HEADER_VERSION, 0x11);
    auto data2 = MakeF45xModuleData(2048, "OPFW", "1.0.0", F45X_DIGEST_HEADER_VERSION, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("mod1"), data1},
        {std::string("mod2"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[1].name == "OPFW");
    REQUIRE(modules[0].crc != modules[1].crc);
}

TEST_CASE("FwUpdate F45x - Multi-module alignment gap", "[FwUpdate][F45x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    // Non-16-byte-aligned sizes to test inter-module padding
    auto data1 = MakeF45xModuleData(2050, "OPFW", "1.0.0", F45X_DIGEST_HEADER_VERSION, 0x11);
    auto data2 = MakeF45xModuleData(1024, "RECOG", "2.0.0", F45X_DIGEST_HEADER_VERSION, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("mod1"), data1},
        {std::string("mod2"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF45x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[0].size == data1.size());
    REQUIRE(modules[1].name == "RECOG");
    REQUIRE(modules[1].size == data2.size());
}
