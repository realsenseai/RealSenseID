/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

#ifndef _WIN32

#include "plat.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

int g_plat_verbose = 0;
static int g_serial_fd = -1;
static int g_cancel_pipe[2] = {-1, -1}; /* self-pipe to wake select on cancel */

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
    if (g_cancel_pipe[1] >= 0)
    {
        uint8_t b = 1;
        if (write(g_cancel_pipe[1], &b, 1) < 0)
            PLAT_ERR(" cancel_io: write to pipe failed (errno %d)\n", errno);
    }
}

/* ---- Transport ---- */

int plat_send(const uint8_t* data, uint32_t len, void* app_ctx)
{
    uint32_t total = 0;
    (void)app_ctx;
    debug_bytes(1, data, len);
    PLAT_DBG("[plat] send %u bytes\n", (unsigned)len);
    while (total < len)
    {
        ssize_t n = write(g_serial_fd, data + total, len - total);
        if (n <= 0)
        {
            PLAT_ERR(" send: write failed (errno %d: %s)\n", errno, strerror(errno));
            return -1;
        }
        total += (uint32_t)n;
    }
    PLAT_DBG("[plat] send: %u bytes ok\n", (unsigned)len);
    return 0;
}

int plat_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx)
{
    uint32_t total = 0;
    struct timespec start, now;
    (void)app_ctx;

    PLAT_DBG("[plat] recv %u bytes, timeout=%u ms\n", (unsigned)len, (unsigned)timeout_ms);
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (total < len)
    {
        fd_set fds;
        struct timeval tv;
        uint32_t elapsed;
        uint32_t remaining;
        ssize_t n;
        int sel;
        int max_fd = g_serial_fd;

        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (uint32_t)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
        if (elapsed >= timeout_ms)
            return -1;
        remaining = timeout_ms - elapsed;

        FD_ZERO(&fds);
        FD_SET(g_serial_fd, &fds);
        if (g_cancel_pipe[0] >= 0)
        {
            FD_SET(g_cancel_pipe[0], &fds);
            if (g_cancel_pipe[0] > max_fd)
                max_fd = g_cancel_pipe[0];
        }
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        sel = select(max_fd + 1, &fds, NULL, NULL, &tv);
        if (sel < 0)
        {
            PLAT_ERR(" recv: select failed (errno %d: %s)\n", errno, strerror(errno));
            return -1;
        }
        if (sel == 0)
            return -1;

        /* Cancel pipe signalled — drain it and bail out */
        if (g_cancel_pipe[0] >= 0 && FD_ISSET(g_cancel_pipe[0], &fds))
        {
            uint8_t drain;
            while (read(g_cancel_pipe[0], &drain, 1) > 0)
            {
            }
            return -1;
        }

        n = read(g_serial_fd, data + total, len - total);
        if (n <= 0)
        {
            PLAT_ERR(" recv: read failed (errno %d: %s)\n", errno, strerror(errno));
            return -1;
        }
        PLAT_DBG("[plat] recv: got %d bytes\n", (int)n);
        total += (uint32_t)n;
    }

    debug_bytes(0, data, len);
    PLAT_DBG("[plat] recv: %u bytes ok\n", (unsigned)len);
    return 0;
}

void plat_purge(void* app_ctx)
{
    (void)app_ctx;
    tcflush(g_serial_fd, TCIOFLUSH);
}

uint32_t plat_get_time_ms(void* app_ctx)
{
    struct timespec ts;
    (void)app_ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void plat_sleep_ms(uint32_t ms, void* app_ctx)
{
    (void)app_ctx;
    usleep(ms * 1000);
}

int open_serial(const char* port)
{
    struct termios tty;

    g_serial_fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_serial_fd < 0)
    {
        PLAT_ERR("Failed to open %s: %s\n", port, strerror(errno));
        return -1;
    }

    /* Set blocking mode */
    fcntl(g_serial_fd, F_SETFL, 0);

    /* Configure 115200 8N1 */
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(g_serial_fd, &tty) != 0)
    {
        PLAT_ERR("tcgetattr failed: %s\n", strerror(errno));
        close(g_serial_fd);
        g_serial_fd = -1;
        return -1;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; /* 8-bit */
    tty.c_cflag &= ~(PARENB | PARODD);          /* no parity */
    tty.c_cflag &= ~CSTOPB;                     /* 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;                    /* no HW flow control */
    tty.c_cflag |= (CLOCAL | CREAD);            /* enable receiver, ignore modem */

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); /* no SW flow control */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0; /* raw mode */
    tty.c_oflag = 0; /* raw output */

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; /* 100ms inter-char timeout */

    if (tcsetattr(g_serial_fd, TCSANOW, &tty) != 0)
    {
        PLAT_ERR("tcsetattr failed: %s\n", strerror(errno));
        close(g_serial_fd);
        g_serial_fd = -1;
        return -1;
    }

    tcflush(g_serial_fd, TCIOFLUSH);

    if (pipe(g_cancel_pipe) != 0)
    {
        PLAT_ERR("pipe failed: %s\n", strerror(errno));
        close(g_serial_fd);
        g_serial_fd = -1;
        return -1;
    }
    fcntl(g_cancel_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(g_cancel_pipe[1], F_SETFL, O_NONBLOCK);

    return 0;
}

void close_serial(void)
{
    if (g_cancel_pipe[0] >= 0)
    {
        close(g_cancel_pipe[0]);
        g_cancel_pipe[0] = -1;
    }
    if (g_cancel_pipe[1] >= 0)
    {
        close(g_cancel_pipe[1]);
        g_cancel_pipe[1] = -1;
    }
    if (g_serial_fd >= 0)
    {
        close(g_serial_fd);
        g_serial_fd = -1;
    }
}

#endif /* !_WIN32 */
