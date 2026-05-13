// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// ========================
// Firmware File Structure
// ========================
//
// The firmware binary (.bin) is a UFIF container holding one or more modules.
// The file is read sequentially: a fixed-size file header, followed by an array
// of entry descriptors, followed by the module data blobs.
//
// File layout:
//
//   Offset 0
//   +------------------------------------------+
//   | UfifFile header (32 bytes)               |
//   |   sig          : uint32  (0x46484655)    |  Magic signature
//   |   ver          : uint16  (0x0100)        |  Format version (major in high byte)
//   |   entryN       : uint16                  |  Number of module entries (1..32)
//   |   otpEncryptVer: uint8                   |  OTP encryption version
//   |   rsv          : uint8[23]               |  Reserved / padding
//   +------------------------------------------+
//   | UfifEntry[0] (80 bytes)                  |
//   |   name         : char[64]                |  Null-terminated module filename
//   |   size         : uint32                  |  Module data size in bytes
//   |   crc32        : uint32                  |  CRC32 of the module data
//   |   rsv          : uint8[8]                |  Reserved
//   +------------------------------------------+
//   | UfifEntry[1] (80 bytes)                  |
//   |   ...                                    |
//   +------------------------------------------+
//   | ...                                      |
//   +------------------------------------------+
//   | UfifEntry[entryN-1] (80 bytes)           |
//   +------------------------------------------+
//   <16-byte alignment padding if needed>
//   +------------------------------------------+
//   | Module 0 data (entry[0].size bytes)      |
//   +------------------------------------------+
//   <16-byte alignment padding if needed>
//   +------------------------------------------+
//   | Module 1 data (entry[1].size bytes)      |
//   +------------------------------------------+
//   | ...                                      |
//   +------------------------------------------+
//
// SKU compatibility:
//   The file header's secureBootEnabled field indicates whether firmware is CSS-signed (F460/F500 only).
//   Before flashing, the device's bspver string is queried for "(secure)" to determine its SKU.
//   Flashing proceeds only if the firmware and device SKUs match.
//
// Alignment and blocking:
//   - In the file, module data offsets are aligned to 16 bytes (UFIF_ALIGN).
//   - In memory, module data is zero-padded to 4K alignment for transfer.
//   - Each 4K-aligned module is split into fixed-size blocks for CRC
//     verification and transfer to the device.
//   - CRC is computed over 4-byte aligned data size.

#include "Utilities.h"
#include "../Common/Common.h"
#include "Logger.h"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <regex>
#include <sstream>

