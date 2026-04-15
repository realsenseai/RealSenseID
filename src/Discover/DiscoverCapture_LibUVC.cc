// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// libuvc capture device discovery.
//
// Enumerates all UVC devices via libuvc and records their bus/address.
//
// Note: we intentionally skip uvc_get_device_descriptor() — it sends USB control
// transfers to fetch string descriptors and can hang indefinitely when the device
// is in a bad state. VID/PID filtering is not needed here because DiscoverDevices()
// already identifies RSID devices via sysfs; we only need the bus/addr mapping.

#include "DiscoverCapture.h"
#include "Logger.h"
#include "libuvc/libuvc.h"

#include <sstream>
#include <stdexcept>

static const char* LOG_TAG = "DiscoverCapture";

namespace RealSenseID
{

static void ThrowIfFailedUVC(const char* call, uvc_error_t status)
{
    if (status != UVC_SUCCESS)
    {
        std::stringstream err;
        err << call << "(...) failed with: " << uvc_strerror(status);
        throw std::runtime_error(err.str());
    }
}

std::vector<CaptureDeviceEntry> DiscoverCaptureEntries()
{
    std::vector<CaptureDeviceEntry> entries;
    uvc_context_t* ctx = nullptr;
    uvc_device_t** list = nullptr;

    try
    {
        LOG_DEBUG(LOG_TAG, "uvc_init");
        ThrowIfFailedUVC("uvc_init", uvc_init(&ctx, nullptr));
        LOG_DEBUG(LOG_TAG, "uvc_get_device_list");
        ThrowIfFailedUVC("uvc_get_device_list", uvc_get_device_list(ctx, &list));

        int idx = 0;
        for (uvc_device_t** it = list; *it; ++it, ++idx)
        {
            uvc_device_t* dev = *it;
            auto bus = static_cast<unsigned int>(uvc_get_bus_number(dev));
            auto addr = static_cast<unsigned int>(uvc_get_device_address(dev));
            entries.push_back({idx, bus, addr});
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARNING(LOG_TAG, "DiscoverCaptureEntries: %s", e.what());
    }

    if (list)
        uvc_free_device_list(list, 1);
    if (ctx)
        uvc_exit(ctx);

    return entries;
}

} // namespace RealSenseID
