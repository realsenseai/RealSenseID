if(TARGET spdlog::spdlog)
    message(STATUS "spdlog already found, skipping FetchContent")
    if(NOT TARGET spdlog::spdlog_header_only)
        add_library(spdlog_header_only_host_compat INTERFACE)
        target_link_libraries(spdlog_header_only_host_compat INTERFACE spdlog::spdlog)
        add_library(spdlog::spdlog_header_only ALIAS spdlog_header_only_host_compat)
    endif()
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    spdlog
    URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.zip
    URL_HASH SHA256=b11912a82d149792fef33fabd0503b13d54aeac25c1464755461d4108ea71fc2
    EXCLUDE_FROM_ALL
)

set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)

target_compile_definitions(spdlog_header_only INTERFACE
    FMT_UNICODE=0
    $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING>
)
