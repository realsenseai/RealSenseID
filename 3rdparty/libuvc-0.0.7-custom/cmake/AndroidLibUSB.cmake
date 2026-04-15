include(FetchContent REQUIRED)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif ()

message(STATUS "libuvc/libusb for Android - fetching libusb.")
FetchContent_Declare(
        libusb
        GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
        GIT_TAG "v1.0.28"
        OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(libusb)

set(LIBUSB_BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(LIBUSB_BUILD_TESTING OFF CACHE BOOL "")
set(LIBUSB_INSTALL_TARGETS OFF CACHE BOOL "")
set(LIBUSB_ENABLE_UDEV OFF CACHE BOOL "")

find_package(libusb REQUIRED)

get_target_property(LIBUSB_INCLUDE_DIRS usb-1.0 INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "libusb include dirs: ${LIBUSB_INCLUDE_DIRS}")

# Get the libraries (not always populated unless it's an imported target)
get_target_property(LIBUSB_LIBRARIES usb-1.0 IMPORTED_LOCATION)
message(STATUS "libusb library: ${LIBUSB_LIBRARIES}")

add_library(LibUSB::LibUSB ALIAS usb-1.0)
