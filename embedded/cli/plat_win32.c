/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

#ifdef _WIN32

#include "plat.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HANDLE g_serial = INVALID_HANDLE_VALUE;
int g_plat_verbose = 0;

#define PLAT_ERR(fmt, ...) fprintf(stderr, "[%u] [plat] " fmt, (unsigned)plat_get_time_ms(NULL), ##__VA_ARGS__)
#define PLAT_DBG(fmt, ...)                                                                                                                 \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (g_plat_verbose)                                                                                                                \
            fprintf(stderr, "[%u] [plat] " fmt, (unsigned)plat_get_time_ms(NULL), ##__VA_ARGS__);                                          \
    } while (0)

static void debug_bytes(int is_tx, const uint8_t* data, uint32_t len)
{
    uint32_t i;
    if (!g_plat_verbose)
        return;
    printf("%s %u bytes\n", is_tx ? ">>>" : "<<<", len);
    for (i = 0; i < len; i++)
    {
        uint8_t byte = data[i];
        if (isprint(byte))
            printf("'%c' ", byte);
        else if (byte == '\r')
            printf("'\\r' ");
        else if (byte == '\n')
            printf("'\\n' ");
        else
            printf("%02X ", byte);
        if (i % 40 == 0 && i != 0)
            printf("\n");
    }
    printf("\n");
}


void plat_cancel_io(void)
{
    if (g_serial != INVALID_HANDLE_VALUE)
        CancelIoEx(g_serial, NULL);
}

/* ---- Transport ---- */

/*
 * Synchronous write over an overlapped handle.
 * The handle uses FILE_FLAG_OVERLAPPED so WriteFile needs an OVERLAPPED struct.
 * A dedicated event is required — without it, a concurrent ReadFile on another
 * thread can signal the handle and cause GetOverlappedResult to return early.
 * If WriteFile pends (ERROR_IO_PENDING), we block until the driver completes.
 */
int plat_send(const uint8_t* data, uint32_t len, void* app_ctx)
{
    DWORD written = 0;
    OVERLAPPED osWrite = {0};
    (void)app_ctx;

    debug_bytes(1, data, len);
    PLAT_DBG("send %u bytes\n", (unsigned)len);

    osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osWrite.hEvent == NULL)
    {
        PLAT_ERR("send: CreateEvent failed (error %lu)\n", GetLastError());
        return -1;
    }

    if (!WriteFile(g_serial, data, len, &written, &osWrite))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            PLAT_ERR("send: WriteFile failed (error %lu)\n", GetLastError());
            CloseHandle(osWrite.hEvent);
            return -1;
        }
        PLAT_DBG("send: WriteFile pending, waiting\n");
        if (!GetOverlappedResult(g_serial, &osWrite, &written, TRUE))
        {
            PLAT_ERR("send: GetOverlappedResult failed (error %lu)\n", GetLastError());
            CloseHandle(osWrite.hEvent);
            return -1;
        }
    }
    CloseHandle(osWrite.hEvent);
    if (written != len)
    {
        PLAT_ERR("send: partial write (%lu of %u bytes)\n", written, (unsigned)len);
        return -1;
    }
    PLAT_DBG("send: %u bytes ok\n", (unsigned)len);
    return 0;
}

/*
 * Overlapped read with software timeout. Loops until all `len` bytes arrive.
 * Uses absolute elapsed time to avoid compounding timeouts across partial reads.
 * On timeout or wait failure, CancelIo aborts the pending I/O and a blocking
 * GetOverlappedResult ensures the kernel releases the buffer before we return.
 */
