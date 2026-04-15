set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    message(STATUS "Windows platform")

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    message(STATUS "Linux platform")
    add_definitions(-DLINUX)

elseif(ANDROID)
    message(STATUS "Android platform")
    add_definitions(-DANDROID_STL=c++_shared)
    set(CMAKE_OBJECT_PATH_MAX 2048)

else()
    message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is currently unsupported")
endif()
