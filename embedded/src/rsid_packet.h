/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_packet.h — Wire-format packet definitions for the RealSenseID serial protocol.
 *
 * Every message between Host and Device is framed as a rsid_serial_packet_t.
 * The on-wire byte layout is:
 *
 *   Offset  Size   Field
 *   ------  -----  --------------------------------------------------
 *   0       1      sync1           '@'  (0x40)
 *   1       1      sync2           'F'  (0x46)
 *   2       1      protocol_ver    currently 3
 *   3       1      msg_id          ASCII char identifying the command
 *   4       16     iv[16]          initialization vector (zeroed in non-secure mode)
 *   20      2      payload_size    little-endian, 32-byte-aligned
 *   ------  -----  --- header ends (22 bytes) ---
 *   22      4      sequence_number monotonic counter for replay protection
 *   26      var    message         fa_msg (41 bytes) or data_msg (up to 8124 bytes)
 *   ...     ...    (zero-padded to 32-byte alignment boundary)
 *   ------  -----  --- payload ends ---
 *   P       32     hmac[32]        HMAC-SHA256 (zeroed in non-secure mode)
 *   P+32    2      crc             CRC-16/AUG-CCITT over header+payload+hmac
 *
 *   P = 22 + payload_size
 *
 * ASCII diagram:
 *
 *   [@][F][ver][id][--- iv:16 ---][size:2][seq:4][--- payload... ---][--- hmac:32 ---][crc:2]
 *   |<--------- header (22B) -------->|<-- payload (32B-aligned) -->|<-- trailer ---------->|
 *
 * Two payload variants:
 *   - FA message (msg_id 'A'-'Z'): user_id[31] + fa_status[1] + reserved[8] = 40 bytes
 *   - Data message (msg_id 'a'-'z'): raw data blob up to 8124 bytes
 *
 * Structs are packed (#pragma pack(1)) and must remain binary-compatible with the
 * C++ SerialPacket in PacketManager/SerialPacket.h.
 */

#ifndef RSID_PACKET_H
#define RSID_PACKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RSID_PROTOCOL_VER 3
/* Use RSID_MAX_USER_ID from rsid.h — must match */
#define RSID_FA_RESERVED_SIZE 8
#define RSID_DATA_MSG_SIZE    8124
#define RSID_HMAC_SIZE        32
#define RSID_IV_SIZE          16

    /* Sync bytes — receiver scans for '@' 'F' to find packet start */
#define RSID_SYNC1 '@'
#define RSID_SYNC2 'F'

    /*
     * Message IDs — must match C++ MsgId enum.
     * Uppercase 'A'-'Z' = FA (face-authentication) messages using fa_msg payload.
     * Lowercase 'a'-'z' = data messages using data_msg payload.
     */

    /* --- Host -> Device commands --- */
#define RSID_MSGID_AUTHENTICATE         'A'
#define RSID_MSGID_REMOVE_ALL           'C'
#define RSID_MSGID_REMOVE_USER          'D'
#define RSID_MSGID_ENROLL               'E'
#define RSID_MSGID_AUTHENTICATE_ONE2ONE 'K'
#define RSID_MSGID_AUTHENTICATE_LOOP    'L'
#define RSID_MSGID_UNLOCK               'U'
#define RSID_MSGID_SET_CONFIG           's'
#define RSID_MSGID_STANDBY              't'

    /* --- Device -> Host responses --- */
#define RSID_MSGID_HINT               'H'
#define RSID_MSGID_RESULT             'R'
#define RSID_MSGID_REPLY              'Y' /* Terminal response — ends the recv loop */
#define RSID_MSGID_FACE_DETECTED      'g'
#define RSID_MSGID_LANDMARKS_DETECTED 'h'
#define RSID_MSGID_FACE_DISTANCES     'm'
#define RSID_MSGID_STATUS             'z'

    /* --- Bidirectional (request + echo/response) --- */
#define RSID_MSGID_NONE          '-'
#define RSID_MSGID_PROGRESS      'P' /* Device->Host during auth; Host->Device as keep-alive */
#define RSID_MSGID_GET_NUM_USERS 'n'
#define RSID_MSGID_START_SESSION 'o'
#define RSID_MSGID_PING          'p'
#define RSID_MSGID_QUERY_CONFIG  'q'
#define RSID_MSGID_GET_USER_IDS  'u'

#pragma pack(push, 1)

    /* 22-byte packet header — first thing on the wire after sync bytes are consumed */
    typedef struct
    {
        char sync1;               /* '@' */
        char sync2;               /* 'F' */
        uint8_t protocol_ver;     /* must equal RSID_PROTOCOL_VER (3) */
        char msg_id;              /* ASCII command/response identifier */
        uint8_t iv[RSID_IV_SIZE]; /* AES-IV for secure mode; zeroed in non-secure */
        uint16_t payload_size;    /* size of payload in bytes, 32-byte-aligned */
    } rsid_packet_header_t;

    /* FA payload: used for authentication/enrollment messages (msg_id 'A'-'Z') */
    typedef struct
    {
        char user_id[RSID_MAX_USER_ID + 1];   /* '\0' terminated, max 30 chars + NUL */
        char fa_status;                       /* status code encoded as '0' + value */
        char reserved[RSID_FA_RESERVED_SIZE]; /* e.g. match score in Result packets */
    } rsid_fa_message_t;

    /* Data payload: used for config, ping, user-list, etc. (msg_id 'a'-'z') */
    typedef struct
    {
        char data[RSID_DATA_MSG_SIZE];
    } rsid_data_message_t;

    /*
     * Complete serial packet. ~8KB due to the data_msg union member.
     * payload_size on wire is always 32-byte-aligned (see rsid_packet_init_*).
     * CRC covers: header + payload + hmac (everything except the crc field itself).
     */
    typedef struct
    {
        rsid_packet_header_t header;
        struct
        {
            uint32_t sequence_number; /* monotonic, validated on recv (delta <= 20) */
            union {
                rsid_fa_message_t fa_msg;
                rsid_data_message_t data_msg;
            } message;
        } payload;
        char hmac[RSID_HMAC_SIZE]; /* HMAC-SHA256 (zeroed in non-secure mode) */
        uint16_t crc;              /* CRC-16/AUG-CCITT, appended last on wire */
    } rsid_serial_packet_t;

#pragma pack(pop)

    /** Initialize packet with sync bytes and protocol version */
    void rsid_packet_init(rsid_serial_packet_t* pkt);

    /** Initialize as FA packet (msg_id in 'A'-'Z' range) */
    void rsid_packet_init_fa(rsid_serial_packet_t* pkt, char msg_id, const char* user_id, char status);

    /** Initialize as data packet */
    void rsid_packet_init_data(rsid_serial_packet_t* pkt, char msg_id, const char* data, uint32_t data_size);

    /** Get pointer to user_id field */
    const char* rsid_packet_get_user_id(rsid_serial_packet_t* pkt);

    /** Get decoded status code (fa_status - '0') */
    char rsid_packet_get_status_code(const rsid_serial_packet_t* pkt);

    /** Get pointer to reserved field */
    const char* rsid_packet_get_reserved(const rsid_serial_packet_t* pkt);

    /** Compute CRC over packet (header + payload + hmac) */
    uint16_t rsid_packet_calc_crc(const rsid_serial_packet_t* pkt);

#ifdef __cplusplus
}
#endif

#endif /* RSID_PACKET_H */
