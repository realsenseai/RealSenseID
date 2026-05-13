/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* RealSenseID Embedded C SDK — CLI test tool (Windows + Linux)
 * Connects to a RealSenseID device over serial port and exercises the API.
 * Platform transport is in plat_win32.c / plat_linux.c. */

#include "rsid.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static void cli_debug(const char* msg, void* app_ctx)
{
    (void)app_ctx;
    printf("[%u] [DBG] %s\n", (unsigned)plat_get_time_ms(NULL), msg);
}

/* Print with [timestamp] [CLI] prefix */
#define CLI_PRINT(fmt, ...) printf("[%u] [CLI] " fmt, (unsigned)plat_get_time_ms(NULL), ##__VA_ARGS__)

/* ---- Status-to-string helpers ---- */

static const char* status_str(rsid_status s)
{
    switch (s)
    {
    case RSID_Ok:
        return "Ok";
    case RSID_Error:
        return "Error";
    case RSID_SerialError:
        return "SerialError";
    case RSID_SecurityError:
        return "SecurityError";
    case RSID_VersionMismatch:
        return "VersionMismatch";
    case RSID_CrcError:
        return "CrcError";
    case RSID_TooManySpoofs:
        return "TooManySpoofs";
    case RSID_NotSupported:
        return "NotSupported";
    case RSID_DatabaseFull:
        return "DatabaseFull";
    case RSID_DuplicateUserId:
        return "DuplicateUserId";
    case RSID_DuplicateFaceprints:
        return "DuplicateFaceprints";
    case RSID_InvalidSettings:
        return "InvalidSettings";
    default:
        return "Unknown";
    }
}

static const char* auth_status_str(rsid_auth_status s)
{
    switch (s)
    {
    case RSID_Auth_Success:
        return "Success";
    case RSID_Auth_NoFaceDetected:
        return "NoFaceDetected";
    case RSID_Auth_FaceDetected:
        return "FaceDetected";
    case RSID_Auth_PersonNotFound:
        return "PersonNotFound";
    case RSID_Auth_PersonFound:
        return "PersonFound";
    case RSID_Auth_LedFlowSuccess:
        return "LedFlowSuccess";
    case RSID_Auth_FaceIsTooFarToTheTop:
        return "FaceTooFarUp";
    case RSID_Auth_FaceIsTooFarToTheBottom:
        return "FaceTooFarDown";
    case RSID_Auth_FaceIsTooFarToTheRight:
        return "FaceTooFarRight";
    case RSID_Auth_FaceIsTooFarToTheLeft:
        return "FaceTooFarLeft";
    case RSID_Auth_FaceTiltIsTooUp:
        return "TiltTooUp";
    case RSID_Auth_FaceTiltIsTooDown:
        return "TiltTooDown";
    case RSID_Auth_FaceTiltIsTooRight:
        return "TiltTooRight";
    case RSID_Auth_FaceTiltIsTooLeft:
        return "TiltTooLeft";
    case RSID_Auth_FaceIsNotFrontal:
        return "NotFrontal";
    case RSID_Auth_CameraStarted:
        return "CameraStarted";
    case RSID_Auth_CameraStopped:
        return "CameraStopped";
    case RSID_Auth_Spoof:
        return "Spoof";
    case RSID_Auth_Forbidden:
        return "Forbidden";
    case RSID_Auth_DeviceError:
        return "DeviceError";
    case RSID_Auth_Failure:
        return "Failure";
    case RSID_Auth_TooManySpoofs:
        return "TooManySpoofs";
    case RSID_Auth_InvalidFeatures:
        return "InvalidFeatures";
    case RSID_Auth_AmbiguousFace:
        return "AmbiguousFace";
    case RSID_Auth_Sunglasses:
        return "Sunglasses";
    case RSID_Auth_MedicalMask:
        return "MedicalMask";
    case RSID_Auth_FaceTooFar:
        return "FaceTooFar";
    case RSID_Auth_CalcDistanceFailure:
        return "CalcDistanceFailure";
    case RSID_Auth_FaceTooClose:
        return "FaceTooClose";
    case RSID_Auth_Serial_Ok:
        return "Serial_Ok";
    case RSID_Auth_Serial_Error:
        return "Serial_Error";
    case RSID_Auth_Serial_SerialError:
        return "Serial_SerialError";
    case RSID_Auth_Serial_SecurityError:
        return "Serial_SecurityError";
    case RSID_Auth_Serial_VersionMismatch:
        return "Serial_VersionMismatch";
    case RSID_Auth_Serial_CrcError:
        return "Serial_CrcError";
    case RSID_Auth_Spoof_2D:
        return "Spoof_2D";
    case RSID_Auth_Spoof_3D:
        return "Spoof_3D";
    case RSID_Auth_Spoof_LR:
        return "Spoof_LR";
    case RSID_Auth_Spoof_Disparity:
        return "Spoof_Disparity";
    case RSID_Auth_Spoof_Vision:
        return "Spoof_Vision";
    case RSID_Auth_Spoof_Surface:
        return "Spoof_Surface";
    case RSID_Auth_Spoof_Plane_Disparity:
        return "Spoof_PlaneDisparity";
    case RSID_Auth_Spoof_2D_Right:
        return "Spoof_2D_Right";
    default:
        return "Unknown";
    }
}

