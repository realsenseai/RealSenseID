cmake_minimum_required(VERSION 3.14)
project(RealSenseID_CLII CXX)

set(EXE_NAME rsid-cli)
add_executable(${EXE_NAME} main.cc)
target_link_libraries(${EXE_NAME} PRIVATE rsid)
if(RSID_SECURE)
    target_link_libraries(${EXE_NAME} PRIVATE rsid_secure_helper)
endif()
if (RSID_PREVIEW)
    target_compile_definitions(${EXE_NAME} PRIVATE RSID_PREVIEW)
endif ()

# set debugger cwd to the exe folder (msvc only)
set_property(TARGET ${EXE_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${EXE_NAME}>")


set_target_properties(${EXE_NAME} PROPERTIES FOLDER "tools")

set_common_compile_opts(${EXE_NAME})
