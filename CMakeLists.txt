cmake_minimum_required(VERSION 3.14)

include(cmake/Version.cmake)
rsid_extract_version()
project(RealSenseID VERSION ${RSID_VERSION})

# C++17 and above
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()


# If msvc, make sure __cplusplus is defined and add parallel build option
if(MSVC)    
    string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus /MP")    
endif()

# CCache
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
    message(STATUS "CCache found!")
endif ()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
	# set the timestamps of extracted contents to the time of extraction when using FetchContent() 
	# in order to avoid DOWNLOAD_EXTRACT_TIMESTAMP warning
    cmake_policy(SET CMP0135 NEW)
endif()

# Global properties
set(LIBRSID_CPP_TARGET rsid)
set(LIBRSID_C_TARGET rsid_c)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_build")
set(RSID_DEBUG_POSTFIX _debug)

# ---------------------------------------------------------------------------------------
# Set RSID_MASTER_PROJECT to ON if we are not used via add_subdirectory, but allow overriding
# ---------------------------------------------------------------------------------------
if(NOT DEFINED RSID_MASTER_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(RSID_MASTER_PROJECT ON)
    else()
        set(RSID_MASTER_PROJECT OFF)
    endif()
endif()

# ---------------------------------------------------------------------------------------
# Set default build to release
# ---------------------------------------------------------------------------------------
if(RSID_MASTER_PROJECT AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()
message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})

# Options
option(RSID_DEBUG_CONSOLE "Log everything to console" ON)
option(RSID_DEBUG_FILE "Log everything to rsid_debug.log file" OFF)
option(RSID_DEBUG_SERIAL "Log all serial communication" OFF)
option(RSID_DEBUG_PACKETS "Log packet sent/received over the serial line" OFF)
option(RSID_DEBUG_VALUES "Replace default common values with debug ones" OFF)
option(RSID_SAMPLES "Build samples" OFF)
option(RSID_TIDY "Enable clang-tidy" OFF)
option(RSID_PEDANTIC "Enable extra compiler warnings" OFF)
option(RSID_PROTECT_STACK "Enable stack protection compiler flags" OFF)
option(RSID_DOXYGEN "Build doxygen docs" OFF)
option(RSID_SECURE "Enable secure communication with device" OFF)
option(RSID_TOOLS "Build additional tools" ON)
option(RSID_PY "Build python wrapper" OFF)
option(RSID_PREVIEW "Enable preview" OFF)
if(ANDROID)
    set(RSID_LIBUVC ON CACHE BOOL "Use libuvc instead of V4L2 for preview" FORCE)
elseif(NOT WIN32)
    option(RSID_LIBUVC "Use libuvc instead of V4L2 for preview" OFF)
endif()
option(RSID_ONE2ONE "Enable one to one user matching use case" OFF)
option(RSID_TESTS "Build unit tests" OFF)

# libuvc is only used for preview, so disable it when preview is off
if(RSID_LIBUVC AND NOT RSID_PREVIEW)
    set(RSID_LIBUVC OFF CACHE BOOL "" FORCE)
endif()

if(NOT ANDROID)
    # install option
    option(RSID_INSTALL "Generate the install target and rsidConfig.cmake" OFF)
endif()

if(RSID_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    message(STATUS "Enabled clang-tidy")
endif()

# extra warning flags support if RSID_PEDANTIC is ON
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/flags.cmake")

set(THIRD_PARTY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")

# Paths for lib, bin, install, etc.
set(BIN_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBS_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_PATH}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${LIBS_OUTPUT_PATH}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${LIBS_OUTPUT_PATH}")    
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

include(cmake/OS.cmake)
include(cmake/spdlog.cmake)

if(RSID_SECURE)
    include(cmake/Mbedtls.cmake)
endif()

if(RSID_PREVIEW AND NOT MSVC)
    include(cmake/libjepg-turbo.cmake)
    if(RSID_LIBUVC)
        include(cmake/libuvc.cmake)
    endif()
endif()

if(RSID_SAMPLES)
    add_subdirectory(samples)
endif()

if(RSID_DOXYGEN)
    include(cmake/Doxygen.cmake)
endif()

if (RSID_TOOLS)
    add_subdirectory(tools)
endif ()

if (RSID_TESTS)
    add_subdirectory(tests)
endif ()

add_subdirectory(src)
add_subdirectory(wrappers)

if(RSID_INSTALL)
    include (cmake/Install.cmake)
endif()

include(FeatureSummary)
feature_summary(WHAT ALL)