static const char* enroll_status_str(rsid_enroll_status s)
{
    switch (s)
    {
    case RSID_Enroll_Success:
        return "Success";
    case RSID_Enroll_NoFaceDetected:
        return "NoFaceDetected";
    case RSID_Enroll_FaceDetected:
        return "FaceDetected";
    case RSID_Enroll_LedFlowSuccess:
        return "LedFlowSuccess";
    case RSID_Enroll_FaceIsTooFarToTheTop:
        return "FaceTooFarUp";
    case RSID_Enroll_FaceIsTooFarToTheBottom:
        return "FaceTooFarDown";
    case RSID_Enroll_FaceIsTooFarToTheRight:
        return "FaceTooFarRight";
    case RSID_Enroll_FaceIsTooFarToTheLeft:
        return "FaceTooFarLeft";
    case RSID_Enroll_FaceTiltIsTooUp:
        return "TiltTooUp";
    case RSID_Enroll_FaceTiltIsTooDown:
        return "TiltTooDown";
    case RSID_Enroll_FaceTiltIsTooRight:
        return "TiltTooRight";
    case RSID_Enroll_FaceTiltIsTooLeft:
        return "TiltTooLeft";
    case RSID_Enroll_FaceIsNotFrontal:
        return "NotFrontal";
    case RSID_Enroll_CameraStarted:
        return "CameraStarted";
    case RSID_Enroll_CameraStopped:
        return "CameraStopped";
    case RSID_Enroll_MultipleFacesDetected:
        return "MultipleFaces";
    case RSID_Enroll_Failure:
        return "Failure";
    case RSID_Enroll_DeviceError:
        return "DeviceError";
    case RSID_Enroll_Spoof:
        return "Spoof";
    case RSID_Enroll_InvalidFeatures:
        return "InvalidFeatures";
    case RSID_Enroll_AmbiguousFace:
        return "AmbiguousFace";
    case RSID_Enroll_Sunglasses:
        return "Sunglasses";
    case RSID_Enroll_MedicalMask:
        return "MedicalMask";
    case RSID_Enroll_FaceTooClose:
        return "FaceTooClose";
    case RSID_Enroll_Serial_Ok:
        return "Serial_Ok";
    case RSID_Enroll_Serial_Error:
        return "Serial_Error";
    case RSID_Enroll_Serial_SerialError:
        return "Serial_SerialError";
    case RSID_Enroll_Serial_SecurityError:
        return "Serial_SecurityError";
    case RSID_Enroll_Serial_VersionMismatch:
        return "Serial_VersionMismatch";
    case RSID_Enroll_Serial_CrcError:
        return "Serial_CrcError";
    case RSID_Enroll_TooManySpoofs:
        return "TooManySpoofs";
    case RSID_Enroll_NotSupported:
        return "NotSupported";
    case RSID_Enroll_DatabaseFull:
        return "DatabaseFull";
    case RSID_Enroll_DuplicateUserId:
        return "DuplicateUserId";
    case RSID_Enroll_DuplicateFaceprints:
        return "DuplicateFaceprints";
    case RSID_Enroll_Spoof_2D:
        return "Spoof_2D";
    case RSID_Enroll_Spoof_3D:
        return "Spoof_3D";
    case RSID_Enroll_Spoof_LR:
        return "Spoof_LR";
    case RSID_Enroll_Spoof_Disparity:
        return "Spoof_Disparity";
    case RSID_Enroll_Spoof_Vision:
        return "Spoof_Vision";
    case RSID_Enroll_Spoof_Surface:
        return "Spoof_Surface";
    case RSID_Enroll_Spoof_Plane_Disparity:
        return "Spoof_PlaneDisparity";
    case RSID_Enroll_Spoof_2D_Right:
        return "Spoof_2D_Right";
    default:
        return "Unknown";
    }
}

