/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_device_controller.c — Device management commands: ping, FW version, reboot, hibernate.
 *
 * These operations bypass the session layer. Unlike auth/enroll, they do NOT use
 * rsid_session_start/send/recv and do NOT track sequence numbers:
 *
 *   - Ping: sends __FACE_API__ + packet directly, receives a raw packet echo.
 *     No session, no sequence validation. Verifies the echo data matches.
 *
 *   - FW version: sends "\r\nbspver\r\n" as raw UART text (not a binary packet).
 *     Receives multi-line text response "MODULE : x.y.z.w" and parses it.
 *
 *   - Reboot / Hibernate: send raw text commands and return immediately.
 *     No response expected (device reboots or enters sleep).
 *
 * All text commands use the "\r\n<cmd>\r\n" framing that the device's UART
 * console expects. This is distinct from the __FACE_API__ binary protocol.
 */

#include "rsid_common.h"

#define RSID_TEXT_RECV_TIMEOUT_MS 200 /* Short timeout for text-mode line reads */
#define RSID_COLOR_GAIN_MAX       511 /* Max value for SetColorGains */
#include <stdio.h>
#include <stdlib.h>

/* Raw text commands — char arrays so sizeof() gives length. Must match C++ PacketManager::Commands. */
static const char CMD_VERSION[] = "\r\nbspver\r\n";
static const char CMD_DEVICE_INFO[] = "\r\nbspver -device\r\n";
static const char CMD_OTP_VER[] = "\r\ngetOtpVer\r\n";
static const char CMD_GTEMP[] = "\r\ngtemp\r\n";
static const char CMD_GET_COLOR_GAINS[] = "\r\ncm\r\n";
static const char CMD_RESET[] = "\r\nreset\r\n";
static const char CMD_HIBERNATE[] = "\r\nsleep 1\r\n";

/*
 * Send a text command and receive the response into the ctx packet buffer.
 * Returns the number of bytes received (0 on send failure).
 * The buffer is null-terminated.
 */
static unsigned int send_text_cmd(rsid_ctx_t* ctx, const char* cmd, unsigned int cmd_len, char** out_buf)
{
    char* buffer;
    const unsigned int buffer_size = sizeof(rsid_serial_packet_t);
    unsigned int pos = 0;
    rsid_serial_status ss;

    if (out_buf == NULL)
        return 0;
    *out_buf = NULL;

    if (ctx == NULL || cmd == NULL || cmd_len == 0 || !validate_ctx(ctx))
        return 0;

    buffer = (char*)ctx_packet(ctx);
    if (buffer == NULL)
        return 0;

    ss = rsid_send_raw(&ctx->platform, cmd, cmd_len);
    if (ss != RSID_SERIAL_OK)
    {
        RSID_DBG(&ctx->platform, "send_text_cmd failed");
        *out_buf = buffer;
        buffer[0] = '\0';
        return 0;
    }

    for (pos = 0; pos < buffer_size - 1; pos++)
    {
        if (ctx->platform.recv((uint8_t*)&buffer[pos], 1, RSID_TEXT_RECV_TIMEOUT_MS, ctx->platform.app_ctx) != 0)
            break;
    }
    buffer[pos] = '\0';
    *out_buf = buffer;
    return pos;
}

rsid_status rsid_ping(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    uint32_t timestamp;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "ping");

    pkt = ctx_packet(ctx);
    timestamp = ctx->platform.get_time_ms(ctx->platform.app_ctx);

    /* Send timestamp as ping payload — device echoes it back */
    rsid_packet_init_data(pkt, RSID_MSGID_PING, (const char*)&timestamp, sizeof(timestamp));

    ss = rsid_send_packet(&ctx->platform, pkt);
    if (ss != RSID_SERIAL_OK)
    {
        RSID_DBG(&ctx->platform, "ping send failed");
        return serial_to_status(ss);
    }

    ss = rsid_recv_packet(&ctx->platform, pkt, RSID_DEFAULT_RECV_TIMEOUT_MS);
    if (ss != RSID_SERIAL_OK)
    {
        RSID_DBG(&ctx->platform, "ping recv failed");
        return serial_to_status(ss);
    }

    if (pkt->header.msg_id != RSID_MSGID_PING)
    {
        RSID_DBG(&ctx->platform, "ping bad msg_id");
        return RSID_Error;
    }

    /* Verify echo matches */
    if (memcmp(&timestamp, pkt->payload.message.data_msg.data, sizeof(timestamp)) != 0)
    {
        RSID_DBG(&ctx->platform, "ping echo mismatch");
        return RSID_Error;
    }

    RSID_DBG(&ctx->platform, "ping ok");
    return RSID_Ok;
}

