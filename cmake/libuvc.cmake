include(FetchContent REQUIRED)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif ()

if (NOT ANDROID)
    # libuvc will search for those and will not display a helpful message.
    # So here we make sure that we do the checks before libuvc and display useful message to the user
    find_package(PkgConfig)
    pkg_check_modules(LibUSB libusb-1.0)

    if (LibUSB_FOUND)
        message(STATUS "libusb-1.0 found using pkgconfig")
    else ()
        message(FATAL_ERROR "Could not find libusb-1.0. On Debian/Ubuntu, you need to do the following:
        > sudo apt-get update   # First: Update package repository

        # Then install the following packages:
        > sudo apt install libusb-1.0-0 libusb-1.0-0-dev pkg-config
        .")
    endif ()
else ()
    # On Android, we need to also find the libusb headers
    set(LIBUSB_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/libusb-src/" CACHE STRING "" FORCE)
endif ()

set(LIBUVC_CUSTOM_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/libuvc-custom-build/${CMAKE_BUILD_TYPE}")

FetchContent_Declare(
        libuvc
        SOURCE_DIR ${THIRD_PARTY_DIRECTORY}/libuvc-0.0.7-custom
        # EXCLUDE_FROM_ALL # When we reach cmake 3.28
        SYSTEM
)

set(libuvc_BUILD_EXAMPLE OFF CACHE BOOL "")
set(libuvc_BUILD_TEST OFF CACHE BOOL "")
set(libuvc_ENABLE_UVC_DEBUGGING OFF CACHE BOOL "")
set(libuvc_CMAKE_BUILD_TARGET "Static" CACHE STRING "" FORCE)

set(libuvc_CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "")
set(libuvc_CMAKE_BUILD_TARGET Release CACHE BOOL "")

FetchContent_MakeAvailable(libuvc)
