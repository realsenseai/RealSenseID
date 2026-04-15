// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "RealSenseID/Preview.h"


namespace py = pybind11;

using ImageCallbackFun = std::function<void(RealSenseID::Image)>;

class PreviewException : public std::runtime_error
{
public:
    explicit PreviewException(const std::string& message) : runtime_error(message)
    {
    }
};

// Callback support using virtual methods
class PreviewImageCallbackPy : public RealSenseID::PreviewImageReadyCallback
{
    ImageCallbackFun _preview_clbk;
    ImageCallbackFun _snapshot_clbk;

public:
    explicit PreviewImageCallbackPy(ImageCallbackFun& preview_callback, ImageCallbackFun& snapshot_callback) :
        _preview_clbk {preview_callback}, _snapshot_clbk {snapshot_callback}
    {
    }
    void OnPreviewImageReady(const RealSenseID::Image& image) override
    {
        py::gil_scoped_acquire acquire;
        if (_preview_clbk)
        {
            _preview_clbk(image);
        }
    }

    void OnSnapshotImageReady(const RealSenseID::Image& image) override
    {
        py::gil_scoped_acquire acquire;
        if (_snapshot_clbk)
        {
            _snapshot_clbk(image);
        }
    }
};

// store preview and callback as unique pointers, so that the callback won't get deleted too soon
class PreviewPy
{
    std::unique_ptr<PreviewImageCallbackPy> _img_clbk;
    std::unique_ptr<RealSenseID::Preview> _preview;

public:
    explicit PreviewPy(RealSenseID::PreviewConfig config)
    {
        _preview = std::make_unique<RealSenseID::Preview>(config);
    }

    void Start(ImageCallbackFun preview_callback = nullptr, ImageCallbackFun snapshot_callback = nullptr)
    {
        Stop();
        _img_clbk = std::make_unique<PreviewImageCallbackPy>(preview_callback, snapshot_callback);
        if (!_preview->StartPreview(*_img_clbk))
            throw PreviewException("StartPreview failed");
    }

    void Stop()
    {
        if (!_preview->StopPreview())
            throw PreviewException("StopPreview failed");
    }
};


void init_preview(pybind11::module& m)
{
    using RealSenseID::Image;
    using RealSenseID::ImageMetadata;
    using RealSenseID::PreviewConfig;
    using RealSenseID::PreviewMode;

    py::register_exception<PreviewException>(m, "PreviewException", PyExc_RuntimeError);

    py::enum_<PreviewMode>(m, "PreviewMode")
        .value("MJPEG_1080P", PreviewMode::MJPEG_1080P)
        .value("MJPEG_720P", PreviewMode::MJPEG_720P)
        .value("RAW10_1080P", PreviewMode::RAW10_1080P);

    py::class_<PreviewConfig>(m, "PreviewConfig")
        .def(py::init<>())
        .def_readwrite("device_type", &PreviewConfig::deviceType)
        .def_readwrite("camera_number", &PreviewConfig::cameraNumber)
        .def_readwrite("preview_mode", &PreviewConfig::previewMode)
        .def_readwrite("portrait_mode", &PreviewConfig::portraitMode)
        .def_readwrite("rotate_raw", &PreviewConfig::rotateRaw)
        .def_readwrite("skip_decode", &PreviewConfig::skip_decode);

    py::class_<ImageMetadata>(m, "ImageMetadata")
        .def(py::init<>())
        .def_readonly("timestamp", &ImageMetadata::timestamp)
        .def_readonly("status", &ImageMetadata::status)
        .def_readonly("exposure", &ImageMetadata::exposure)
        .def_readonly("gain", &ImageMetadata::gain)
        .def_readonly("sensor_id", &ImageMetadata::sensor_id)
        .def_property_readonly("is_snapshot", [](const ImageMetadata& self) { return self.is_snapshot != 0; })
        .def_property_readonly("led", [](const ImageMetadata& self) -> bool { return self.led != 0; });

    py::class_<Image>(m, "Image")
        .def(py::init<>())
        /*.def_readonly("buffer", &Image::buffer)*/
        .def_readonly("size", &Image::size)
        .def_readonly("width", &Image::width)
        .def_readonly("height", &Image::height)
        .def_readonly("stride", &Image::stride)
        .def_readonly("number", &Image::number)
        .def_readonly("metadata", &Image::metadata)
        .def("get_buffer", [](const Image& self) {
            return py::memoryview::from_memory(self.buffer,                             // buffer pointer
                                               (Py_ssize_t)sizeof(uint8_t) * self.size, // buffer size
                                               true                                     // readonly
            );
        });

    py::class_<PreviewPy>(m, "Preview")
        .def(py::init<const PreviewConfig&>())
        .def("start", &PreviewPy::Start, py::call_guard<py::gil_scoped_release>(), py::arg("preview_callback").none(true),
             py::arg("snapshot_callback").none(true))
        .def("stop", &PreviewPy::Stop, py::call_guard<py::gil_scoped_release>());
}