// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <RealSenseID/Logging.h>
#include "rsid_py.h"

#include <string>

namespace py = pybind11;


static RealSenseID::LogCallback s_log_callback_py;

void log_callback_wrapper(RealSenseID::LogLevel level, const char* msg)
{
    py::gil_scoped_acquire acquire;
    if (s_log_callback_py)
        s_log_callback_py(level, msg);
}


void init_logging(pybind11::module& m)
{
    using namespace RealSenseID;
    py::enum_<LogLevel>(m, "LogLevel")
        .value("Trace", LogLevel::Trace)
        .value("Debug", LogLevel::Debug)
        .value("Info", LogLevel::Info)
        .value("Warning", LogLevel::Warning)
        .value("Error", LogLevel::Error)
        .value("Critical", LogLevel::Critical)
        .value("Off", LogLevel::Off);

    m.def(
        "set_log_callback",
        [](LogCallback& clbk, LogLevel level, bool do_formatting) {
            s_log_callback_py = clbk;
            RealSenseID::SetLogCallback(log_callback_wrapper, level, do_formatting);
        },
        py::arg("callback"), py::arg("log_level"), py::arg("do_formatting") = true, py::call_guard<py::gil_scoped_release>());

    m.add_object("__cleanup_logger", py::capsule([]() { s_log_callback_py = nullptr; }));
}