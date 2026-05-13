# onnxruntime.cmake — Download and import ONNX Runtime as a prebuilt shared library.
#
# What this does:
#   - Downloads the correct platform-specific ORT release archive (zip/tgz)
#   - Creates an IMPORTED SHARED library target called "onnxruntime"
#   - Provides copy_onnxruntime_to_target() to copy the runtime DLL/SO next to a built exe
#
# Usage in other CMakeLists.txt:
#   target_link_libraries(my_target PRIVATE onnxruntime)
#   copy_onnxruntime_to_target(my_target)

include(FetchContent)

# ORT packages are prebuilt binaries with no CMakeLists.txt, so we use
# FetchContent_Populate (manual mode) instead of FetchContent_MakeAvailable.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(ORT_VERSION "1.24.3")

# Select platform-specific package.
# Each entry sets the archive URL and its SHA hash for integrity verification.
if(WIN32)
    set(ORT_PACKAGE "onnxruntime-win-x64-${ORT_VERSION}")
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PACKAGE}.zip")
    set(ORT_HASH "4fbfb85d0e9de9bb6fb8a9866a7cb477cbad404d889b236931bf3f5d547e5f48")
    set(ORT_HASH_TYPE "SHA256")
elseif(ANDROID)
    set(ORT_PACKAGE "onnxruntime-android-${ORT_VERSION}")
    set(ORT_URL "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/${ORT_VERSION}/${ORT_PACKAGE}.aar")
    set(ORT_HASH "e17cad728482733e3787abaf2a0bbe1b8122ff8a")
    set(ORT_HASH_TYPE "SHA1")

    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(ORT_ANDROID_ABI_DIR "arm64-v8a")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
        set(ORT_ANDROID_ABI_DIR "armeabi-v7a")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(ORT_ANDROID_ABI_DIR "x86_64")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
        set(ORT_ANDROID_ABI_DIR "x86")
    else()
        message(FATAL_ERROR "Unsupported Android ABI: ${CMAKE_ANDROID_ARCH_ABI}")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(ORT_PACKAGE "onnxruntime-linux-aarch64-${ORT_VERSION}")
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PACKAGE}.tgz")
    set(ORT_HASH "15100fb88b4c692cdd6bf2cca5f4a26a3806cebca8136de6681e2aba4b2ea033")
    set(ORT_HASH_TYPE "SHA256")
else()
    set(ORT_PACKAGE "onnxruntime-linux-x64-${ORT_VERSION}")
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PACKAGE}.tgz")
    set(ORT_HASH "4c436a280d650f8bf32c921a2bf4de7c42cc32884c51c90e47de991708bbb5a4")
    set(ORT_HASH_TYPE "SHA256")
endif()

message(STATUS "ONNX Runtime: ${ORT_PACKAGE}")
message(STATUS "ONNX Runtime URL: ${ORT_URL}")

# Download and extract the archive. The result is cached in the build directory
# so subsequent configures skip the download.
FetchContent_Declare(onnxruntime
    URL ${ORT_URL}
    URL_HASH ${ORT_HASH_TYPE}=${ORT_HASH}
)

FetchContent_GetProperties(onnxruntime)
if(NOT onnxruntime_POPULATED)
    FetchContent_Populate(onnxruntime)
    message(STATUS "ONNX Runtime extracted to: ${onnxruntime_SOURCE_DIR}")
endif()

# Create an IMPORTED SHARED target so consumers can simply link against "onnxruntime".
# This sets up include paths and library locations without building anything.
add_library(onnxruntime SHARED IMPORTED GLOBAL)

set(ORT_INCLUDE_DIR "${onnxruntime_SOURCE_DIR}/include")
set(ORT_LIB_DIR "${onnxruntime_SOURCE_DIR}/lib")

if(ANDROID)
    set(ORT_INCLUDE_DIR "${onnxruntime_SOURCE_DIR}/headers")
    set(ORT_LIB_DIR "${onnxruntime_SOURCE_DIR}/jni/${ORT_ANDROID_ABI_DIR}")
endif()

# Expose ORT headers to any target that links against onnxruntime.
set_target_properties(onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE_DIR}"
)

# Set the import library (.lib) and runtime DLL/SO paths per platform.
if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ORT_LIB_DIR}/onnxruntime.lib"
        IMPORTED_LOCATION "${ORT_LIB_DIR}/onnxruntime.dll"
    )
elseif(ANDROID)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_LIB_DIR}/libonnxruntime.so"
        IMPORTED_SONAME "libonnxruntime.so"
    )
else()
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_LIB_DIR}/libonnxruntime.so.${ORT_VERSION}"
        IMPORTED_SONAME "libonnxruntime.so.${ORT_VERSION}"
    )
endif()

# Copy the ORT runtime library next to the target's output (post-build).
# Windows: DLL must be next to the exe. Linux: .so next to librsid.so.
function(copy_onnxruntime_to_target TARGET_NAME)
    if(WIN32)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ORT_LIB_DIR}/onnxruntime.dll"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>"
            COMMENT "Copying onnxruntime.dll"
        )
    elseif(NOT ANDROID)
        # Copy the versioned .so and create symlinks matching the SONAME chain:
        #   libonnxruntime.so -> libonnxruntime.so.1 -> libonnxruntime.so.1.24.3
        string(REGEX MATCH "^[0-9]+" ORT_VERSION_MAJOR "${ORT_VERSION}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ORT_LIB_DIR}/libonnxruntime.so.${ORT_VERSION}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/libonnxruntime.so.${ORT_VERSION}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "libonnxruntime.so.${ORT_VERSION}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/libonnxruntime.so.${ORT_VERSION_MAJOR}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "libonnxruntime.so.${ORT_VERSION_MAJOR}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/libonnxruntime.so"
            COMMENT "Copying libonnxruntime.so"
        )
    endif()
endfunction()
