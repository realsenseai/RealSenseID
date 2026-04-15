// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2024 RealSense, Inc. All Rights Reserved.

// V4L2-based camera capture for Linux preview.
//
// Uses the Video4Linux2 kernel API to stream MJPEG frames from a USB camera
// via memory-mapped buffers. This replaces the libuvc+libusb capture path
// and has no external dependencies beyond kernel headers and libjpeg-turbo
// (for JPEG decoding, handled by StreamConverter).
//
// Architecture:
//   V4L2Streamer   — owns the /dev/videoN fd, mmap buffers, and V4L2 streaming
//   CaptureHandle  — public interface consumed by PreviewImpl; wraps V4L2Streamer
//                    and StreamConverter (MJPEG -> RGB decode)
//
// Streaming uses the V4L2 mmap buffer model:
//   1. VIDIOC_REQBUFS  — ask kernel to allocate N internal buffers
//   2. VIDIOC_QUERYBUF — get each buffer's size and offset
//   3. mmap()          — map each buffer into userspace
//   4. VIDIOC_QBUF     — enqueue all buffers (hand them to the driver)
//   5. VIDIOC_STREAMON  — start capture
//   6. poll() + VIDIOC_DQBUF — wait for a frame, dequeue it
//   7. Process the frame, then VIDIOC_QBUF to return the buffer
//   8. VIDIOC_STREAMOFF + munmap — teardown
//
// Limitations:
//   - MJPEG format only (RAW/W10 not supported — throws on attempt)
//   - No UVC extension metadata (V4L2 doesn't expose it the same way)

#include "V4L2Capture.h"
#include "Logger.h"
#include <vector>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <poll.h>

namespace RealSenseID
{
namespace Capture
{
static const char* LOG_TAG = "V4L2Capture";

// Throw a std::runtime_error with the given message, appending strerror(err_no) if err_no != 0.
[[noreturn]] static void ThrowError(const std::string& msg, int err_no = 0)
{
    if (err_no != 0)
        throw std::runtime_error(msg + ": " + std::strerror(err_no));
    throw std::runtime_error(msg);
}

// V4L2Streamer: manages the lifecycle of a V4L2 streaming session.
//
// Owns the file descriptor for /dev/videoN, the mmap'd capture buffers,
// and the streaming state. Read() returns one MJPEG frame at a time.
//
// Buffer lifecycle:
//   The driver owns a pool of buffers. We mmap them at setup and then cycle
//   them between two states:
//     QUEUED   — buffer is with the driver, waiting to be filled with a frame
//     DEQUEUED — buffer is with us, contains a completed frame we can read
//
//   Read() dequeues one buffer (VIDIOC_DQBUF), returns its data, and
//   re-queues the *previous* buffer (VIDIOC_QBUF) on the next call.
//   This ensures the returned pointer stays valid until the next Read().
class V4L2Streamer
{
public:
    V4L2Streamer(int camera_number, const StreamAttributes& stream_attributes) :
        _stream_attributes(stream_attributes), _camera_index(camera_number)
    {
        OpenAndStartStream();
    }

    ~V4L2Streamer()
    {
        SetStopping(true);
        CloseStream();
    }

    struct Frame
    {
        void* data;
        size_t size;
    };

