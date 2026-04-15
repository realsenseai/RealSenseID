// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

void init_face_authenticator(pybind11::module& m);
void init_preview(pybind11::module& m);
void init_device_controller(pybind11::module& m);
void init_discover_devices(pybind11::module& m);
void init_fw_updater(pybind11::module& m);
void init_logging(pybind11::module& m);


// Call rsid function and throw if the retval is not Status::Ok
// This so that the python will raise exception on such cases
#define RSID_THROW_ON_ERROR(rsid_call)                                                                                                     \
    do                                                                                                                                     \
    {                                                                                                                                      \
        auto rv = rsid_call;                                                                                                               \
        if (rv != RealSenseID::Status::Ok)                                                                                                 \
            throw std::runtime_error(RealSenseID::Description(rv));                                                                        \
    } while (0)
