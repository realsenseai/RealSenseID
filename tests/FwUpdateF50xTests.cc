// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#include "FwUpdateTestHelpers.h"
#include "FwUpdate/F50x/Utilities.h"

#include <catch2/catch_test_macros.hpp>

// ============================================================================
// Tests: CalculateCRC
// ============================================================================
TEST_CASE("FwUpdate - CalculateCRC known values", "[FwUpdate][CRC]")
{
    SECTION("All-zeros buffer")
    {
        uint32_t data[4] = {0, 0, 0, 0};
        auto crc = RealSenseID::FwUpdateCommon::CalculateCRC(0, data, sizeof(data));
        // CRC of all zeros should be deterministic and non-zero
        REQUIRE(crc != 0);
        // Calling again with same data should produce the same result
        auto crc2 = RealSenseID::FwUpdateCommon::CalculateCRC(0, data, sizeof(data));
        REQUIRE(crc == crc2);
    }

    SECTION("Different initial CRC values produce different results")
    {
        uint32_t data[4] = {1, 2, 3, 4};
        auto crc0 = RealSenseID::FwUpdateCommon::CalculateCRC(0, data, sizeof(data));
        auto crc1 = RealSenseID::FwUpdateCommon::CalculateCRC(1, data, sizeof(data));
        REQUIRE(crc0 != crc1);
    }

    SECTION("Different data produces different CRC")
    {
        uint32_t data_a[4] = {0x11, 0x22, 0x33, 0x44};
        uint32_t data_b[4] = {0x11, 0x22, 0x33, 0x45};
        auto crc_a = RealSenseID::FwUpdateCommon::CalculateCRC(0, data_a, sizeof(data_a));
        auto crc_b = RealSenseID::FwUpdateCommon::CalculateCRC(0, data_b, sizeof(data_b));
        REQUIRE(crc_a != crc_b);
    }

    SECTION("Empty buffer (size=0)")
    {
        uint32_t dummy = 0;
        auto crc = RealSenseID::FwUpdateCommon::CalculateCRC(0, &dummy, 0);
        // With size=0, the loop doesn't execute, result is crc ^ ~0U ^ ~0U = crc = 0
        REQUIRE(crc == 0);
    }
}

// ============================================================================
// Tests: LoadFileToBuffer
// ============================================================================
TEST_CASE("FwUpdate - LoadFileToBuffer", "[FwUpdate][LoadFile]")
{
    SECTION("Loads file with padding")
    {
        TempFile tf;
        std::vector<unsigned char> content = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        WriteFile(tf.path, content);

        size_t aligned_size = 16; // larger than actual size
        auto buf = RealSenseID::FwUpdateCommon::LoadFileToBuffer(tf.path, aligned_size, content.size(), 0);
        REQUIRE(buf.size() == aligned_size);
        // First bytes match content
        REQUIRE(std::memcmp(buf.data(), content.data(), content.size()) == 0);
        // Padding bytes are zero
        for (size_t i = content.size(); i < aligned_size; i++)
        {
            REQUIRE(buf[i] == 0);
        }
    }

    SECTION("Loads file with offset")
    {
        TempFile tf;
        std::vector<unsigned char> content = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
        WriteFile(tf.path, content);

        size_t offset = 4;
        size_t read_size = 4;
        auto buf = RealSenseID::FwUpdateCommon::LoadFileToBuffer(tf.path, read_size, read_size, offset);
        REQUIRE(buf.size() == read_size);
        REQUIRE(buf[0] == 0xEE);
        REQUIRE(buf[1] == 0xFF);
        REQUIRE(buf[2] == 0x11);
        REQUIRE(buf[3] == 0x22);
    }

    SECTION("Throws on non-existent file")
    {
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateCommon::LoadFileToBuffer("__no_such_file_12345__.bin", 100, 100, 0), std::runtime_error);
    }

    SECTION("Throws when aligned_size < size")
    {
        TempFile tf;
        std::vector<unsigned char> content(100, 0xAA);
        WriteFile(tf.path, content);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateCommon::LoadFileToBuffer(tf.path, 50, 100, 0), std::runtime_error);
    }
}

// ============================================================================
// Tests: F50x ParseUfifMetadata
// ============================================================================
TEST_CASE("FwUpdate F50x - ParseUfifMetadata", "[FwUpdate][F50x]")
{
    SECTION("Returns all metadata fields from valid file")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.2.3.0.sbin"), module_data}}, UFIF_SIG, UFIF_VER, /*otpEncVer=*/0, /*dbVer=*/11,
                      /*cfgVer=*/1, /*deviceType=*/1, /*secureBootEnabled=*/1);
        auto meta = RealSenseID::FwUpdateF50x::ParseUfifMetadata(tf.path);
        REQUIRE(meta.secureBootEnabled == 1);
        REQUIRE(meta.dbVersion == 11);
        REQUIRE(meta.configVersion == 1);
        REQUIRE(meta.deviceType == 1);
    }

    SECTION("Throws on non-existent file")
    {
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifMetadata("__no_such_file__.bin"), std::runtime_error);
    }

    SECTION("Throws on invalid signature")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.2.3.0.sbin"), module_data}}, 0xDEADBEEF);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifMetadata(tf.path), std::runtime_error);
    }
}