    // Read a frame from the device.
    //
    // Returns a pointer into the mmap'd buffer — valid until the next Read() call
    // (which re-queues the buffer back to the driver).
    //
    // Uses poll() with a 100ms timeout so we don't block indefinitely,
    // allowing the stopping flag to be checked between attempts.
    Frame Read()
    {
        if (_stopping)
            return {nullptr, 0};

        // Return the previous buffer to the driver before dequeuing a new one.
        // This keeps the previous frame's data valid between Read() calls.
        if (_current_buf_index >= 0)
        {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = _current_buf_index;
            _current_buf_index = -1; // clear before ioctl to avoid double-QBUF if a later step fails
            int rc;
            while ((rc = ioctl(_fd, VIDIOC_QBUF, &buf)) < 0 && errno == EINTR)
            {
                // retry on EINTR
            }
            if (rc < 0)
            {
                HandleIOError("VIDIOC_QBUF", errno);
                return {nullptr, 0};
            }
        }

        // Wait for a frame to become available (100ms timeout)
        struct pollfd fds
        {
        };
        fds.fd = _fd;
        fds.events = POLLIN;

        int r;
        while ((r = poll(&fds, 1, 100)) < 0 && errno == EINTR)
        {
            // retry on EINTR
        }
        if (r < 0)
        {
            HandleIOError("poll", errno);
            return {nullptr, 0};
        }
        // Frame not available yet or stopped
        if (r == 0 || _stopping)
        {
            return {nullptr, 0};
        }

        // Dequeue the completed frame buffer
        struct v4l2_buffer buf
        {
        };
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        int rc;
        while ((rc = ioctl(_fd, VIDIOC_DQBUF, &buf)) < 0 && errno == EINTR)
        {
            // retry on EINTR
        }
        if (rc < 0)
        {
            if (errno != EAGAIN) // EAGAIN is normal for non-blocking fd (while very unlikely here)
                HandleIOError("VIDIOC_DQBUF", errno);
            return {nullptr, 0};
        }

        if (buf.index >= _buffers.size())
        {
            LOG_ERROR(LOG_TAG, "VIDIOC_DQBUF returned out-of-range index %u (have %zu buffers)", buf.index, _buffers.size());
            return {nullptr, 0};
        }
        // Remember which buffer we dequeued so we can return it on the next Read()
        _current_buf_index = static_cast<int>(buf.index);
        return {_buffers[buf.index].start, buf.bytesused};
    }

    void SetStopping(bool stopping)
    {
        _stopping = stopping;
    }

    // prevent copy — only one owner of the fd and mmap buffers
    V4L2Streamer(const V4L2Streamer&) = delete;
    V4L2Streamer& operator=(const V4L2Streamer&) = delete;

private:
    static constexpr unsigned int n_buffers = 4;
    StreamAttributes _stream_attributes;
    int _camera_index = -1;
    int _fd = -1;
    std::atomic<bool> _stopping {false};

    struct Buffer
    {
        void* start = nullptr;
        size_t length = 0;
    };
    std::vector<Buffer> _buffers;
    int _current_buf_index = -1; // index of the last dequeued buffer, or -1

    // Log the error and, if fatal (device lost), set the stopping flag.
    void HandleIOError(const char* context, int err)
    {
        LOG_ERROR(LOG_TAG, "%s: %s", context, std::strerror(err));
        if (err == ENODEV || err == EIO || err == ENXIO)
            _stopping = true;
    }

    // Set up V4L2 capture: open device, set format, allocate and map buffers, start streaming.
    void OpenAndStartStream()
    {
        if (_stream_attributes.format == RAW)
            ThrowError("RAW/W10 and snapshots are not supported in V4L2 implementation");

        // Open the V4L2 device node in non-blocking mode (required for poll-based reading)
        std::string dev_name = "/dev/video" + std::to_string(_camera_index);
        _fd = open(dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (_fd < 0)
            ThrowError("Cannot open " + dev_name, errno);

        // Request MJPEG format at the desired resolution.
        // The driver will reject this if the device doesn't support the exact format/size.
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = _stream_attributes.width;
        fmt.fmt.pix.height = _stream_attributes.height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;

        if (ioctl(_fd, VIDIOC_S_FMT, &fmt) < 0)
        {
            auto err = errno;
            CloseStream();
            ThrowError("VIDIOC_S_FMT failed for " + dev_name + " (" + std::to_string(_stream_attributes.width) + "x" +
                           std::to_string(_stream_attributes.height) + " MJPEG)",
                       err);
        }

        // Query and log the actual frame rate the device is using.
        struct v4l2_streamparm parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(_fd, VIDIOC_G_PARM, &parm) == 0 && parm.parm.capture.timeperframe.numerator > 0)
        {
            LOG_INFO(LOG_TAG, "Frame rate: %u/%u fps", parm.parm.capture.timeperframe.denominator,
                     parm.parm.capture.timeperframe.numerator);
        }

        // Ask the driver to allocate 4 internal capture buffers for mmap streaming.
        // We need at least 2 to double-buffer (one being filled while we read another).
        // Using 4 gives a cushion: if processing a frame takes longer than one frame
        // interval, the driver still has spare buffers to write into instead of dropping frames.
        struct v4l2_requestbuffers req = {};
        req.count = n_buffers;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(_fd, VIDIOC_REQBUFS, &req) < 0)
        {
            auto err = errno;
            CloseStream();
            ThrowError("VIDIOC_REQBUFS failed", err);
        }