/* ---- Callbacks ---- */

static void on_auth_result(rsid_auth_status status, const char* user_id, short score, void* ctx)
{
    (void)ctx;
    if (status == RSID_Auth_Success)
        CLI_PRINT("AUTH OK: user='%s' score=%d\n", user_id ? user_id : "", (int)score);
    else
        CLI_PRINT("AUTH: %s user='%s' score=%d\n", auth_status_str(status), user_id ? user_id : "", (int)score);
}

static void on_auth_hint(rsid_auth_status hint, float frame_score, void* ctx)
{
    (void)ctx;
    (void)frame_score;
    CLI_PRINT("HINT: %s\n", auth_status_str(hint));
}

static void on_face_detected(const rsid_face_rect* faces, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i;
    (void)ctx;
    for (i = 0; i < num_faces; i++)
        CLI_PRINT("FACE[%u]: x=%u y=%u w=%u h=%u ts=%u\n", i, faces[i].x, faces[i].y, faces[i].w, faces[i].h, ts);
}

static void on_landmarks_detected(const rsid_face_landmarks* landmarks, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i, j;
    (void)ctx;
    for (i = 0; i < num_faces; i++)
    {
        CLI_PRINT("LANDMARKS[%u] ts=%u:", i, ts);
        for (j = 0; j < RSID_NUM_FACE_LANDMARKS; j++)
            printf(" (%u,%u)", landmarks[i].lm_x[j], landmarks[i].lm_y[j]);
        printf("\n");
    }
}

static void on_face_distances(const double* distances, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i;
    (void)ctx;
    for (i = 0; i < num_faces; i++)
        CLI_PRINT("DISTANCE[%u]: %.1f cm ts=%u\n", i, distances[i], ts);
}

static void on_enroll_result(rsid_enroll_status status, void* ctx)
{
    (void)ctx;
    CLI_PRINT("ENROLL: %s\n", enroll_status_str(status));
}

static void on_enroll_progress(rsid_face_pose pose, void* ctx)
{
    const char* names[] = {"Center", "Up", "Down", "Left", "Right"};
    (void)ctx;
    CLI_PRINT("POSE: %s\n", (pose >= RSID_Face_Center && pose <= RSID_Face_Right) ? names[pose] : "?");
}

static void on_enroll_hint(rsid_enroll_status hint, float frame_score, void* ctx)
{
    (void)ctx;
    CLI_PRINT("HINT: %s (score=%.2f)\n", enroll_status_str(hint), frame_score);
}

/* ---- CLI Commands ---- */

static void prompt_string(const char* prompt, char* buf, int buf_size);
static rsid_ctx_t g_ctx;

#define PING_COUNT 50

