// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <RealSenseID/DiscoverDevices.h>
#include "rsid_py.h"

#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
namespace py = pybind11;

void init_discover_devices(pybind11::module& m)
{
    using namespace RealSenseID;

    m.def("discover_devices", &RealSenseID::DiscoverDevices);
    m.def("discover_capture", &RealSenseID::DiscoverCapture);
    m.def("discover_device_type", &RealSenseID::DiscoverDeviceType);

    py::class_<DeviceInfo>(m, "DeviceInfo")
        .def(py::init<>())
        .def_readonly("serial_port", &DeviceInfo::serialPort)
        .def_readonly("device_type", &DeviceInfo::deviceType)
        .def_readonly("serial_number", &DeviceInfo::serialNumber)
        .def_readonly("camera_number", &DeviceInfo::cameraNumber);
}
