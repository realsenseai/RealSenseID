project(LibRealSenseID_C)

set(INC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/include/rsid_c")
set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(HEADERS ${INC_PATH}/rsid_status.h ${INC_PATH}/rsid_export.h ${INC_PATH}/rsid_client.h)
set(SOURCES ${SRC_PATH}/rsid_c_client.cc ${SRC_PATH}/rsid_c_device_controller.cc)

if (MSVC)
    list(APPEND HEADERS "${INC_PATH}/rsid_fw_updater.h")
    list(APPEND SOURCES "${SRC_PATH}/rsid_c_fw_updater.cc")
endif ()

if(RSID_PREVIEW)
    list(APPEND HEADERS "${INC_PATH}/rsid_preview.h")
    list(APPEND SOURCES "${SRC_PATH}/rsid_c_preview.cc")
endif()

add_library(${LIBRSID_C_TARGET} SHARED ${HEADERS} ${SOURCES})

set_common_compile_opts(${LIBRSID_C_TARGET})

target_include_directories(${LIBRSID_C_TARGET}
    PUBLIC 
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/RealSenseID>")

target_link_libraries(${LIBRSID_C_TARGET} PRIVATE ${LIBRSID_CPP_TARGET})

set_target_properties(${LIBRSID_C_TARGET} 
    PROPERTIES
        COMPILE_DEFINITIONS $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        DEBUG_POSTFIX ${RSID_DEBUG_POSTFIX}
        FOLDER "wrappers"
)

if(RSID_SECURE)
    target_compile_definitions(${LIBRSID_C_TARGET} PUBLIC RSID_SECURE)
endif()


get_target_property(PROJECT_SOURCES ${LIBRSID_C_TARGET} SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PROJECT_SOURCES})
