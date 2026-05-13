cmake_minimum_required(VERSION 3.14)

project(RealSenseID_Tests CXX)

include(${CMAKE_SOURCE_DIR}/cmake/catch2.cmake)

set(EXE_NAME rsid-utests)
add_executable(${EXE_NAME}
    main.cc
    FaceAuthenticatorTests.cc
    DetectionApiTests.cc
    PipelineTests.cc
    ${CMAKE_SOURCE_DIR}/src/Pipeline/FaceSelector.cc
    FwUpdateF50xTests.cc
    FwUpdateF45xTests.cc
    ${CMAKE_SOURCE_DIR}/src/FwUpdate/Common/Common.cc
    ${CMAKE_SOURCE_DIR}/src/FwUpdate/F50x/Utilities.cc
    ${CMAKE_SOURCE_DIR}/src/FwUpdate/F45x/Utilities.cc
    ${CMAKE_SOURCE_DIR}/src/Logger/Logger.cc
)

target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/FwUpdate ${CMAKE_SOURCE_DIR}/src/Logger ${CMAKE_SOURCE_DIR}/src/Pipeline)
target_link_libraries(${EXE_NAME} PRIVATE rsid Catch2::Catch2 spdlog::spdlog_header_only)


