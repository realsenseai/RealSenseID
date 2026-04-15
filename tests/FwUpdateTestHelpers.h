// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.
#pragma once

#include "FwUpdate/Common/Common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// UFIF binary file constants (must match the production code)
// ============================================================================
static constexpr uint32_t UFIF_SIG = 0x46484655;
static constexpr uint16_t UFIF_VER = 0x0100;
static constexpr uint32_t UFIF_ALIGN = 16;
static constexpr uint32_t UFIF_NAME_MAX = 64;

// ============================================================================
// Helpers: RAII temp file, UFIF builder
// ============================================================================

// RAII temp file that deletes itself on destruction
struct TempFile
{
    std::string path;

    TempFile()
    {
#ifdef _WIN32
        char buf[L_tmpnam_s];
        if (tmpnam_s(buf, sizeof(buf)) != 0)
            throw std::runtime_error("tmpnam_s failed");
        path = buf;
#else
        char buf[L_tmpnam];
        if (!std::tmpnam(buf))
            throw std::runtime_error("tmpnam failed");
        path = buf;
#endif
    }

    ~TempFile()
    {
        std::remove(path.c_str());
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// Writes raw bytes to a file
inline void WriteFile(const std::string& path, const std::vector<unsigned char>& data)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open file for writing: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Aligns offset up to alignment boundary
inline size_t AlignUp(size_t offset, size_t alignment)
{
    auto rem = offset % alignment;
    return rem ? offset + (alignment - rem) : offset;
}

// Packed UFIF structs for building test files
#pragma pack(push, 1)
struct UfifFileHeader
{
    uint32_t sig;
    uint16_t ver;
    uint16_t entryN;
    uint8_t otpEncryptVersion; // OTP encryption key slot version
    uint8_t uispPackVersion;   // uisp pack format version (ICATCH_UISP_PACK_VER)
    uint8_t dbVersion;         // on-device face DB format version (ICATCH_DB_VER)
    uint8_t configVersion;     // auth config struct version (ICATCH_CONFIG_VER)
    uint8_t deviceType;        // target device: 0=F450, 1=F460, 2=F500
    uint8_t secureBootEnabled; // CSS-signed firmware (F460/F500 only; always 0 for F450)
    uint8_t rsv[18];
};

struct UfifEntryHeader
{
    char name[UFIF_NAME_MAX];
    uint32_t size;
    uint32_t crc32;
    uint8_t rsv[8];
};
#pragma pack(pop)

static_assert(sizeof(UfifFileHeader) == 32, "UfifFileHeader must be 32 bytes");
static_assert(sizeof(UfifEntryHeader) == 80, "UfifEntryHeader must be 80 bytes");

// Computes CRC for a module (4-byte-aligned size), matching production code
inline uint32_t ComputeModuleCRC(const std::vector<unsigned char>& data)
{
    uint32_t crc_size = (static_cast<uint32_t>(data.size()) + 3) & ~3u;
    // Zero-pad to 4-byte alignment for CRC
    std::vector<unsigned char> padded = data;
    padded.resize(crc_size, 0);
    return RealSenseID::FwUpdateCommon::CalculateCRC(0, padded.data(), crc_size);
}

// Describes a module to add to a UFIF file
struct ModuleSpec
{
    std::string name; // entry name (e.g., "SBC.1.2.3.0.sbin" or "BOOT.INI")
    std::vector<unsigned char> data;
    uint32_t crc_override = 0; // if non-zero, use this instead of computed CRC
};

// Builds a UFIF binary file from specs and writes it to `path`
inline void BuildUfifFile(const std::string& path, uint16_t entryN, const std::vector<ModuleSpec>& modules, uint32_t sig = UFIF_SIG,
                          uint16_t ver = UFIF_VER, uint8_t otpEncVer = 0, uint8_t dbVer = 0, uint8_t cfgVer = 0, uint8_t deviceType = 0,
                          uint8_t secureBootEnabled = 0)
{
    std::vector<unsigned char> buf;

    // File header
    UfifFileHeader hdr {};
    hdr.sig = sig;
    hdr.ver = ver;
    hdr.entryN = entryN;
    hdr.otpEncryptVersion = otpEncVer;
    hdr.dbVersion = dbVer;
    hdr.configVersion = cfgVer;
    hdr.deviceType = deviceType;
    hdr.secureBootEnabled = secureBootEnabled;
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&hdr), reinterpret_cast<uint8_t*>(&hdr) + sizeof(hdr));

    // Entry headers
    for (uint16_t i = 0; i < entryN; i++)
    {
        UfifEntryHeader entry {};
        if (i < modules.size())
        {
            std::snprintf(entry.name, sizeof(entry.name), "%s", modules[i].name.c_str());
            entry.size = static_cast<uint32_t>(modules[i].data.size());
            entry.crc32 = modules[i].crc_override ? modules[i].crc_override : ComputeModuleCRC(modules[i].data);
        }
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&entry), reinterpret_cast<uint8_t*>(&entry) + sizeof(entry));
    }

    // Module data (16-byte aligned)
    for (uint16_t i = 0; i < entryN && i < modules.size(); i++)
    {
        // Align to 16 bytes
        size_t aligned = AlignUp(buf.size(), UFIF_ALIGN);
        buf.resize(aligned, 0);
        buf.insert(buf.end(), modules[i].data.begin(), modules[i].data.end());
    }

    WriteFile(path, buf);
}

// Creates module data of a given size filled with a repeating byte pattern
inline std::vector<unsigned char> MakeModuleData(size_t size, unsigned char fill = 0xAB)
{
    return std::vector<unsigned char>(size, fill);
}
