cmake_minimum_required(VERSION 3.14)

project(RealSenseID_Firmware_Update_Cli_Tool CXX)

set(EXE_NAME rsid-fw-update)

add_executable(${EXE_NAME} main.cc)

target_include_directories(${EXE_NAME} PRIVATE "${CMAKE_BINARY_DIR}/GeneratedHeaders")

target_link_libraries(${EXE_NAME} PRIVATE rsid)

# set debugger cwd to the exe folder (msvc only)
set_property(TARGET ${EXE_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${EXE_NAME}>")

set_target_properties(${EXE_NAME} 
	PROPERTIES FOLDER "tools"
	COMPILE_DEFINITIONS $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

set_common_compile_opts(${EXE_NAME})
