if(NOT ANDROID)
    add_subdirectory(c)
endif()

if(WIN32)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        add_subdirectory(csharp)
    else()
        message(WARNING "C# wrapper will be skipped: not supported with Ninja generator")
    endif()
endif()

if(ANDROID)
    add_subdirectory("android/realsenseid/src")
endif()

if(RSID_PY)
    add_subdirectory(python)
endif()
