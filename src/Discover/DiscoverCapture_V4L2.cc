// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2024 RealSense, Inc. All Rights Reserved.

// V4L2 capture device discovery.
//
// Enumerates /sys/class/video4linux/videoN entries, filters for actual capture
// devices (not metadata nodes), and resolves each to a USB bus/addr pair via sysfs.
//
// A USB camera typically creates multiple /dev/videoN nodes:
//   video0 → Video Capture (the real camera stream)
//   video1 → Metadata Capture (UVC metadata, no video frames)
// We use VIDIOC_QUERYCAP to keep only nodes with V4L2_CAP_VIDEO_CAPTURE.
//
// The sysfs path for a V4L2 device looks like:
//   /sys/devices/.../usbN/N-P/N-P:I.F/video4linux/videoN
// We strip 3 parent levels (videoN → video4linux → N-P:I.F) to reach
// the USB device directory containing busnum/devnum files.

#include "DiscoverCapture.h"
#include "Logger.h"

#include <string>
#include <fstream>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <limits.h>

static const char* LOG_TAG = "DiscoverCapture";

namespace RealSenseID
{

// Return the parent directory of a path (strip last component).
static std::string ParentPath(const std::string& path)
{
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return {};
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

// Read a single unsigned integer from a sysfs file (e.g. "busnum", "devnum").
static bool ReadUnsignedIntFile(const std::string& path, unsigned int& value)
{
    std::ifstream file(path);
    if (!file)
        return false;
    file >> value;
    return file.good();
}

// Check if /dev/videoN supports V4L2_CAP_VIDEO_CAPTURE (not just metadata).
static bool IsVideoCaptureDevice(int video_index)
{
    std::string dev_name = "/dev/video" + std::to_string(video_index);
    int fd = open(dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd < 0)
        return false;

    struct v4l2_capability cap = {};
    bool is_capture = false;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
        is_capture = (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;

    close(fd);
    return is_capture;
}

// Discover all V4L2 capture devices and map them to USB bus/addr.
//
// How it works, step by step:
//
// 1. Walk /sys/class/video4linux/ — the kernel creates a "videoN" symlink
//    here for every registered V4L2 device node.
//
// 2. For each "videoN" entry, extract N (the /dev/videoN index).
//
// 3. Filter: open /dev/videoN and call VIDIOC_QUERYCAP. Only keep nodes
//    whose device_caps include V4L2_CAP_VIDEO_CAPTURE. This filters out
//    metadata-only nodes (e.g. video1) that the UVC driver creates alongside
//    the real capture node (e.g. video0).
//
// 4. Resolve the sysfs symlink to a real path using realpath(). Example:
//      /sys/class/video4linux/video0
//      -> /sys/devices/pci0000:00/0000:00:14.0/usb3/3-4/3-4.2/3-4.2:1.0/video4linux/video0
//
// 5. Strip 3 directory levels to reach the USB device directory:
//      .../3-4.2:1.0/video4linux/video0   (start)
//      .../3-4.2:1.0/video4linux           strip "video0"
//      .../3-4.2:1.0                       strip "video4linux"
//      .../3-4.2                           strip "3-4.2:1.0" (interface dir)
//    Now we're at the USB device dir which contains busnum and devnum files.
//
// 6. Read busnum and devnum — these identify the physical USB device and
//    are the same values that DiscoverDevices() reads for serial ports.
//    This allows matching: serial port <-> camera by shared (bus, addr).
//
// 7. Store {video_index, bus, addr} in the result. The caller uses
//    FindCaptureIndex() to look up the video index for a given bus/addr.
//
std::vector<CaptureDeviceEntry> DiscoverCaptureEntries()
{
    std::vector<CaptureDeviceEntry> entries;
    constexpr const char* v4l_base = "/sys/class/video4linux/";

    // Step 1: open the sysfs directory listing all V4L2 devices
    DIR* dir = opendir(v4l_base);
    if (!dir)
    {
        LOG_WARNING(LOG_TAG, "DiscoverCaptureEntries: cannot open %s", v4l_base);
        return entries;
    }

    while (dirent* entry = readdir(dir))
    {
        // Step 2: only look at entries named "videoN"
        std::string name = entry->d_name;
        if (name.rfind("video", 0) != 0)
            continue;

        int video_index = -1;
        try
        {
            video_index = std::stoi(name.substr(5)); // "video0" -> 0
        }
        catch (...)
        {
            continue;
        }

        // Step 3: filter out metadata-only nodes
        if (!IsVideoCaptureDevice(video_index))
            continue;

        // Step 4: resolve sysfs symlink to real device path
        std::string full_path = v4l_base + name;
        char real_buf[PATH_MAX] = {};
        if (!realpath(full_path.c_str(), real_buf))
            continue;

        // Step 5: strip 3 levels (videoN / video4linux / interface) to reach USB device dir
        std::string usb_device_dir = ParentPath(ParentPath(ParentPath(std::string(real_buf))));
        if (usb_device_dir.empty())
            continue;

        // Step 6: read USB bus number and device address
        unsigned int bus = 0, addr = 0;
        if (!ReadUnsignedIntFile(usb_device_dir + "/busnum", bus) || !ReadUnsignedIntFile(usb_device_dir + "/devnum", addr))
            continue;

        // Step 7: record the mapping
        entries.push_back({video_index, bus, addr});
    }
    closedir(dir);
    return entries;
}

} // namespace RealSenseID
