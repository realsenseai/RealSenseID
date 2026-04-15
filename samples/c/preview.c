// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "rsid_c/rsid_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef LINUX
#include <unistd.h>
#else
#include <Windows.h>
#endif

void render(rsid_image image, void* ctx)
{
    printf("frame #%d: %dX%d (%d bytes)\n", image.number, image.width, image.height, image.size);
}

int main()
{
    rsid_preview_config config;
    rsid_preview* preview;
    config.camera_number = -1; // auto detect
    config.preview_mode = MJPEG_1080P;

    preview = rsid_create_preview(&config);
    rsid_start_preview(preview, render, NULL);

    printf("run preview for 30 sec\n");
#ifdef LINUX
    sleep(30); // seconds
#else
    Sleep(30000); // miliseconds
#endif
    rsid_stop_preview(preview);
}