static void cmd_ping(void)
{
    int ok = 0;
    int fail = 0;
    uint32_t total_ms = 0;
    int i;

    for (i = 0; i < PING_COUNT; i++)
    {
        uint32_t start = plat_get_time_ms(NULL);
        rsid_status s = rsid_ping(&g_ctx);
        uint32_t elapsed = plat_get_time_ms(NULL) - start;

        if (s == RSID_Ok)
        {
            ok++;
            total_ms += elapsed;
            CLI_PRINT("[%d] OK  %ums\n", i + 1, (unsigned)elapsed);
        }
        else
        {
            fail++;
            CLI_PRINT("[%d] FAILED\n", i + 1);
        }
    }

    CLI_PRINT("Ping: %d/%d ok", ok, PING_COUNT);
    if (ok > 0)
        printf(", avg round-trip %ums", (unsigned)(total_ms / ok));
    if (fail > 0)
        printf(", %d failed", fail);
    printf("\n");
}

static void cmd_version(void)
{
    char ver[512];
    rsid_status s = rsid_query_firmware_version(&g_ctx, ver, sizeof(ver));
    if (s == RSID_Ok)
        CLI_PRINT("%s\n", ver);
    else
        CLI_PRINT("Version query failed: %s\n", status_str(s));
}

static void cmd_serial_number(void)
{
    char sn[128];
    rsid_status s = rsid_query_serial_number(&g_ctx, sn, sizeof(sn));
    if (s == RSID_Ok)
        CLI_PRINT("Serial: %s\n", sn);
    else
        CLI_PRINT("Serial number query failed: %s\n", status_str(s));
}

static void cmd_temperature(void)
{
    float soc = 0, board = 0;
    rsid_status s = rsid_get_temperature(&g_ctx, &soc, &board);
    if (s == RSID_Ok)
        CLI_PRINT("Temperature: SoC=%.1f Board=%.1f\n", soc, board);
    else
        CLI_PRINT("Temperature query failed: %s\n", status_str(s));
}

static void cmd_enroll(const char* user_id)
{
    rsid_enroll_callbacks_t cb;
    rsid_status s;

    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_enroll_result;
    cb.on_progress = on_enroll_progress;
    cb.on_hint = on_enroll_hint;

    CLI_PRINT("Enrolling '%s'...\n", user_id);
    s = rsid_enroll(&g_ctx, user_id, &cb, NULL);
    CLI_PRINT("Enroll result: %s\n", status_str(s));
}

static void cmd_authenticate(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;

    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_auth_result;
    cb.on_hint = on_auth_hint;
    cb.on_face_detected = on_face_detected;
    cb.on_landmarks_detected = on_landmarks_detected;
    cb.on_face_distances = on_face_distances;

    CLI_PRINT("Authenticating...\n");
    s = rsid_authenticate(&g_ctx, &cb, NULL);
    CLI_PRINT("Authenticate result: %s\n", status_str(s));
}

/* Wait for Enter then cancel (runs in a background thread) */
#ifdef _WIN32
static DWORD WINAPI cancel_on_enter(LPVOID arg)
{
    (void)getchar();
    rsid_cancel((rsid_ctx_t*)arg);
    plat_cancel_io();
    return 0;
}
#else
static void* cancel_on_enter(void* arg)
{
    (void)getchar();
    rsid_cancel((rsid_ctx_t*)arg);
    return NULL;
}
#endif

static void cmd_authenticate_loop(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;

    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_auth_result;
    cb.on_hint = on_auth_hint;
    cb.on_face_detected = on_face_detected;
    cb.on_landmarks_detected = on_landmarks_detected;
    cb.on_face_distances = on_face_distances;

    CLI_PRINT("Authenticate loop running. Press Enter to cancel...\n");

#ifdef _WIN32
    {
        HANDLE h = CreateThread(NULL, 0, cancel_on_enter, &g_ctx, 0, NULL);
        s = rsid_authenticate_loop(&g_ctx, &cb, NULL);
        if (h != NULL)
        {
            WaitForSingleObject(h, 1000);
            CloseHandle(h);
        }
    }
#else
    {
        pthread_t th;
        if (pthread_create(&th, NULL, cancel_on_enter, &g_ctx) == 0)
        {
            s = rsid_authenticate_loop(&g_ctx, &cb, NULL);
            pthread_join(th, NULL);
        }
        else
        {
            s = RSID_Error;
        }
    }
#endif
    CLI_PRINT("Authenticate loop result: %s\n", status_str(s));
}

