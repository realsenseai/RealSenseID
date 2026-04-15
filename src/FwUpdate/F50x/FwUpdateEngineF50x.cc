// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "FwUpdateEngineF50x.h"
#include "Utilities.h"
#include "../Common/Common.h"
#include "Logger.h"
#include "Cmds.h"
#include "RealSenseID/DiscoverDevices.h"
#include "PacketManager/SerialPacket.h"
#include <thread>
#include <chrono>
#include <regex>
#include <sstream>
#include <cassert>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <string.h>

namespace RealSenseID
{
namespace FwUpdateF50x
{
static const char* LOG_TAG = "FwUpdater";

static const char* DumpFilename = "fw-update.log";
static const std::set<std::string> AllowedModules {"OPFW",    "NNLED",  "NNLEDR", "DNET",     "RECOG",    "ACCNET",
                                                   "YOLO",    "AS2DLR", "ASDISP", "SPOOFS",   "ASVIS",    "PDET",
                                                   "POSEDET", "POSELM", "POSE",   "BPARTDET", "FEATURES", "BOOT"};

static const std::string OPFW = "OPFW"; // Do not change
static const std::string BOOT = "BOOT"; // Do not change

using namespace std::chrono_literals;
/*
parse dlinfo response

if amount of blocks in device/host are the same - if not, update all blocks

else, decide which blocks need to be updated according to -
compare pre-calcualted block crcs in ModuleInfo to the ones we receive from device (HDR CRC)
HDR CRC == REAL CRC == OUR CRC
status == "OK"
return vector of bools to flag if need update(true) or not(false)

example dlinfo response to parse:

FW is empty

 or

dlinfo ack
DNET.6.2.24.0.SBIN info
total 3230445, blkSz 524288
blk  state HDR CRC  Real CRC
#0   OK    b8061296 b8061296
#1   OK    3c26a6d2 3c26a6d2
#2   OK    c21034d4 c21034d4
#3   OK    747705fb 747705fb
#4   OK    d52c9065 d52c9065
#5   OK    7fcb9359 7fcb9359
#6   OK    2f681760 2f681760
dlinfo end

*/
bool FwUpdateEngineF50x::ShouldUpdate(const ModuleInfo& module)
{
    _comm->ConsumeScanned();
    _comm->WriteCmd(F50xCmds::dlinfo(module.file_name));
    _comm->WaitForStr("dlinfo end", 1000ms);

    // all blocks should be updated until proven otherwise ("Ok" status and CRCs match: HDR==Real==Host)
    char* logBuf = _comm->GetScanPtr();
    // if "empty" encountered, all blocks need to be updated
    if (strstr(logBuf, "empty") != nullptr)
    {
        _comm->ConsumeScanned();
        return true;
    }
    const char* cur_input = logBuf;
    size_t records_found = 0;
    while (true)
    {
        char state_str[16] = {0};
        unsigned int block_number, hdrCrc, fwCrc;
        const char* p = strchr(cur_input, '#');
        if (p == NULL)
        {
            // no more records found
            break;
        }

        int n = sscanf(p + 1, "%u %15s %x %x", &block_number, state_str, &hdrCrc, &fwCrc);
        if (n < 4)
        {
            cur_input = p + 1;
            continue; // ignore lines withot the form
        }

        // some healthy asserts
        assert(strlen(state_str) != 0);
        assert(hdrCrc != 0);
        assert(fwCrc != 0);

        records_found++;
        if (block_number >= module.blocks.size())
        {
            // if number of blocks is differnt from host return immediatly and update all blocks
            LOG_DEBUG(LOG_TAG, "Block number(%u) not found in host. Should update", block_number);
            _comm->ConsumeScanned();
            return true;
        }

        auto host_block_crc = module.blocks[block_number].crc;
        // update if not ok or if one of the crcs differs from the others
        bool should_update = strcmp(state_str, "OK") != 0 || hdrCrc != fwCrc || hdrCrc != host_block_crc;
        if (should_update)
        {
            LOG_DEBUG(LOG_TAG, "Block #%u: fw: %s 0x%08x 0x%08x, local: 0x%08x. Should update", block_number, state_str, hdrCrc, fwCrc,
                      host_block_crc);
            _comm->ConsumeScanned();
            return true;
        }
        cur_input = p + 9; // prepare to search next record (strchr(cur_input, '#'))
    }
    bool is_same = records_found == module.blocks.size();
    if (!is_same)
    {
        LOG_DEBUG(LOG_TAG, "Number of blocks differ: %zu (host) vs %zu (device). Should update", module.blocks.size(), records_found);
    }
    else
    {
        LOG_DEBUG(LOG_TAG, "All %d blocks match; no update is required", records_found);
    }
    _comm->ConsumeScanned();
    return !is_same;
}

// parse 'dl' ack
bool FwUpdateEngineF50x::ParseDlResponse(const std::string& name, size_t blkNo, size_t sz)
{
    char* logBuf = _comm->GetScanPtr();
    char str[64];
    ::snprintf(str, sizeof(str), "%s : blk %zu sz=%zu", name.c_str(), blkNo, sz);
    bool ack = strstr(logBuf, str) != NULL;

    if (!ack)
    {
        LOG_ERROR(LOG_TAG, "ParseDlResponse: cannot find %s", str);
        LOG_DEBUG(LOG_TAG, "logbuf:\n%s", logBuf);
    }
    _comm->ConsumeScanned();
    return ack;
}

// parse 'dl' send buffer result - return true if 'dl ret=0' is returned from device, false otherwise
bool FwUpdateEngineF50x::ParseDlBlockResult()
{
    const char* dlRetStr = "dl ret=";
    char* logBuf = _comm->GetScanPtr();
    char* p = strstr(logBuf, dlRetStr);
    int result = -1;
    bool rv = false;
    if (p)
    {
        p += strlen(dlRetStr);
        uint32_t n = sscanf(p, "%d", &result);
        if (n != 1)
        {
            rv = false;
        }
        else
        {
            rv = result == 0;
        }
    }

    _comm->ConsumeScanned();
    if (!rv)
    {
        LOG_ERROR(LOG_TAG, "ParseDlBlockResult: dl ret=%d\n%s", result, logBuf);
    }
    return rv;
}

// tokenize by , and print each token in a new line
static void DebugBootIni(const unsigned char* sendBuf, size_t size)
{
    std::string boot_ini((const char*)sendBuf, size);
    std::string debug_str = std::regex_replace(boot_ini, std::regex(","), "\n\t");
    LOG_DEBUG(LOG_TAG, "\nBoot.ini :\n\t%s", debug_str.c_str());
}

void FwUpdateEngineF50x::BurnModule(ProgressTick tick, const ModuleInfo& module, const Buffer& buffer, bool force_full)
{
    // if no updated required, just tick the progress and return
    auto should_update = force_full || ShouldUpdate(module);
    if (!should_update)
    {
        for (const auto& block : module.blocks)
            tick();
        return;
    }

    // send dlinit
    _comm->WriteCmd(F50xCmds::dlinit(module.file_name, module.size));

    // check for err string which arrives shortly after the dlinit ack
    std::this_thread::sleep_for(50ms);
    char* logBuf = _comm->GetScanPtr();
    _comm->ConsumeScanned();
    if (strstr(logBuf, "err ") != nullptr)
    {
        LOG_ERROR(LOG_TAG, "dlinit returned err. Closing session. Please retry");
        throw std::runtime_error("dlinit returned err");
    }

    // send CRCs of all blocks to fw as binary array of [n x uin32_t] bytes (little endian)
    std::vector<uint32_t> blkCrc;
    for (const auto& block : module.blocks)
    {
        blkCrc.push_back(block.crc);
    }
    auto crcSendSize = blkCrc.size() * sizeof(uint32_t);
    _comm->WriteBinary((const char*)blkCrc.data(), crcSendSize);
    _comm->ConsumeScanned();

    LOG_INFO(LOG_TAG, "Starting module %s update", module.name.c_str());
    for (size_t blockNumber = 0; blockNumber < module.blocks.size(); ++blockNumber)
    {
        LOG_DEBUG(LOG_TAG, "Module %s, block #%d, updating...", module.name.c_str(), blockNumber);
        const auto& block = module.blocks[blockNumber];
        auto sendSz = block.size;

        if (buffer.size() <= block.offset || buffer.size() - block.offset < sendSz)
        {
            throw std::runtime_error("Invalid buffer or block size");
        }
        const unsigned char* sendBuf = buffer.data() + block.offset;
        if (module.name == BOOT)
        {
            DebugBootIni(sendBuf, sendSz);
        }

        _comm->WriteCmd(F50xCmds::dl(module.file_name, blockNumber));
        _comm->WaitForIdle();
        bool dlAck = ParseDlResponse(module.file_name, blockNumber, sendSz);
        if (!dlAck)
        {
            throw std::runtime_error("Did not receive 'dl ack'");
        }

        _comm->WriteBinary((char*)sendBuf, sendSz);
        auto timeoutMs = 2000 * BlockSize / (64 * 1024);
        _comm->WaitForStr("dl ret=", std::chrono::milliseconds {timeoutMs});
        auto dlErr = ParseDlBlockResult();
        if (!dlErr)
        {
            throw std::runtime_error("dl block failed");
        }

        tick();
    }
    // update finished - check block crcs again. they all should match now
    if (ShouldUpdate(module))
    {
        throw std::runtime_error("Update failed");
    }
}

void FwUpdateEngineF50x::BurnSelectModules(const ModuleVector& modules, ProgressTick tick, bool force_full)
{
    size_t module_count = 0;
    for (const auto& module : modules)
    {
        module_count++;

        auto buffer = FwUpdateCommon::LoadFileToBuffer(module.filename, module.aligned_size, module.size, module.file_offset);
        if (buffer.empty())
        {
            throw std::runtime_error("Failed loading firmware file");
        }
        LOG_INFO(LOG_TAG, "*************************************************************************");
        LOG_INFO(LOG_TAG, "                %s (%0.2f MB, %zu blocks)", module.file_name.c_str(),
                 static_cast<double>(buffer.size()) / 1048576.0, module.blocks.size());
        LOG_INFO(LOG_TAG, "*************************************************************************");
        BurnModule(tick, module, buffer, force_full);
        LOG_INFO(LOG_TAG, "");
    }
}

// use multiple dlinfo to detect which modules from the uploaded fw file uploaded to the device.
// a module should be updated if it doesn't exist in the device or at least one of its blocks needs to be updated (CRC
// mismatch)
bool FwUpdateEngineF50x::FindDirtyModules(const ModuleVector& modules)
{
    return std::any_of(modules.begin(), modules.end(), [this](const ModuleInfo& module) { return ShouldUpdate(module); });
}

ModuleVector FwUpdateEngineF50x::ModulesFromFile(const std::string& path)
{
    LOG_INFO(LOG_TAG, "Extract modules from \"%s\"", path.c_str());
    auto modules = ParseUfifToModules(path, BlockSize);
    // validate that we get known module names
    for (const auto& module : modules)
    {
        if (AllowedModules.find(module.name) == AllowedModules.end())
            throw std::runtime_error("Found invalid module name in file: " + module.name);
    }

    LOG_INFO(LOG_TAG, "Extracted %zu modules", modules.size());
    LOG_DEBUG(LOG_TAG, "");
    return modules;
}

void FwUpdateEngineF50x::BurnModules(const Settings& settings, const ModuleVector& modules, ProgressCallback on_progress)
{
    if (modules.empty())
    {
        LOG_ERROR(LOG_TAG, "Received empty modules list");
        return;
    }


    // make sure there is exactly one boot module at the end since it is critical
    for (size_t i = 0; i < modules.size(); i++)
    {
        auto is_last = i == modules.size() - 1;
        const auto& module_name = modules[i].name;
        if ((!is_last && module_name == BOOT) || (is_last && module_name != BOOT))
        {
            throw std::runtime_error("BOOT module must be as the last module");
        }
    }

    size_t total_number_of_blocks = 0;
    for (const auto& module : modules)
        total_number_of_blocks += module.blocks.size();

    if (total_number_of_blocks == 0)
    {
        LOG_ERROR(LOG_TAG, "total_number_of_blocks is zero");
        return;
    }
    // calculate the effect each block has on the overall progress
    float progress_delta = 1.0f / static_cast<float>(total_number_of_blocks);

    float overall_progress = 0.0f;
    // wrap external progress callback with a "tick progress" lambda, called every time a block is sent.
    auto progress_tick = [progress_delta, on_progress, &overall_progress]() {
        overall_progress += progress_delta;
        on_progress(overall_progress);
    };


    _comm = std::make_unique<FwUpdaterCommF50x>(settings.serial_config);
    // 1. selt baudrate speed
    // 2. clean device from previous update leftovers
    // 3. burn modules
    try
    {
        on_progress(0.0f);
        _comm->WaitForIdle();
        _comm->WriteCmd(F50xCmds::dlspd(settings.baud_rate), true);
        std::this_thread::sleep_for(50ms);
        // clean to make sure we have enough space
        _comm->WriteCmd(F50xCmds::dlclean());
        _comm->WaitForIdle();
        _comm->ConsumeScanned();
        BurnSelectModules(modules, progress_tick, settings.force_full);
        _comm->StopReaderThread();
        _comm->WriteCmd(PacketManager::Commands::reset, false);
        _comm->DumpSession(DumpFilename);
        on_progress(1.0f);
    }
    catch (const std::exception&)
    {
        _comm->DumpSession(DumpFilename);
        _comm.reset();
        throw;
    }
}
} // namespace FwUpdateF50x
} // namespace RealSenseID