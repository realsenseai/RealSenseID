cmake_minimum_required(VERSION 3.14)

if (POLICY CMP0000)
    cmake_policy(SET CMP0000 OLD)
endif()

project(RealSenseIDSwigJNI)

if (RSID_SECURE)
    set(FLAVOR "secured")
else()
    set(FLAVOR "standard")
endif()

set(SWIG_IN_FILE "swig.i.in")
set(SWIG_I_FILE ${FLAVOR}/swig.i)

set(JAVA_GEN_PACKAGE "com.realsenseai.rsid.api")
set(JAVA_GEN_PACKAGE_PATH "com/realsenseai/rsid/api")

get_filename_component(BASE_GEN_SUBDIR ${CMAKE_CURRENT_SOURCE_DIR}/${FLAVOR} ABSOLUTE)
message(STATUS "Generating package ${JAVA_GEN_PACKAGE} in ${BASE_GEN_SUBDIR}")
set(CPP_GEN_DIR "${BASE_GEN_SUBDIR}/cpp_gen")
set(JAVA_GEN_DIR "${BASE_GEN_SUBDIR}/java/${JAVA_GEN_PACKAGE_PATH}")

# Fail the build if SWIG is not found - Ensure SWIG is installed and on the system path
find_package(SWIG REQUIRED)
include(${SWIG_USE_FILE})

# Fail the build if OpenMP is not found - Ensure OpenMP is installed and on the system path
# This accelrates the Image Processing for Android
find_package(OpenMP REQUIRED CXX)

# Find Java and JNI
# find_package(Java COMPONENTS Development REQUIRED)
# find_package(JNI REQUIRED)

# Remove old generated files, in case we don't need to generate some of them anymore
file(REMOVE_RECURSE ${JAVA_GEN_DIR})
file(REMOVE_RECURSE ${CPP_GEN_DIR})
file(REMOVE ${SWIG_I_FILE})

# Preprocess the swig i file
set(RealSenseID_HEADERS_FOLDER ${CMAKE_SOURCE_DIR}/include/RealSenseID)
configure_file(${SWIG_IN_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SWIG_I_FILE})

# Ensure file recognized as C++ (otherwise, exported as C file)
set_property(SOURCE ${SWIG_I_FILE} PROPERTY CPLUSPLUS ON)

# Setup SWIG flags and locations
set(CMAKE_SWIG_FLAGS -package ${JAVA_GEN_PACKAGE} -doxygen)
set(CMAKE_SWIG_OUTDIR ${JAVA_GEN_DIR})
set(SWIG_OUTFILE_DIR ${CPP_GEN_DIR})

# Export a wrapper file to Java, and link with the created C++ library
set(ALL_SWIG_I_FILES ${SWIG_I_FILE})

if (RSID_SECURE)
##### SHOULD NOT BE USED IN PRODUCTION - Not part of the RealSenseID API - Used to create the Android app using
##### rsid_secure_helper.
    set(SIG_EXAMPLE_IN_FILE signature_example_wrapper.i.in)
    set(SIG_EXAMPLE_I_FILE ${FLAVOR}/signature_example_wrapper.i)
    file(REMOVE ${FLAVOR}/${SIG_EXAMPLE_I_FILE})
    set(RealSenseID_EXAMPLES_SHARED_FOLDER ${CMAKE_SOURCE_DIR}/tools/shared)
    configure_file(${SIG_EXAMPLE_IN_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SIG_EXAMPLE_I_FILE})
    set_property(SOURCE ${SIG_EXAMPLE_I_FILE} PROPERTY CPLUSPLUS ON)
    list(APPEND ALL_SWIG_I_FILES ${SIG_EXAMPLE_I_FILE})
    list(APPEND ALL_LINKED_LIBRARIES rsid_secure_helper)
endif()
swig_add_library(${PROJECT_NAME} TYPE MODULE LANGUAGE java SOURCES ${ALL_SWIG_I_FILES})

find_library(ANDROID_LIB android)
find_library(ANDROID_LOG log)

# Configure OpenMP for static linking on Android
target_compile_options(${PROJECT_NAME} PRIVATE "-fopenmp")
target_link_options(${PROJECT_NAME} PRIVATE "-fopenmp" "-static-openmp")

set(ALL_LINKED_LIBRARIES ${ANDROID_LIB} ${ANDROID_LOG} ${LIBRSID_CPP_TARGET})
swig_link_libraries(${PROJECT_NAME} ${ALL_LINKED_LIBRARIES})