int plat_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx)
{
    DWORD total_read = 0;
    OVERLAPPED osReader = {0};
    uint32_t start_ms = plat_get_time_ms(NULL);
    (void)app_ctx;

    PLAT_DBG("recv %u bytes, timeout=%u ms\n", (unsigned)len, (unsigned)timeout_ms);

    osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osReader.hEvent == NULL)
    {
        PLAT_ERR("recv: CreateEvent failed (error %lu)\n", GetLastError());
        return -1;
    }

    while (total_read < len)
    {
        DWORD bytes_read = 0;
        DWORD bytes_to_read = len - total_read;
        uint32_t elapsed_ms = plat_get_time_ms(NULL) - start_ms;

        if (elapsed_ms >= timeout_ms)
        {
            CloseHandle(osReader.hEvent);
            return -1;
        }

        uint32_t remaining_ms = timeout_ms - elapsed_ms;

        if (!ReadFile(g_serial, data + total_read, bytes_to_read, &bytes_read, &osReader))
        {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING)
            {
                if (err != ERROR_OPERATION_ABORTED)
                    PLAT_ERR("recv: ReadFile failed (error %lu)\n", err);

                CloseHandle(osReader.hEvent);
                return -1;
            }

            /* io is pending, wait for completion or timeout */
            DWORD wait_res = WaitForSingleObject(osReader.hEvent, remaining_ms);

            if (wait_res == WAIT_OBJECT_0)
            {
                if (!GetOverlappedResult(g_serial, &osReader, &bytes_read, FALSE))
                {
                    err = GetLastError();
                    if (err != ERROR_OPERATION_ABORTED)
                        PLAT_ERR("recv: GetOverlappedResult failed (error %lu)\n", err);

                    CloseHandle(osReader.hEvent);
                    return -1;
                }
                PLAT_DBG("recv: overlapped read got %lu bytes\n", bytes_read);
            }
            else if (wait_res == WAIT_TIMEOUT)
            {
                CancelIo(g_serial);
                GetOverlappedResult(g_serial, &osReader, &bytes_read, TRUE);
                CloseHandle(osReader.hEvent);
                return -1;
            }
            else
            {
                PLAT_ERR("recv: WaitForSingleObject failed (result %lu, error %lu)\n", wait_res, GetLastError());
                CancelIo(g_serial);
                GetOverlappedResult(g_serial, &osReader, &bytes_read, TRUE);
                CloseHandle(osReader.hEvent);
                return -1;
            }
        }

        if (bytes_read == 0)
        {
            PLAT_ERR("recv: zero-byte read (connection lost?)\n");
            CloseHandle(osReader.hEvent);
            return -1;
        }
        total_read += bytes_read;
    }

    CloseHandle(osReader.hEvent);
    debug_bytes(0, data, len);
    PLAT_DBG("recv: %u bytes ok\n", (unsigned)len);
    return 0;
}

void plat_purge(void* app_ctx)
{
    (void)app_ctx;
    PurgeComm(g_serial, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

uint32_t plat_get_time_ms(void* app_ctx)
{
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    (void)app_ctx;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint32_t)(now.QuadPart * 1000 / freq.QuadPart);
}

void plat_sleep_ms(uint32_t ms, void* app_ctx)
{
    (void)app_ctx;
    Sleep((DWORD)ms);
}

int open_serial(const char* port)
{
    DCB dcb;
    char full_port[64];
    COMMTIMEOUTS timeouts = {0};

    if (strncmp(port, "\\\\.\\", 4) == 0)
        snprintf(full_port, sizeof(full_port), "%s", port);
    else
        snprintf(full_port, sizeof(full_port), "\\\\.\\%s", port);

    g_serial = CreateFileA(full_port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (g_serial == INVALID_HANDLE_VALUE)
    {
        PLAT_ERR("Failed to open %s (error %lu)\n", full_port, GetLastError());
        return -1;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(g_serial, &dcb))
    {
        PLAT_ERR("GetCommState failed (error %lu)\n", GetLastError());
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
        return -1;
    }

    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(g_serial, &dcb))
    {
        PLAT_ERR("SetCommState failed (error %lu)\n", GetLastError());
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
        return -1;
    }

    /* Zeroed timeouts so the OS doesn't interfere with WaitForSingleObject */
    SetCommTimeouts(g_serial, &timeouts);

    return 0;
}

void close_serial(void)
{
    if (g_serial != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
    }
}

#endif /* _WIN32 */
