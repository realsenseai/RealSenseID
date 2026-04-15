// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#ifdef WIN32
#if rsid_secure_helper_EXPORTS
#define RSID_SIG_EXAMPLE_API __declspec(dllexport)
#else
#define RSID_SIG_EXAMPLE_API __declspec(dllimport)
#endif // rsid_secure_helper_EXPORTS
#elif LINUX
#define RSID_SIG_EXAMPLE_API __attribute__((visibility("default")))
#else
#define RSID_SIG_EXAMPLE_API
#endif // WIN32