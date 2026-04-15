// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <RealSenseID/DeviceController.h>
#include <RealSenseID/DiscoverDevices.h>
#include "rsid_py.h"
#include <string>

namespace py = pybind11;

void init_device_controller(pybind11::module& m)
{
    using namespace RealSenseID;
    py::class_<DeviceController>(m, "DeviceController")
        // ctor with device type and  serial port to connect to
        .def(py::init([](const DeviceType deviceType, const std::string& port) { // ctor with port to connect
            auto f = std::make_unique<DeviceController>(deviceType);
            RSID_THROW_ON_ERROR(f->Connect(SerialConfig {port.c_str()}));
            return f;
        }))

        .def(py::init([](const std::string& port) { // ctor with port to connect
            auto f = std::make_unique<DeviceController>();
            RSID_THROW_ON_ERROR(f->Connect(SerialConfig {port.c_str()}));
            return f;
        }))

        .def("__enter__", [](DeviceController& self) { return &self; })
        .def("__exit__", [](DeviceController& self, py::handle, py::handle, py::handle) { self.Disconnect(); })

        .def(
            "connect",
            [](DeviceController& self, const std::string& port) { RSID_THROW_ON_ERROR(self.Connect(SerialConfig {port.c_str()})); },
            py::arg("port").none(false), py::call_guard<py::gil_scoped_release>())

        .def("disconnect", &DeviceController::Disconnect, py::call_guard<py::gil_scoped_release>())
        .def(
            "reboot", [](DeviceController& self) { RSID_THROW_ON_ERROR(self.Reboot()); }, py::call_guard<py::gil_scoped_release>())
        .def(
            "query_firmware_version",
            [](DeviceController& self) {
                std::string version;
                RSID_THROW_ON_ERROR(self.QueryFirmwareVersion(version));
                return version;
            },
            py::call_guard<py::gil_scoped_release>())

        .def(
            "query_serial_number",
            [](DeviceController& self) {
                std::string serial;
                RSID_THROW_ON_ERROR(self.QuerySerialNumber(serial));
                return serial;
            },
            py::call_guard<py::gil_scoped_release>())
        .def("ping", &DeviceController::Ping, py::call_guard<py::gil_scoped_release>())
        .def(
            "fetch_log",
            [](DeviceController& self) {
                std::string log;
                RSID_THROW_ON_ERROR(self.FetchLog(log));
                return log;
            },
            py::call_guard<py::gil_scoped_release>())
        .def(
            "set_color_gains", [](DeviceController& self, int red, int blue) { RSID_THROW_ON_ERROR(self.SetColorGains(red, blue)); },
            py::arg("red"), py::arg("blue"), py::doc("Set red+blue color gains. Valid range: 0-511"),
            py::call_guard<py::gil_scoped_release>())
        .def(
            "get_color_gains",
            [](DeviceController& self) {
                int red, blue;
                RSID_THROW_ON_ERROR(self.GetColorGains(red, blue));
                return std::make_tuple(red, blue);
            },
            py::doc("Get device color gains as a tuple (red,blue)"), py::call_guard<py::gil_scoped_release>())
        .def(
            "query_bspver",
            [](DeviceController& self) {
                std::string bspver;
                RSID_THROW_ON_ERROR(self.QueryBspVer(bspver));
                return bspver;
            },
            py::call_guard<py::gil_scoped_release>());
}