TEST_CASE("FwUpdate F50x - ParseUfifToModules single module", "[FwUpdate][F50x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    auto module_data = MakeModuleData(2048); // < 4K, will be 4K-aligned

    BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.2.3.0.sbin"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);

    auto& m = modules[0];
    REQUIRE(m.name == "OPFW"); // SBC maps to OPFW
    REQUIRE(m.version == "1.2.3.0");
    REQUIRE(m.size == 2048);
    REQUIRE(m.aligned_size == 4096); // (2048+4095) & 0xfffff000
    REQUIRE(m.blocks.size() == 1);   // 4096 / 4096 = 1 block
    REQUIRE(m.crc != 0);
}

TEST_CASE("FwUpdate F50x - ParseUfifToModules BOOT.INI module", "[FwUpdate][F50x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    // BOOT.INI module has min size 8, max 2048
    auto module_data = MakeModuleData(64);

    BuildUfifFile(tf.path, 1, {{std::string("fw/BOOT.INI"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].name == "BOOT");
    REQUIRE(modules[0].file_name.find("BOOT.INI") == 0);
}

TEST_CASE("FwUpdate F50x - ParseUfifToModules multi-module", "[FwUpdate][F50x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    auto data1 = MakeModuleData(2048, 0x11);
    auto data2 = MakeModuleData(1024, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("fw/SBC.1.0.0.0.sbin"), data1},
        {std::string("fw/RECOG.2.5.24.0.sbin"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[1].name == "RECOG");
    REQUIRE(modules[1].version == "2.5.24.0");
}

TEST_CASE("FwUpdate F50x - ParseUfifToModules multiple blocks", "[FwUpdate][F50x]")
{
    TempFile tf;
    const uint32_t block_size = 4096;
    // 10000 bytes -> aligned to 12288 (3*4K) -> 3 blocks
    auto module_data = MakeModuleData(10000, 0x55);

    BuildUfifFile(tf.path, 1, {{std::string("fw/RECOG.1.0.0.0.sbin"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == 10000);
    REQUIRE(modules[0].aligned_size == 12288);
    REQUIRE(modules[0].blocks.size() == 3);
}

TEST_CASE("FwUpdate F50x - ParseUfifToModules error cases", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;

    SECTION("Invalid signature")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}}, 0xBADBAD);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Invalid version (major mismatch)")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        // Version with different major byte (0x0200 instead of 0x0100)
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}}, UFIF_SIG, 0x0200);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Entry count = 0")
    {
        TempFile tf;
        BuildUfifFile(tf.path, 0, {});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Module size too small")
    {
        TempFile tf;
        // Non-BOOT module with size < 1024 (MIN_MODULE_SIZE)
        auto module_data = MakeModuleData(512);
        BuildUfifFile(tf.path, 1, {{std::string("fw/RECOG.1.0.0.0.sbin"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("CRC mismatch")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        ModuleSpec spec;
        spec.name = "SBC.1.0.0.0.sbin";
        spec.data = module_data;
        spec.crc_override = 0xDEADDEAD; // wrong CRC
        BuildUfifFile(tf.path, 1, {spec});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Non-existent file")
    {
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules("__no_such_file__.bin", block_size), std::runtime_error);
    }

    SECTION("Invalid module name (no regex match)")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("INVALID_NAME.bin"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }
}

// ============================================================================
// Tests: Malformed UFIF files (edge cases)
// ============================================================================
TEST_CASE("FwUpdate F50x - Malformed: truncated file", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;

    SECTION("File too short for header")
    {
        TempFile tf;
        // 16 bytes < 32-byte header
        std::vector<unsigned char> truncated(16, 0);
        WriteFile(tf.path, truncated);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Valid header but file truncated before entry headers")
    {
        TempFile tf;
        // Header claims 2 entries but file ends after header
        std::vector<unsigned char> buf;
        UfifFileHeader hdr {};
        hdr.sig = 0x46484655;
        hdr.ver = 0x0100;
        hdr.entryN = 2;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));
        WriteFile(tf.path, buf);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Valid header+entries but file truncated before module data")
    {
        TempFile tf;
        // Entry claims 2048 bytes but no module data follows
        auto module_data = MakeModuleData(2048);
        auto crc = ComputeModuleCRC(module_data);

        std::vector<unsigned char> buf;
        UfifFileHeader hdr {};
        hdr.sig = 0x46484655;
        hdr.ver = 0x0100;
        hdr.entryN = 1;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

        UfifEntryHeader entry {};
        std::snprintf(entry.name, sizeof(entry.name), "%s", "fw/SBC.1.0.0.0.sbin");
        entry.size = 2048;
        entry.crc32 = crc;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));
        // No module data written
        WriteFile(tf.path, buf);
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }
}

TEST_CASE("FwUpdate F50x - Malformed: entry count boundaries", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;

    SECTION("entryN = 33 (just above MAX_ENTRY_COUNT)")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        // entryN=33 exceeds MAX_ENTRY_COUNT
        BuildUfifFile(tf.path, 33, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("entryN = MAX (32) but file only has data for 1 module")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        // Claims 32 entries but only 1 provided
        BuildUfifFile(tf.path, 32, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }
}

TEST_CASE("FwUpdate F50x - Malformed: module size boundaries", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;

    SECTION("Module size at MAX_MODULE_SIZE (32MB) is accepted")
    {
        TempFile tf;
        auto module_data = MakeModuleData(32 * 1024 * 1024);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}});
        REQUIRE_NOTHROW(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size));
    }

    SECTION("Module size just above MAX_MODULE_SIZE is rejected")
    {
        TempFile tf;
        auto module_data = MakeModuleData(32 * 1024 * 1024 + 1);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("BOOT.INI at exactly MIN_BOOT_MODULE_SIZE (8) is accepted")
    {
        TempFile tf;
        auto module_data = MakeModuleData(8);
        BuildUfifFile(tf.path, 1, {{std::string("fw/BOOT.INI"), module_data}});
        REQUIRE_NOTHROW(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size));
    }

    SECTION("BOOT.INI at exactly MAX_BOOT_MODULE_SIZE (2048) is accepted")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("fw/BOOT.INI"), module_data}});
        REQUIRE_NOTHROW(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size));
    }

    SECTION("BOOT.INI above MAX_BOOT_MODULE_SIZE is rejected")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2049);
        BuildUfifFile(tf.path, 1, {{std::string("fw/BOOT.INI"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("BOOT.INI below MIN_BOOT_MODULE_SIZE is rejected")
    {
        TempFile tf;
        auto module_data = MakeModuleData(7);
        BuildUfifFile(tf.path, 1, {{std::string("fw/BOOT.INI"), module_data}});
        REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
    }

    SECTION("Non-BOOT module at exactly MIN_MODULE_SIZE (1024) is accepted")
    {
        TempFile tf;
        auto module_data = MakeModuleData(1024);
        BuildUfifFile(tf.path, 1, {{std::string("fw/RECOG.1.0.0.0.sbin"), module_data}});
        REQUIRE_NOTHROW(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size));
    }
}

TEST_CASE("FwUpdate F50x - Malformed: entry name not null-terminated", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto module_data = MakeModuleData(2048);

    // Name field filled with 'A' bytes, no null terminator
    std::vector<unsigned char> buf;
    UfifFileHeader hdr {};
    hdr.sig = 0x46484655;
    hdr.ver = 0x0100;
    hdr.entryN = 1;
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

    UfifEntryHeader entry {};
    std::memset(entry.name, 'A', UFIF_NAME_MAX);
    entry.size = static_cast<uint32_t>(module_data.size());
    entry.crc32 = ComputeModuleCRC(module_data);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));

    // Align and write module data
    size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
    buf.resize(aligned, 0);
    buf.insert(buf.end(), module_data.begin(), module_data.end());
    WriteFile(tf.path, buf);

    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - Malformed: empty file", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    WriteFile(tf.path, {});
    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - block_size = 0 throws", "[FwUpdate][F50x]")
{
    TempFile tf;
    auto module_data = MakeModuleData(2048);
    BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.1.0.0.0.sbin"), module_data}});
    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, 0), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - Malformed: entry name starts with null byte", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto module_data = MakeModuleData(2048);

    std::vector<unsigned char> buf;
    UfifFileHeader hdr {};
    hdr.sig = 0x46484655;
    hdr.ver = 0x0100;
    hdr.entryN = 1;
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

    UfifEntryHeader entry {};
    // Name is all zeros — empty string for regex
    entry.size = static_cast<uint32_t>(module_data.size());
    entry.crc32 = ComputeModuleCRC(module_data);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));

    size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
    buf.resize(aligned, 0);
    buf.insert(buf.end(), module_data.begin(), module_data.end());
    WriteFile(tf.path, buf);

    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - Malformed: path traversal in entry name", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto module_data = MakeModuleData(2048);

    BuildUfifFile(tf.path, 1, {{std::string("../../etc/SBC.1.0.0.0.sbin"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    // SBC still maps to OPFW despite path prefix
    REQUIRE(modules[0].name == "OPFW");
}

TEST_CASE("FwUpdate F50x - Malformed: second entry invalid rejects entire file", "[FwUpdate][F50x]")
{
    // Size validation of ALL entries happens upfront in UfifReadHeader
    const uint32_t block_size = 4096;
    TempFile tf;
    auto valid_data = MakeModuleData(2048);
    auto invalid_data = MakeModuleData(512); // below MIN_MODULE_SIZE for non-BOOT

    std::vector<ModuleSpec> specs = {
        {std::string("fw/SBC.1.0.0.0.sbin"), valid_data},
        {std::string("fw/RECOG.1.0.0.0.sbin"), invalid_data},
    };

    BuildUfifFile(tf.path, 2, specs);
    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - Malformed: partial module data", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto full_data = MakeModuleData(2048);

    std::vector<unsigned char> buf;
    UfifFileHeader hdr {};
    hdr.sig = 0x46484655;
    hdr.ver = 0x0100;
    hdr.entryN = 1;
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

    UfifEntryHeader entry {};
    std::snprintf(entry.name, sizeof(entry.name), "%s", "fw/SBC.1.0.0.0.sbin");
    entry.size = 2048;
    entry.crc32 = ComputeModuleCRC(full_data);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));

    size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
    buf.resize(aligned, 0);
    // Only 500 of the claimed 2048 bytes
    buf.insert(buf.end(), full_data.begin(), full_data.begin() + 500);
    WriteFile(tf.path, buf);

    REQUIRE_THROWS_AS(RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size), std::runtime_error);
}

