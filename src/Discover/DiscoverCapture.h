// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

// Internal header for capture device discovery.
//
// Used by DiscoverDevices_Linux.cc to match serial ports to camera devices.
// Two backends exist — CMake selects exactly one at build time:
//   - DiscoverCapture_V4L2.cc   (RSID_LIBUVC=OFF) — uses V4L2 sysfs, no external deps
//   - DiscoverCapture_LibUVC.cc (RSID_LIBUVC=ON)  — uses libuvc + libusb
//
// Both implement DiscoverCaptureEntries(), returning a list of {index, bus, addr}.
// The caller matches entries to serial ports by USB bus/addr.

#pragma once

#include <vector>

namespace RealSenseID
{

// A capture device identified by its index and USB location.
//   V4L2 backend:  index = /dev/videoN number
//   libuvc backend: index = position in uvc_get_device_list()
struct CaptureDeviceEntry
{
    int index = -1;        // capture device index (backend-specific meaning)
    unsigned int bus = 0;  // USB bus number
    unsigned int addr = 0; // USB device address
};

// Enumerate all available capture devices.
// Implemented by the selected backend (DiscoverCapture_V4L2.cc or DiscoverCapture_LibUVC.cc).
std::vector<CaptureDeviceEntry> DiscoverCaptureEntries();

// Find the capture index for a device at the given USB bus/address.
// Returns -1 if no matching entry is found.
inline int FindCaptureIndex(const std::vector<CaptureDeviceEntry>& entries, unsigned int bus, unsigned int addr)
{
    for (const auto& e : entries)
    {
        if (e.bus == bus && e.addr == addr)
            return e.index;
    }
    return -1;
}

} // namespace RealSenseID
