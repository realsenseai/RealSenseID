cmake_minimum_required(VERSION 3.14)

if(RSID_SECURE)
	add_subdirectory(shared/secure-mode-helper)
endif()

add_subdirectory(rsid-fw-update)
add_subdirectory(rsid-cli)

if(MSVC)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        add_subdirectory(rsid-viewer)
    else()
        message(WARNING "rsid-viewer will be skipped: not supported with Ninja generator")
    endif()
endif()
