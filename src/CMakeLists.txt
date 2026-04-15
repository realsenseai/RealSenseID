project(LibRealSenseID)

add_library(${LIBRSID_CPP_TARGET} SHARED "")

find_package(Threads REQUIRED)

target_link_libraries(${LIBRSID_CPP_TARGET} 
    PRIVATE
        spdlog::spdlog_header_only
        Threads::Threads
        $<$<BOOL:${RSID_SECURE}>:mbedtls>        
)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(HEADERS "${SRC_DIR}/StatusHelper.h")
set(SOURCES        
    "${SRC_DIR}/StatusHelper.cc"
    "${SRC_DIR}/Version.cc"    
)

add_subdirectory("${SRC_DIR}/FaceAuthenticator")
add_subdirectory("${SRC_DIR}/DeviceController")
add_subdirectory("${SRC_DIR}/Discover")
add_subdirectory("${SRC_DIR}/Logger")
add_subdirectory("${SRC_DIR}/PacketManager")
add_subdirectory("${SRC_DIR}/Matcher")
add_subdirectory("${SRC_DIR}/FwUpdate")

if(RSID_PREVIEW)
    add_subdirectory("${SRC_DIR}/Preview")
endif()

if(RSID_ONE2ONE)	
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/pipeline.cmake")	
	setup_rsid_pipeline(${LIBRSID_CPP_TARGET})
	target_compile_definitions(${LIBRSID_CPP_TARGET} PRIVATE RSID_ONE2ONE)
endif()

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${HEADERS} ${SOURCES})

target_include_directories(${LIBRSID_CPP_TARGET} 
    PRIVATE 
        "${SRC_DIR}"
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)


target_compile_definitions(${LIBRSID_CPP_TARGET}
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        $<$<BOOL:${RSID_DEBUG_VALUES}>:RSID_DEBUG_VALUES>
        $<$<BOOL:${RSID_DEBUG_PACKETS}>:RSID_DEBUG_PACKETS>
    PUBLIC
        $<$<BOOL:${RSID_SECURE}>:RSID_SECURE>
)

set_target_properties(${LIBRSID_CPP_TARGET} PROPERTIES DEBUG_POSTFIX ${RSID_DEBUG_POSTFIX})
set_common_compile_opts(${LIBRSID_CPP_TARGET})

# set ide source group
get_target_property(PROJECT_SOURCES ${LIBRSID_CPP_TARGET} SOURCES)
source_group(TREE "${SRC_DIR}" FILES ${PROJECT_SOURCES})

# show api folder in ide
file(GLOB API_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/../include/RealSenseID/*.h")
add_custom_target("rsid-api" SOURCES ${API_HEADERS})
