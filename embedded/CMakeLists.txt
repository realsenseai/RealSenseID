cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0077 NEW)
project(rsid_embedded C)

# Version — must match RSID_EMBEDDED_VER_* in src/rsid.h
set(RSID_EMBEDDED_VERSION_MAJOR 3)
set(RSID_EMBEDDED_VERSION_MINOR 3)
set(RSID_EMBEDDED_VERSION_PATCH 1)

# When built as part of Host SDK, verify versions match
if(DEFINED RSID_VERSION_MAJOR)
    if(NOT RSID_EMBEDDED_VERSION_MAJOR EQUAL RSID_VERSION_MAJOR OR
       NOT RSID_EMBEDDED_VERSION_MINOR EQUAL RSID_VERSION_MINOR OR
       NOT RSID_EMBEDDED_VERSION_PATCH EQUAL RSID_VERSION_PATCH)
        message(FATAL_ERROR "Embedded ${RSID_EMBEDDED_VERSION_MAJOR}.${RSID_EMBEDDED_VERSION_MINOR}.${RSID_EMBEDDED_VERSION_PATCH}"
                " != Host ${RSID_VERSION} — update embedded/CMakeLists.txt and src/rsid.h")
    endif()
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(rsid_embedded STATIC
    ${SRC_DIR}/rsid.h
    ${SRC_DIR}/rsid_common.h
    ${SRC_DIR}/rsid_packet.h
    ${SRC_DIR}/rsid_session.h
    ${SRC_DIR}/rsid_packet.c
    ${SRC_DIR}/rsid_session.c
    ${SRC_DIR}/rsid_authenticator.c
    ${SRC_DIR}/rsid_device_controller.c
)

target_include_directories(rsid_embedded PUBLIC ${SRC_DIR})

option(RSID_DEBUG "Enable debug logging with [timestamp] [DEBUG] prefix" OFF)
if(RSID_DEBUG)
    target_compile_definitions(rsid_embedded PUBLIC RSID_DEBUG=1)
endif()

set_target_properties(rsid_embedded PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "rsid_embedded"
)

if(MSVC)
    target_compile_definitions(rsid_embedded PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# CLI test tool (Windows and Linux)
if(WIN32)
    set(PLAT_SRC cli/plat_win32.c)
else()
    set(PLAT_SRC cli/plat_linux.c)
endif()
add_executable(rsid_embedded_cli cli/rsid_cli.c ${PLAT_SRC})
target_link_libraries(rsid_embedded_cli PRIVATE rsid_embedded)
set_target_properties(rsid_embedded_cli PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    FOLDER "rsid_embedded"
)
if(MSVC)
    target_compile_definitions(rsid_embedded_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT rsid_embedded_cli)
endif()

# Unit tests (on by default, disable with -DRSID_EMBEDDED_TESTS=OFF)
option(RSID_EMBEDDED_TESTS "Build embedded SDK unit tests" ON)
if(RSID_EMBEDDED_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
