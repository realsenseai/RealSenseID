project(LibRealSenseID_Py CXX)

if (RSID_SECURE)
    message(FATAL_ERROR "Secure mode is not supported for python wrapper")
endif ()

# Make sure that Python3 interpreter and dev headers are present on the system
find_package(Python3 COMPONENTS Interpreter Development)

include(FetchContent)

if(POLICY CMP0148)
  cmake_policy(SET CMP0148 NEW)
endif()

FetchContent_Declare(
  pybind11
   URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
   URL_HASH SHA256=d0a116e91f64a4a2d8fb7590c34242df92258a61ec644b79127951e821b47be6
)

FetchContent_MakeAvailable(pybind11)

set(PYBIND11_CPP_STANDARD -std=c++11)
set(PY_TARGET rsid_py)

set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

set(SOURCES 
    "${SRC_PATH}/rsid_py.cc" 
    "${SRC_PATH}/rsid_py.h"
    "${SRC_PATH}/face_auth_py.cc"    
    "${SRC_PATH}/discover_devices_py.cc"
    "${SRC_PATH}/device_controller_py.cc"
    "${SRC_PATH}/fw_updater_py.cc"
    "${SRC_PATH}/logging_py.cc"
    )

if (RSID_PREVIEW)
    list(APPEND SOURCES "${SRC_PATH}/preview_py.cc")    
endif ()

pybind11_add_module(${PY_TARGET} ${HEADERS} ${SOURCES})

target_link_libraries(${PY_TARGET} PRIVATE ${LIBRSID_CPP_TARGET})

set_target_properties(${PY_TARGET}
        PROPERTIES
        COMPILE_DEFINITIONS $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        FOLDER "wrappers"
        )

if (RSID_PREVIEW)
    target_compile_definitions(${PY_TARGET} PUBLIC RSID_PREVIEW)
endif()

set_common_compile_opts(${PY_TARGET})

# Place the Python module (.pyd/.so) next to the rsid shared library.
# On Windows rsid.dll lives in bin/, on Linux librsid.so lives in lib/.
# pybind11 modules are MODULE libraries, so LIBRARY_OUTPUT_DIRECTORY controls both .pyd and .so.
if(WIN32)
    set_target_properties(${PY_TARGET} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_PATH}")
else()
    set_target_properties(${PY_TARGET} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${LIBS_OUTPUT_PATH}")
endif()

get_target_property(PROJECT_SOURCES ${PY_TARGET} SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PROJECT_SOURCES})