static void cmd_users(void)
{
    unsigned int count = 100;
    char users[100][RSID_MAX_USER_ID + 1];
    unsigned int i;
    rsid_status s;

    s = rsid_query_number_of_users(&g_ctx, &count);
    if (s != RSID_Ok)
    {
        CLI_PRINT("Query number of users failed: %s\n", status_str(s));
        return;
    }
    CLI_PRINT("Number of users: %u\n", count);

    if (count == 0)
        return;
    if (count > 100)
        count = 100;

    s = rsid_query_user_ids(&g_ctx, users, &count);
    if (s != RSID_Ok)
    {
        CLI_PRINT("Query user IDs failed: %s\n", status_str(s));
        return;
    }
    for (i = 0; i < count; i++)
        CLI_PRINT("[%u] %s\n", i, users[i]);
}

static void cmd_remove(const char* user_id)
{
    rsid_status s = rsid_remove_user(&g_ctx, user_id);
    CLI_PRINT("Remove '%s': %s\n", user_id, status_str(s));
}

static void cmd_remove_all(void)
{
    rsid_status s = rsid_remove_all(&g_ctx);
    CLI_PRINT("Remove all: %s\n", status_str(s));
}

static const char* rot_names[] = {"0", "180", "90", "270"};
static const char* sec_names[] = {"High", "Medium", "Low"};
static const char* algo_names[] = {"All", "SpoofOnly", "RecognitionOnly"};
static const char* face_sel_names[] = {"Single", "All"};
static const char* dump_names[] = {"None", "CroppedFace", "FullFrame", "Debug"};
static const char* frontal_names[] = {"None", "Moderate", "Strict"};
static const char* motion_names[] = {"Static", "Walkthrough"};

static void print_config_line(int num, const char* name, const char* value)
{
    if (num > 0)
        printf("  %2d. %-22s %s\n", num, name, value);
    else
        printf("      %-22s %s\n", name, value);
}

static void print_config_int(int num, const char* name, int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    print_config_line(num, name, buf);
}

static void print_config(const rsid_device_config_t* c, int numbered)
{
    int n = numbered ? 1 : 0;
    print_config_line(n ? n++ : 0, "camera_rotation", rot_names[c->camera_rotation % 4]);
    print_config_line(n ? n++ : 0, "security_level", sec_names[c->security_level % 3]);
    print_config_line(n ? n++ : 0, "algo_mode", algo_names[c->algo_mode % 3]);
    print_config_line(n ? n++ : 0, "face_selection", face_sel_names[c->face_selection_policy % 2]);
    print_config_line(n ? n++ : 0, "dump_mode", dump_names[c->dump_mode % 4]);
    print_config_line(n ? n++ : 0, "frontal_face_policy", frontal_names[c->frontal_face_policy % 3]);
    print_config_line(n ? n++ : 0, "person_motion_mode", motion_names[c->person_motion_mode % 2]);
    print_config_line(n ? n++ : 0, "max_spoofs", c->max_spoofs ? "enabled" : "disabled");
    print_config_line(n ? n++ : 0, "gpio_auth_toggling", c->gpio_auth_toggling ? "on" : "off");
    print_config_int(n ? n++ : 0, "match_thresh", c->match_thresh);
    print_config_int(n ? n++ : 0, "manual_exposure_us", c->manual_exposure_time_us);
    print_config_int(n ? n++ : 0, "manual_gain", c->manual_gain);
    print_config_line(n ? n++ : 0, "rect_enable", c->rect_enable ? "on" : "off");
    print_config_line(n ? n++ : 0, "landmarks_enable", c->landmarks_enable ? "on" : "off");
    {
        char dist_buf[24];
        if (c->distance_limit_cm == 0)
            snprintf(dist_buf, sizeof(dist_buf), "unlimited (0)");
        else
            snprintf(dist_buf, sizeof(dist_buf), "%u cm", (unsigned)c->distance_limit_cm);
        print_config_line(n ? n++ : 0, "distance_limit_cm", dist_buf);
    }
    print_config_line(n ? n++ : 0, "distance_enabled", c->distance_enabled ? "on" : "off");
}