        if (req.count < 2)
        {
            CloseStream();
            ThrowError("VIDIOC_REQBUFS returned only " + std::to_string(req.count) + " buffers (need at least 2)");
        }

        // Map each kernel buffer into our address space.
        // QUERYBUF returns the size and kernel-side offset of buffer #i.
        // mmap then creates a userspace pointer to that same physical memory,
        // so when the driver fills a frame we can read it directly — no copy needed.
        _buffers.resize(req.count);

        for (size_t i = 0; i < req.count; ++i)
        {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (ioctl(_fd, VIDIOC_QUERYBUF, &buf) < 0)
            {
                auto err = errno;
                CloseStream();
                ThrowError("VIDIOC_QUERYBUF failed", err);
            }

            // Map buffer #i to userspace:
            //     buf.m.offset — the Video4Linux driver assigns each buffer a unique offset value,
            //     and its mmap handler uses it to identify which buffer to map.
            _buffers[i].length = buf.length;
            _buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, buf.m.offset);

            if (_buffers[i].start == MAP_FAILED)
            {
                auto err = errno;
                CloseStream();
                ThrowError("mmap failed", err);
            }
        }

        // Enqueue all buffers so the driver can start filling them with frames
        for (size_t i = 0; i < req.count; ++i)
        {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(_fd, VIDIOC_QBUF, &buf) < 0)
            {
                auto err = errno;
                CloseStream();
                ThrowError("VIDIOC_QBUF failed", err);
            }
        }

        // Start the capture stream
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(_fd, VIDIOC_STREAMON, &type) < 0)
        {
            auto err = errno;
            CloseStream();
            ThrowError("VIDIOC_STREAMON failed", err);
        }

        // Discard the first frame — it is typically corrupt/incomplete.
        (void)Read();
    }

    // Stop streaming and release all resources (buffers, fd).
    void CloseStream()
    {
        if (_fd >= 0)
        {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(_fd, VIDIOC_STREAMOFF, &type);

            for (auto& b : _buffers)
            {
                if (b.start && b.start != MAP_FAILED)
                {
                    if (munmap(b.start, b.length) == -1)
                        LOG_WARNING(LOG_TAG, "munmap failed with error %s", std::strerror(errno));
                }
            }
            _buffers.clear();
            close(_fd);
            _fd = -1;
        }
    }
};

// CaptureHandle: public interface matching the same API as LibUVCCapture's CaptureHandle.
// PreviewImpl uses this generically — it doesn't know which backend is active.

CaptureHandle::~CaptureHandle() = default;

CaptureHandle::CaptureHandle(const PreviewConfig& config) : _config(config)
{
    _stream_converter = std::make_unique<StreamConverter>(_config);
    StreamAttributes attr = _stream_converter->GetStreamAttributes();
    _v4l2_streamer = std::make_unique<V4L2Streamer>(_config.cameraNumber, attr);
}

// Read one frame and decode it from MJPEG to RGB via StreamConverter.
bool CaptureHandle::Read(RealSenseID::Image* res) const
{
    auto frame = _v4l2_streamer->Read();
    if (frame.data != nullptr && frame.size > 0)
    {
        buffer frame_buffer = {static_cast<unsigned char*>(frame.data), static_cast<unsigned int>(frame.size), 0};
        try
        {
            buffer md_buffer = {nullptr, 0, 0}; // no UVC metadata in V4L2 path
            return _stream_converter->Buffer2Image(res, frame_buffer, md_buffer);
        }
        catch (...)
        {
            return false;
        }
    }
    return false;
}
} // namespace Capture
} // namespace RealSenseID
