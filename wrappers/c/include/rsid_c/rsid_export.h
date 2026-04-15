// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#ifdef _WIN32
#ifdef rsid_c_EXPORTS
#define RSID_C_API __declspec(dllexport)
#else
#define RSID_C_API __declspec(dllimport)
#endif // rsid_c_EXPORTS
#else
#define RSID_C_API __attribute__((visibility("default")))
#endif // _WIN32