namespace RealSenseID
{
namespace FwUpdateF50x
{
static const char* LOG_TAG = "FwUpdateF50x";

// file structure
static constexpr uint32_t UFIF_ALIGN = 16;
static constexpr uint32_t UFIF_SIG = 0x46484655;
static constexpr uint32_t UFIF_VER = 0x0100;
static constexpr uint32_t UFIF_NAME_MAX = 64;
static constexpr uint32_t DIGEST_HEADER_VERSION = 0x00000004;
static constexpr uint32_t DIGEST_HEADER_VERSION_SIZE = 12;

// module sizes
static constexpr uint32_t MIN_MODULE_SIZE = 1024;
static constexpr uint32_t MAX_MODULE_SIZE = 32 * 1024 * 1024;
static constexpr uint32_t MIN_BOOT_MODULE_SIZE = 8;
static constexpr uint32_t MAX_BOOT_MODULE_SIZE = 2 * 1024;

// entry count
static constexpr uint16_t MIN_ENTRY_COUNT = 1;
static constexpr uint16_t MAX_ENTRY_COUNT = 32;

struct UfifFile
{
    uint32_t sig;
    uint16_t ver;
    uint16_t entryN;
    uint8_t otpEncryptVersion; // OTP encryption key slot version (F450 only; 0=SKU1, 1=SKU2)
    uint8_t uispPackVersion;   // uisp pack format version (ICATCH_UISP_PACK_VER)
    uint8_t dbVersion;         // on-device face DB format version (ICATCH_DB_VER)
    uint8_t configVersion;     // auth config struct version (ICATCH_CONFIG_VER)
    uint8_t deviceType;        // target device: 0=F450, 1=F460, 2=F500
    uint8_t secureBootEnabled; // CSS-signed firmware (F460/F500 only; always 0 for F450)
    uint8_t rsv[18];
};

struct UfifEntry
{
    char name[UFIF_NAME_MAX];
    uint32_t size;
    uint32_t crc32;
    uint8_t rsv[8];
};


static bool UfifCheckHeader(const UfifFile& header)
{
    LOG_DEBUG(LOG_TAG,
              "UFIF header: sig=0x%08x, ver=0x%04x, entries=%u, otpEncVer=%u, uispPackVer=%u, dbVer=%u, configVer=%u, deviceType=%u, "
              "secureBootEnabled=%u",
              header.sig, header.ver, header.entryN, header.otpEncryptVersion, header.uispPackVersion, header.dbVersion,
              header.configVersion, header.deviceType, header.secureBootEnabled);
    if (header.sig != UFIF_SIG || (header.ver >> 8) != (UFIF_VER >> 8))
    {
        LOG_TRACE(LOG_TAG, "ufif header err, sig:%x != %x, ver:%x != %x", header.sig, UFIF_SIG, header.ver >> 8, UFIF_VER >> 8);
        return false;
    }
    if (header.entryN < MIN_ENTRY_COUNT || header.entryN > MAX_ENTRY_COUNT)
    {
        LOG_ERROR(LOG_TAG, "Invalid number of entries: %u", header.entryN);
        return false;
    }
    return true;
}

static std::vector<UfifEntry> UfifReadHeader(std::ifstream& file, UfifFile& header)
{
    if (!file.read((char*)&header, sizeof(UfifFile)))
    {
        throw std::runtime_error("Error while reading ufifFile_t");
    }


    if (!UfifCheckHeader(header))
    {
        throw std::runtime_error("Error while validating ufif header");
    }

    // read headers entries from file
    std::vector<UfifEntry> rv(header.entryN);
    if (!file.read((char*)rv.data(), rv.size() * sizeof(UfifEntry)))
    {
        throw std::runtime_error("Error while reading ufifEntries");
    }

    // validate sizes of the UfifEntry vector
    for (const auto& entry : rv)
    {
        std::string name(entry.name, sizeof(entry.name) - 1);
        bool is_boot = name.find("BOOT.INI") != std::string::npos;
        uint32_t min_size = is_boot ? MIN_BOOT_MODULE_SIZE : MIN_MODULE_SIZE;
        uint32_t max_size = is_boot ? MAX_BOOT_MODULE_SIZE : MAX_MODULE_SIZE;
        if (entry.size < min_size || entry.size > max_size)
        {
            throw std::runtime_error("Invalid module size in " + name);
        }
    }
    return rv;
}

static UfifFile ReadUfifFile(const std::string& path)
{
    std::ifstream ifile(path, std::ios::binary);
    if (!ifile)
    {
        throw std::runtime_error("Error while trying to read project header");
    }
    UfifFile header;
    UfifReadHeader(ifile, header);
    return header;
}

UfifMetadata ParseUfifMetadata(const std::string& path)
{
    auto h = ReadUfifFile(path);
    return {h.otpEncryptVersion, h.dbVersion, h.configVersion, h.deviceType, h.secureBootEnabled};
}


ModuleVector ParseUfifToModules(const std::string& path, const uint32_t block_size)
{
    if (block_size == 0)
    {
        throw std::runtime_error("block_size must be > 0");
    }

    std::ifstream ifile(path, std::ios::binary);
    if (!ifile)
    {
        throw std::runtime_error("Error while trying to read project header");
    }
    UfifFile header;
    auto entries = UfifReadHeader(ifile, header);
    ModuleVector result;
    LOG_DEBUG(LOG_TAG, "Header Entry size = %d", entries.size());

    for (const auto& entry : entries)
    {
        auto ofs = ifile.tellg();
        if (ofs == -1)
        {
            throw std::runtime_error("tellg failed");
        }
        if (ofs % UFIF_ALIGN)
        {
            ofs += UFIF_ALIGN - ofs % UFIF_ALIGN;
            if (!ifile.seekg(ofs))
            {
                throw std::runtime_error("seekg failed");
            }
        }
        // 4k aligned module size
        auto aligned_buffer_size = (entry.size + 4095) & 0xfffff000;
        auto n_blocks = aligned_buffer_size / block_size;
        if (aligned_buffer_size % block_size)
        {
            n_blocks++;
        }

        std::string module_name;
        std::string module_file_name;
        std::string module_version;
        // regex groups to match: module_name, version, extension
        static const std::regex rgx {
            R"((.+)(SBC|NNLED|NNLEDR|DNET|RECOG|YOLO|AS2DLR|ASDISP|SPOOFS|ACCNET|ASVIS|PDET|POSEDET|POSELM|POSE|BPARTDET|FEATURES)\.([\d\.]+)\.(.+))",
            std::regex_constants::icase};
        std::smatch match;
        std::string line = std::string(entry.name, sizeof(entry.name) - 1);
        auto match_ok = std::regex_search(line, match, rgx);
        if (match_ok == false)
        {
            // Check for BOOT.INI file
            static const std::regex boot_rgx {R"((.+)(BOOT)\.(.+))"};
            auto match_ok = std::regex_search(line, match, boot_rgx);
            if (match_ok == false)
            {
                throw std::runtime_error("Invalid module name in " + line);
            }

            module_name = match[2].str();
            // module_file_name = module_name.<extension> : Example: BOOT.INI
            module_file_name = module_name + '.' + match[3].str();
        }
        else
        {
            module_name = match[2].str();
            module_version = match[3].str();
            if (module_name == "SBC")
            {
                module_name = "OPFW";
                // For OPFW maodule the file name is of format SBC.<W>.<X>.<Y>.<Z>.<ext>
                module_file_name = "SBC." + module_version + '.' + match[4].str();
            }
            else
            {
                // module_file_name = module_name.module_version.<extension> : Example: FEATURES.2.5.24.0.sbin
                module_file_name = module_name + '.' + module_version + '.' + match[4].str();
            }
            assert(!module_version.empty());
        }
        assert(!module_name.empty());
        assert(!module_file_name.empty());
        // Convert to upper case for future comparisons.
        std::transform(module_file_name.begin(), module_file_name.end(), module_file_name.begin(), ::toupper);

        LOG_DEBUG(LOG_TAG, "[%s] %0.2f MB, %u blocks", module_file_name.c_str(), entry.size / 1048576.0, n_blocks);

        // buffer to store module data,
        // init with zeroes so alignment bytes(aligned_buffer_size-entry.size) are all zeroes
        std::vector<unsigned char> module_buffer(aligned_buffer_size, 0);

        if (!ifile.read((char*)&module_buffer[0], entry.size))
        {
            throw std::runtime_error("Failed reading module from file");
        }

        // crc sz must be 4-aligned
        uint32_t crc_aligned_data_size = (entry.size + 3) & ~3;
        auto whole_module_crc = FwUpdateCommon::CalculateCRC(0, module_buffer.data(), crc_aligned_data_size);
        if (whole_module_crc != entry.crc32)
        {
            throw std::runtime_error("Invalid crc field in module " + module_name);
        }

        ModuleInfo module_info;
        module_info.crc = whole_module_crc;
        module_info.name = module_name;
        module_info.version = module_version;
        module_info.filename = path;
        module_info.file_name = module_file_name;
        module_info.file_offset = ofs;
        module_info.size = entry.size;
        module_info.aligned_size = aligned_buffer_size;
        uint32_t block_crc_size = 0;
        size_t block_size_min = 0;

        for (unsigned i = 0; i < n_blocks; i++)
        {
            BlockInfo block;
            block.offset = i * static_cast<size_t>(block_size);
            block_size_min =
                (block.offset + static_cast<size_t>(block_size) > module_info.size) ? (module_info.size - block.offset) : block_size;
            block_crc_size = std::min(crc_aligned_data_size, block_size);
            block.size = std::min(static_cast<size_t>(block_crc_size), block_size_min);
            block.crc = FwUpdateCommon::CalculateCRC(i, &module_buffer[block.offset], block_crc_size);
            crc_aligned_data_size -= block_crc_size;
            module_info.blocks.push_back(block);
        }

        LOG_DEBUG(LOG_TAG, "Updated Entry Size = %d", module_info.size);

        result.push_back(module_info);
    }
    return result;
}

} // namespace FwUpdateF50x
} // namespace RealSenseID