TEST_CASE("FwUpdate F50x - Duplicate module names", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto data1 = MakeModuleData(2048, 0x11);
    auto data2 = MakeModuleData(2048, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("fw/SBC.1.0.0.0.sbin"), data1},
        {std::string("fw/SBC.1.0.0.0.sbin"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[1].name == "OPFW");
    REQUIRE(modules[0].crc != modules[1].crc);
}

TEST_CASE("FwUpdate F50x - Module size exactly 4K aligned", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto module_data = MakeModuleData(4096);

    BuildUfifFile(tf.path, 1, {{std::string("fw/RECOG.1.0.0.0.sbin"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == 4096);
    REQUIRE(modules[0].aligned_size == 4096);
    REQUIRE(modules[0].blocks.size() == 1);
}

TEST_CASE("FwUpdate F50x - Module size 4K+1 alignment", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;
    TempFile tf;
    auto module_data = MakeModuleData(4097);

    BuildUfifFile(tf.path, 1, {{std::string("fw/RECOG.1.0.0.0.sbin"), module_data}});

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 1);
    REQUIRE(modules[0].size == 4097);
    REQUIRE(modules[0].aligned_size == 8192);
    REQUIRE(modules[0].blocks.size() == 2);
}

TEST_CASE("FwUpdate F50x - Multi-module alignment gap", "[FwUpdate][F50x]")
{
    // 2050 is not 16-byte aligned, so padding is needed between modules
    const uint32_t block_size = 4096;
    TempFile tf;
    auto data1 = MakeModuleData(2050, 0x11);
    auto data2 = MakeModuleData(1024, 0x22);

    std::vector<ModuleSpec> specs = {
        {std::string("fw/SBC.1.0.0.0.sbin"), data1},
        {std::string("fw/RECOG.1.0.0.0.sbin"), data2},
    };

    BuildUfifFile(tf.path, 2, specs);

    auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "OPFW");
    REQUIRE(modules[0].size == 2050);
    REQUIRE(modules[1].name == "RECOG");
    REQUIRE(modules[1].size == 1024);
}

TEST_CASE("FwUpdate F50x - Malformed: version string edge cases", "[FwUpdate][F50x]")
{
    const uint32_t block_size = 4096;

    SECTION("Version with only dots")
    {
        TempFile tf;
        auto module_data = MakeModuleData(2048);
        BuildUfifFile(tf.path, 1, {{std::string("fw/SBC.....sbin"), module_data}});
        auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "OPFW");
        REQUIRE(!modules[0].version.empty());
    }

    SECTION("All recognized module types parse correctly")
    {
        std::vector<std::string> types = {"RECOG", "YOLO", "DNET", "NNLED", "SPOOFS", "ACCNET"};
        for (const auto& type : types)
        {
            TempFile tf;
            auto module_data = MakeModuleData(2048);
            std::string name = "fw/" + type + ".1.0.0.0.sbin";
            BuildUfifFile(tf.path, 1, {{name, module_data}});
            auto modules = RealSenseID::FwUpdateF50x::ParseUfifToModules(tf.path, block_size);
            REQUIRE(modules.size() == 1);
            REQUIRE(modules[0].name == type);
        }
    }
}
