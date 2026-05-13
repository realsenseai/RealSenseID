cmake_minimum_required(VERSION 3.14)
include(CSharpUtilities)
project(LibRealSenseID_CSharp LANGUAGES CSharp)

set(CMAKE_CSharp_FLAGS "/platform:x64")

set(LIBRSID_CSHARP_TARGET rsid_dotnet)
add_library(${LIBRSID_CSHARP_TARGET} SHARED Authenticator.cs DeviceController.cs Preview.cs Shared.cs Logging.cs FwUpdater.cs Discover.cs)

if(RSID_SECURE)
    add_definitions(-DRSID_SECURE)
endif()

set_target_properties(${LIBRSID_CSHARP_TARGET} PROPERTIES DEBUG_POSTFIX ${RSID_DEBUG_POSTFIX}
                                                          VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.8")

# set ide source group
set_target_properties(${LIBRSID_CSHARP_TARGET} PROPERTIES FOLDER "wrappers")
get_target_property(PROJECT_SOURCES ${LIBRSID_CSHARP_TARGET} SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PROJECT_SOURCES})

add_dependencies(${LIBRSID_CSHARP_TARGET} ${LIBRSID_C_TARGET})
