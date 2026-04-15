// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

// clang-format off
#ifdef _WIN32
    #if rsid_EXPORTS
        #define RSID_API __declspec(dllexport)
    #else
        #define RSID_API __declspec(dllimport)
    #endif // rsid_EXPORTS
#elif defined(__linux__) || defined(__ANDROID__)
    #define RSID_API __attribute__((visibility("default")))
#else
    #define RSID_API
#endif //_WIN32
// clang-format on