static void cmd_config(void)
{
    rsid_device_config_t config;
    rsid_status s = rsid_query_device_config(&g_ctx, &config);
    if (s != RSID_Ok)
    {
        CLI_PRINT("Query config failed: %s\n", status_str(s));
        return;
    }
    CLI_PRINT("Device config:\n");
    print_config(&config, 0);
}

/* Cycle a value: current -> (current + 1) % count */
#define CYCLE(field, count) field = (field + 1) % (count)

static void cmd_set_config(void)
{
    rsid_device_config_t config;
    rsid_status s;
    char input[16];
    int choice;

    s = rsid_query_device_config(&g_ctx, &config);
    if (s != RSID_Ok)
    {
        CLI_PRINT("Query config failed: %s\n", status_str(s));
        return;
    }

    while (1)
    {
        printf("\nSelect setting to change ('q' to apply & quit):\n");
        print_config(&config, 1);
        printf("  > ");

        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\r\n")] = '\0';

        if (input[0] == 'q')
            break;

        choice = atoi(input);
        switch (choice)
        {
        case 1:
            CYCLE(config.camera_rotation, 4);
            break;
        case 2:
            CYCLE(config.security_level, 3);
            break;
        case 3:
            CYCLE(config.algo_mode, 3);
            break;
        case 4:
            CYCLE(config.face_selection_policy, 2);
            break;
        case 5:
            CYCLE(config.dump_mode, 4);
            break;
        case 6:
            CYCLE(config.frontal_face_policy, 3);
            break;
        case 7:
            CYCLE(config.person_motion_mode, 2);
            break;
        case 8:
            config.max_spoofs = config.max_spoofs ? 0 : 3;
            break;
        case 9:
            config.gpio_auth_toggling = !config.gpio_auth_toggling;
            break;
        case 10:
            config.match_thresh = (config.match_thresh + 100) % 1100;
            break;
        case 11:
            config.manual_exposure_time_us = (config.manual_exposure_time_us + 500) % 5500;
            break;
        case 12:
            config.manual_gain = (config.manual_gain + 50) % 550;
            break;
        case 13:
            config.rect_enable = !config.rect_enable;
            break;
        case 14:
            config.landmarks_enable = !config.landmarks_enable;
            break;
        case 15:
            /* Cycle through 0 (unlimited) -> 50 -> 100 -> 150 (max) -> 0 */
            if (config.distance_limit_cm == 0)
                config.distance_limit_cm = 50;
            else if (config.distance_limit_cm < 100)
                config.distance_limit_cm = 100;
            else if (config.distance_limit_cm < RSID_MAX_DISTANCE_CM)
                config.distance_limit_cm = RSID_MAX_DISTANCE_CM;
            else
                config.distance_limit_cm = 0;
            break;
        case 16:
            config.distance_enabled = !config.distance_enabled;
            break;
        default:
            printf("Enter 1-16 or 'q'\n");
            continue;
        }
    }

    s = rsid_set_device_config(&g_ctx, &config);
    CLI_PRINT("Set config: %s\n", (s == RSID_Ok) ? "OK" : "FAILED");
}

static void print_menu(void)
{
    printf("\nChoose an option ('?' for menu, 'q' to quit):\n");
    printf("  'e' Enroll\n");
    printf("  'a' Authenticate\n");
    printf("  't' Authenticate loop\n");
    printf("  'd' Delete all users\n");
    printf("  'r' Remove user\n");
    printf("  'u' List users\n");
    printf("  's' Show device config\n");
    printf("  'S' Set device config\n");
    printf("  'x' Ping\n");
    printf("  'v' Firmware version\n");
    printf("  'n' Serial number\n");
    printf("  'T' Temperature (F50x)\n");
    printf("  'R' Reboot\n");
}