/*
 * Query firmware version using raw text protocol (not binary packets).
 * Sends "\r\nbspver\r\n" and receives multi-line text like:
 *   "OPFW : 4.2.0.1\r\n"
 *   "NNFW : 3.1.0.0\r\n"
 *   ...
 * Parses "MODULE : x.y.z.w" lines, skips "0.0.0.0" (unused modules),
 * and writes "MODULE1:ver1|MODULE2:ver2|..." into output buffer.
 * Relies on recv timeout to detect end of response (no explicit terminator).
 */
rsid_status rsid_query_firmware_version(rsid_ctx_t* ctx, char* output, unsigned int output_len)
{
    char* buffer;
    unsigned int pos;
    unsigned int out_pos = 0;
    unsigned int line_start;

    if (ctx == NULL || output == NULL || output_len == 0)
        return RSID_Error;
    output[0] = '\0';

    RSID_DBG(&ctx->platform, "fw-version query");

    pos = send_text_cmd(ctx, CMD_VERSION, sizeof(CMD_VERSION) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    /* Parse lines: "MODULE : x.y.z.w" format using simple string scanning */
    line_start = 0;
    out_pos = 0;
    while (line_start < pos)
    {
        unsigned int line_end = line_start;
        char* colon;

        /* Find end of line */
        while (line_end < pos && buffer[line_end] != '\n')
            line_end++;

        /* Null-terminate line for parsing */
        if (line_end < pos)
            buffer[line_end] = '\0';

        /* Look for " : " pattern */
        colon = strstr(&buffer[line_start], " : ");
        if (colon != NULL)
        {
            const char* module_name = &buffer[line_start];
            const char* version = colon + 3;
            unsigned int name_len;
            unsigned int ver_len;

            /* Trim leading/trailing whitespace and \r from module name */
            *colon = '\0';
            while (*module_name == ' ' || *module_name == '\t' || *module_name == '\r')
                module_name++;
            name_len = (unsigned int)rsid_strnlen(module_name, pos);
            while (name_len > 0 &&
                   (module_name[name_len - 1] == ' ' || module_name[name_len - 1] == '\t' || module_name[name_len - 1] == '\r'))
                name_len--;

            /* Skip version "0.0.0.0" — unused module */
            if (strncmp(version, "0.0.0.0", 7) == 0)
            {
                line_start = line_end + 1;
                continue;
            }

            /* Version is digits and dots only (matches C++ regex [\d\.]+) */
            ver_len = 0;
            while (version[ver_len] != '\0' && ((version[ver_len] >= '0' && version[ver_len] <= '9') || version[ver_len] == '.'))
                ver_len++;

            /* Skip if no valid version digits found */
            if (ver_len == 0 || name_len == 0)
            {
                line_start = line_end + 1;
                continue;
            }

            /* Append "MODULE:version" to output (with leading | separator) */
            {
                unsigned int needed = name_len + 1 + ver_len; /* MODULE:version */
                if (out_pos > 0)
                    needed += 1; /* | separator */
                if (out_pos + needed < output_len)
                {
                    if (out_pos > 0)
                        output[out_pos++] = '|';
                    memcpy(&output[out_pos], module_name, name_len);
                    out_pos += name_len;
                    output[out_pos++] = ':';
                    memcpy(&output[out_pos], version, ver_len);
                    out_pos += ver_len;
                }
            }
        }

        line_start = line_end + 1;
    }

    output[out_pos] = '\0';

    if (out_pos == 0)
    {
        RSID_DBG(&ctx->platform, "version parse empty");
        return RSID_Error;
    }

    return RSID_Ok;
}

/*
 * Query device serial number. Sends "\r\nbspver -device\r\n" and parses "SN : [xxx]".
 */
rsid_status rsid_query_serial_number(rsid_ctx_t* ctx, char* output, unsigned int output_len)
{
    char* buffer;
    unsigned int pos;
    char* sn_start;

    if (ctx == NULL || output == NULL || output_len == 0)
        return RSID_Error;
    output[0] = '\0';

    RSID_DBG(&ctx->platform, "serial-num query");

    pos = send_text_cmd(ctx, CMD_DEVICE_INFO, sizeof(CMD_DEVICE_INFO) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    /* Look for "SN : [" */
    sn_start = strstr(buffer, "SN : [");
    if (sn_start != NULL)
    {
        char* val = sn_start + 6; /* skip "SN : [" */
        char* end = strchr(val, ']');
        if (end != NULL)
        {
            unsigned int len = (unsigned int)(end - val);
            if (len >= output_len)
                len = output_len - 1;
            memcpy(output, val, len);
            output[len] = '\0';
            return RSID_Ok;
        }
    }

    RSID_DBG(&ctx->platform, "serial-num parse failed");
    return RSID_Error;
}

/*
 * Query OTP version. Sends "\r\ngetOtpVer\r\n" and parses "otp version is X".
 */
rsid_status rsid_query_otp_version(rsid_ctx_t* ctx, uint8_t* version)
{
    char* buffer;
    unsigned int pos;
    char* match;

    if (ctx == NULL || version == NULL)
        return RSID_Error;
    *version = 0;

    RSID_DBG(&ctx->platform, "otp-ver query");

    pos = send_text_cmd(ctx, CMD_OTP_VER, sizeof(CMD_OTP_VER) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    match = strstr(buffer, "otp version is ");
    if (match != NULL && match[15] != '\0')
    {
        *version = (uint8_t)match[15]; /* raw ASCII char, matches C++ SDK convention */
        return RSID_Ok;
    }

    RSID_DBG(&ctx->platform, "otp-ver parse failed");
    return RSID_Error;
}

/*
 * Get SOC and board temperature. Sends "\r\ngtemp\r\n" and parses float values.
 * F50x only — F450 will return an error or unparseable response.
 */
rsid_status rsid_get_temperature(rsid_ctx_t* ctx, float* soc_temp, float* board_temp)
{
    char* buffer;
    unsigned int pos;
    char* match;
    char temp_str[16];
    char* end;
    unsigned int len;
    int parsed = 0;

    if (ctx == NULL || soc_temp == NULL || board_temp == NULL)
        return RSID_Error;
    *soc_temp = 0.0f;
    *board_temp = 0.0f;

    RSID_DBG(&ctx->platform, "temp query");

    pos = send_text_cmd(ctx, CMD_GTEMP, sizeof(CMD_GTEMP) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    /* Parse "SoC temperature" line — find ": " then read number */
    match = strstr(buffer, "SoC temperature");
    if (match != NULL)
    {
        char* colon = strstr(match, ": ");
        if (colon != NULL)
        {
            colon += 2;
            for (len = 0;
                 len < sizeof(temp_str) - 1 && (colon[len] == '-' || colon[len] == '.' || (colon[len] >= '0' && colon[len] <= '9')); len++)
                temp_str[len] = colon[len];
            temp_str[len] = '\0';
            if (len > 0)
            {
                *soc_temp = (float)strtod(temp_str, &end);
                parsed++;
            }
        }
    }

    /* Parse "Board temperature" line */
    match = strstr(buffer, "Board temperature");
    if (match != NULL)
    {
        char* colon = strstr(match, ": ");
        if (colon != NULL)
        {
            colon += 2;
            for (len = 0;
                 len < sizeof(temp_str) - 1 && (colon[len] == '-' || colon[len] == '.' || (colon[len] >= '0' && colon[len] <= '9')); len++)
                temp_str[len] = colon[len];
            temp_str[len] = '\0';
            if (len > 0)
            {
                *board_temp = (float)strtod(temp_str, &end);
                parsed++;
            }
        }
    }

    if (parsed == 0)
    {
        RSID_DBG(&ctx->platform, "gtemp parse failed");
        return RSID_Error;
    }

    return RSID_Ok;
}

/*
 * Get color gains. Sends "\r\ncm\r\n" and parses "[red blue]".
 * F450 only — F50x will return an error or unparseable response.
 */
rsid_status rsid_get_color_gains(rsid_ctx_t* ctx, int* red, int* blue)
{
    char* buffer;
    unsigned int pos;
    char* bracket;

    if (ctx == NULL || red == NULL || blue == NULL)
        return RSID_Error;
    *red = 0;
    *blue = 0;

    RSID_DBG(&ctx->platform, "color-gains query");

    pos = send_text_cmd(ctx, CMD_GET_COLOR_GAINS, sizeof(CMD_GET_COLOR_GAINS) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    /* Parse "[123 456]" */
    bracket = strchr(buffer, '[');
    if (bracket != NULL)
    {
        char* p = bracket + 1;
        char* end;
        long r = strtol(p, &end, 10);
        if (end != p && *end == ' ')
        {
            long b = strtol(end + 1, &end, 10);
            if (*end == ']')
            {
                *red = (int)r;
                *blue = (int)b;
                return RSID_Ok;
            }
        }
    }

    RSID_DBG(&ctx->platform, "color-gains parse failed");
    return RSID_Error;
}

/*
 * Set color gains. Sends "\r\ncm <red> <blue>\r\n".
 * F450 only — F50x will ignore or reject the command.
 */
rsid_status rsid_set_color_gains(rsid_ctx_t* ctx, int red, int blue)
{
    char cmd[32];
    int len;
    rsid_serial_status ss;

    if (ctx == NULL || red < 0 || red > RSID_COLOR_GAIN_MAX || blue < 0 || blue > RSID_COLOR_GAIN_MAX)
        return RSID_Error;

    RSID_DBG(&ctx->platform, "set-color-gains r=%d b=%d", red, blue);

    len = snprintf(cmd, sizeof(cmd), "\r\ncm %d %d\r\n", red, blue);
    if (len < 0 || len >= (int)sizeof(cmd))
        return RSID_Error;

    ss = rsid_send_raw(&ctx->platform, cmd, (uint32_t)len);
    if (ss != RSID_SERIAL_OK)
    {
        RSID_DBG(&ctx->platform, "set-color-gains send failed");
        return RSID_SerialError;
    }

    return RSID_Ok;
}

/*
 * Query raw BSP version text. Same command as QueryFirmwareVersion but returns
 * the unprocessed multi-line response from the device.
 */
rsid_status rsid_query_bsp_version(rsid_ctx_t* ctx, char* output, unsigned int output_len)
{
    char* buffer;
    unsigned int pos;

    if (ctx == NULL || output == NULL || output_len == 0)
        return RSID_Error;
    output[0] = '\0';

    pos = send_text_cmd(ctx, CMD_VERSION, sizeof(CMD_VERSION) - 1, &buffer);
    if (pos == 0)
        return RSID_SerialError;

    if (pos >= output_len)
        pos = output_len - 1;
    memcpy(output, buffer, pos);
    output[pos] = '\0';
    return RSID_Ok;
}

/* Fire-and-forget: device reboots immediately, no response expected */
rsid_status rsid_reboot(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "reboot");

    ss = rsid_send_raw(&ctx->platform, CMD_RESET, sizeof(CMD_RESET) - 1);
    if (ss != RSID_SERIAL_OK)
        RSID_DBG(&ctx->platform, "reboot send failed");
    return (ss == RSID_SERIAL_OK) ? RSID_Ok : RSID_SerialError;
}

/* Fire-and-forget: device enters low-power sleep, no response expected */
rsid_status rsid_hibernate(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "hibernate");

    ss = rsid_send_raw(&ctx->platform, CMD_HIBERNATE, sizeof(CMD_HIBERNATE) - 1);
    if (ss != RSID_SERIAL_OK)
        RSID_DBG(&ctx->platform, "hibernate send failed");
    return (ss == RSID_SERIAL_OK) ? RSID_Ok : RSID_SerialError;
}
