// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

//
// Serial packet spec (little endian for all uint16_t and uint32_t fields)
//

#include <cstdint>
#include <cstddef>

#pragma pack(push)
#pragma pack(1)

namespace RealSenseID
{
namespace PacketManager
{
static constexpr unsigned char ProtocolVer = 3;
static constexpr std::size_t MaxUserIdSize = 30;

struct FaMessage
{
    char user_id[MaxUserIdSize + 1]; // ascii only. '\0' terminated
    char fa_status;                  // ascii status number (e.g. '0', '1', etc.)
    char reserved[8];                // 8 bytes for reserved data
};

struct DataMessage
{
    char data[8124]; // any binary data to complete packet total size to exactly 8k
};

enum class SyncByte : char
{
    Sync1 = '@',
    Sync2 = 'F'
};

enum class MsgId : char
{
    None = '-',

    // Min value for fa messages
    MinFa = 'A',
    Authenticate = 'A',
    DetectSpoof = 'B',
    RemoveAllUsers = 'C',
    RemoveUser = 'D',
    Enroll = 'E',
    SaveDebug = 'F',
    EnrollImageOneToOne = 'G',
    Hint = 'H',
    EnrollImage = 'I',
    EnrollImageFeatureExtraction = 'J',
    AuthenticateOneToOne = 'K',
    AuthenticateLoop = 'L',
    AuthenticateImgOneToOne = 'M',
    SecureFaceprintsEnroll = 'N',
    // = 'O',
    Progress = 'P',
    SecureFaceprintsAuthenticate = 'Q',
    Result = 'R',
    SaveDatabase = 'S',
    EnrollFaceprintsExtraction = 'T',
    Unlock = 'U',
    // = 'V'
    AuthenticateFaceprintsExtractionLoop = 'W',
    AuthenticateFaceprintsExtraction = 'X',
    Reply = 'Y',
    MaxFa = 'Z',
    // Max value for fa messages

    HostEcdsaKey = 'a',
    DeviceEcdsaKey = 'b',
    HostEcdhKey = 'c',
    DeviceEcdhKey = 'd',
    UploadImage = 'e',
    Faceprints = 'f',
    FaceDetected = 'g',
    LandmarksDetected = 'h',
#ifdef SECURITY_EXTENSIONS
    SecureFaceprintsBeginSecureSession = 'i',
    SecureFaceprintsEndSecureSession = 'j',
    SecureFaceprintsOnSecureSessionReady = 'k',
    SecureFaceprintsOnSecureSessionCmd = 'l',
    SecureFaceprintsOnSecureSessionCmdResp = 'm',
#else
    PersonDetected = 'i',
    PoseDetected = 'j',
    // = 'k',
    // = 'l',
#endif
    FaceDistances = 'm',
    GetNumberOfUsers = 'n',
    StartSession = 'o',
    Ping = 'p',
    QueryDeviceConfig = 'q',
    SecureFaceprintsFaceprintsReady = 'r',
    SetDeviceConfig = 's',
    StandBy = 't',
    GetUserIds = 'u',
    BarcodeDecoded = 'v',
    FaceCroppedImage = 'w',
    SetUserFeatures = 'x',
    GetUserFeatures = 'y',
    Status = 'z',

    // F500 APIs
    DetectPersons = '0',
    DetectPersonsLoop = '1',
    DetectPoses = '2',
    DetectPosesLoop = '3',
    DecodeBarcodes = '4',
    DecodeBarcodesLoop = '5',
    DetectBodyParts = '6',
    DetectBodyPartsLoop = '7'
};

struct SerialPacket
{
    struct
    {
        SyncByte sync1;
        SyncByte sync2;

        unsigned char protocol_ver;
        MsgId id; //'A'-'Z' fa message, 'a-'z' data message
        unsigned char iv[16];
        uint16_t payload_size;
    } header;
    struct
    {
        uint32_t sequence_number;
        union {
            FaMessage fa_msg;
            DataMessage data_msg;
        } message;
    } payload;
    char hmac[32]; // if security is enabled it will store hmac calculation
    uint16_t crc;
    SerialPacket();
};

static_assert(sizeof(SerialPacket) <= 8192, "SerialPacket size must not exceed 8192 bytes");
static_assert((sizeof(SerialPacket::payload) % 32 == 0), "payload size must be dividable by 32");

//
// fa packet
//
struct FaPacket : public SerialPacket
{
    FaPacket(MsgId id, const char* user_id, char status);
    FaPacket(MsgId id, const char* user_id, char status, const char* reserved, size_t reservedSize);
    FaPacket(MsgId id);
    const char* GetUserId() const;
    char GetStatusCode() const;
    const char* GetReserved() const;
};

// data packet
struct DataPacket : public SerialPacket
{
    // copy data to packet. pad with zeros if data_size is smaller than actual reserved data size
    DataPacket(MsgId id, char* data, size_t data_size);
    DataPacket(MsgId id);
    const DataMessage& Data() const;
    size_t MessageSize() const
    {
        return this->header.payload_size - sizeof(this->payload.sequence_number);
    }
};

namespace Commands
{
static const char* face_api = "\r\n__FACE_API__\r\n";
static const char* face_cancel = "\r\n__FACE_CANCEL__\r\n";
static const char* version_info = "\r\nbspver\r\n";
static const char* device_info = "\r\nbspver -device\r\n";
static const char* reset = "\r\nreset\r\n";
static const char* otp_ver = "\r\ngetOtpVer\r\n";
static const char* getlogs = "\r\ngetLogs\r\n";
static const char* gtemp = "\r\ngtemp\r\n";
static const char* get_color_gains = "\r\ncm\r\n";
static const char* set_color_gains = "\r\ncm %d %d\r\n";
static const char* hibernate = "\r\nsleep 1\r\n";
static const char* usbmm = "\r\ninit 0\r\nusbmm 13\r\n";
} // namespace Commands
} // namespace PacketManager
} // namespace RealSenseID

#pragma pack(pop)
