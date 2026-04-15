include(ExternalProject)

set(LIBJPEG_TURBO_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo-build/${CMAKE_BUILD_TYPE}")

if (ANDROID)
    list(APPEND EXTRA_CMAKE_ARGS
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DANDROID_ABI=${ANDROID_ABI}"
            "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
            "-DCMAKE_ANDROID_ARCH_ABI=${CMAKE_ANDROID_ARCH_ABI}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
            "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}"
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    )

    ExternalProject_Add(
            libjpeg-turbo
            PREFIX _deps/libjpeg-turbo
            URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.0/libjpeg-turbo-3.1.0.tar.gz
            URL_HASH SHA256=9564c72b1dfd1d6fe6274c5f95a8d989b59854575d4bbee44ade7bc17aa9bc93
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release --parallel
            EXCLUDE_FROM_ALL
            CMAKE_ARGS
            ${EXTRA_CMAKE_ARGS}
            -DENABLE_SHARED=OFF
            -DWITH_TURBOJPEG=OFF
            -DWITH_CRT_DLL=ON
            -DWITH_SIMD=ON
            -DWITH_JPEG8=ON
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_INSTALL_PREFIX=${LIBJPEG_TURBO_INSTALL_DIR}
            BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libjpeg.a _deps/libjpeg-turbo-build/${CMAKE_BUILD_TYPE}/lib/libjpeg.a
    )

    add_library(JPEG_TURBO_LIBRARY STATIC IMPORTED GLOBAL)
    set_property(TARGET JPEG_TURBO_LIBRARY PROPERTY IMPORTED_LOCATION "${LIBJPEG_TURBO_INSTALL_DIR}/lib/libjpeg.a")
    add_dependencies(JPEG_TURBO_LIBRARY libjpeg-turbo)

else ()
    ExternalProject_Add(
            libjpeg-turbo
            PREFIX _deps/libjpeg-turbo
            URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.0/libjpeg-turbo-3.1.0.tar.gz
            URL_HASH SHA256=9564c72b1dfd1d6fe6274c5f95a8d989b59854575d4bbee44ade7bc17aa9bc93
            BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release --parallel
            EXCLUDE_FROM_ALL
            CMAKE_ARGS
            -DENABLE_SHARED=OFF
            -DWITH_TURBOJPEG=OFF
            -DWITH_CRT_DLL=ON
            -DWITH_SIMD=ON
            -DWITH_JPEG8=ON
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_INSTALL_PREFIX=${LIBJPEG_TURBO_INSTALL_DIR}
            BUILD_BYPRODUCTS ${LIBJPEG_TURBO_INSTALL_DIR}/lib/libjpeg.a
    )
    add_library(JPEG_TURBO_LIBRARY STATIC IMPORTED GLOBAL)
    set_property(TARGET JPEG_TURBO_LIBRARY PROPERTY IMPORTED_LOCATION "${LIBJPEG_TURBO_INSTALL_DIR}/lib/libjpeg.a")
    add_dependencies(JPEG_TURBO_LIBRARY libjpeg-turbo)
endif ()


set_target_properties(libjpeg-turbo PROPERTIES FOLDER "external/libjpeg-turbo")