static void prompt_string(const char* prompt, char* buf, int buf_size)
{
    buf[0] = '\0';
    do
    {
        printf("%s", prompt);
        if (!fgets(buf, buf_size, stdin))
            break;
        buf[strcspn(buf, "\r\n")] = '\0';
    } while (buf[0] == '\0');
}

static const char* g_port;

/* Open port, run a command, close port */
static int with_serial(void (*fn)(void))
{
    if (open_serial(g_port) != 0)
        return -1;
    fn();
    close_serial();
    return 0;
}

/* Same but passes a string argument */
static int with_serial_str(void (*fn)(const char*), const char* arg)
{
    if (open_serial(g_port) != 0)
        return -1;
    fn(arg);
    close_serial();
    return 0;
}

int main(int argc, char* argv[])
{
    char input[256];
    rsid_status init_status;

    if (argc >= 2 && strcmp(argv[1], "-v") == 0)
    {
        g_plat_verbose = 1;
        argc--;
        argv++;
    }

    if (argc < 2)
    {
        CLI_PRINT("Usage: %s [-v] <SERIAL_PORT>\n", argv[0]);
#ifdef _WIN32
        CLI_PRINT("Example: %s COM3\n", argv[0]);
#else
        CLI_PRINT("Example: %s /dev/ttyACM0\n", argv[0]);
#endif
        return 1;
    }

    g_port = argv[1];
    printf("RealSenseID Embedded SDK v%s\n", rsid_version());

    /* Initialize SDK context once */
    g_ctx.platform.send = plat_send;
    g_ctx.platform.recv = plat_recv;
    g_ctx.platform.get_time_ms = plat_get_time_ms;
    g_ctx.platform.purge = plat_purge;
    g_ctx.platform.sleep_ms = plat_sleep_ms;
    g_ctx.platform.debug = cli_debug;
    init_status = rsid_init(&g_ctx);
    if (init_status != RSID_Ok)
    {
        printf("SDK init failed: %s\n", status_str(init_status));
        return 1;
    }

    /* Verify port works */
    if (open_serial(g_port) != 0)
        return 1;
    close_serial();
    printf("Using %s\n", g_port);

    print_menu();

    while (1)
    {
        printf("\n> ");
        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\r\n")] = '\0';

        if (input[0] == '\0' || input[1] != '\0')
            continue; /* single char only */

        switch (input[0])
        {
        case '?':
            print_menu();
            break;
        case 'e': {
            char name[64];
            prompt_string("User id to enroll: ", name, sizeof(name));
            if (name[0] != '\0')
                with_serial_str(cmd_enroll, name);
            break;
        }
        case 'a':
            with_serial(cmd_authenticate);
            break;
        case 't':
            with_serial(cmd_authenticate_loop);
            break;
        case 'd':
            with_serial(cmd_remove_all);
            break;
        case 'r': {
            char name[64];
            prompt_string("User id to remove: ", name, sizeof(name));
            if (name[0] != '\0')
                with_serial_str(cmd_remove, name);
            break;
        }
        case 'u':
            with_serial(cmd_users);
            break;
        case 's':
            with_serial(cmd_config);
            break;
        case 'S':
            with_serial(cmd_set_config);
            break;
        case 'x':
            with_serial(cmd_ping);
            break;
        case 'v':
            with_serial(cmd_version);
            break;
        case 'n':
            with_serial(cmd_serial_number);
            break;
        case 'T':
            with_serial(cmd_temperature);
            break;
        case 'R': {
            if (open_serial(g_port) != 0)
                break;
            CLI_PRINT("Reboot: %s\n", (rsid_reboot(&g_ctx) == RSID_Ok) ? "OK" : "FAILED");
            close_serial();
            break;
        }
        case 'q':
            goto done;
        default:
            CLI_PRINT("Unknown command '%c'. Press '?' for menu.\n", input[0]);
            break;
        }
    }

done:
    CLI_PRINT("Bye..\n");
    return 0;
}
