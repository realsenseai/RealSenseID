# stb.cmake — Vendored stb headers from https://github.com/nothings/stb (public domain / MIT)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${CMAKE_SOURCE_DIR}/3rdparty/stb")
