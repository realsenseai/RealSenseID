set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEADERS "${SRC_DIR}/Randomizer.h" "${SRC_DIR}/PacketSender.h" "${SRC_DIR}/SerialPacket.h" "${SRC_DIR}/Timer.h"
            "${SRC_DIR}/SerialConnection.h" "${SRC_DIR}/CommonTypes.h"  ${SRC_DIR}/Crc16.h "${SRC_DIR}/AuthConfigPayload.h")

set(SOURCES "${SRC_DIR}/Randomizer.cc" "${SRC_DIR}/PacketSender.cc" "${SRC_DIR}/SerialPacket.cc" "${SRC_DIR}/Timer.cc"  ${SRC_DIR}/Crc16.cc )

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux" OR APPLE)
    # LinuxSerial uses only POSIX serial APIs (termios.h / unistd.h /
    # fcntl.h) — no Linux-specific syscalls or headers — so the same
    # implementation builds and runs on macOS unchanged.
    list(APPEND HEADERS "${SRC_DIR}/LinuxSerial.h")
    list(APPEND SOURCES "${SRC_DIR}/LinuxSerial.cc")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    list(APPEND HEADERS "${SRC_DIR}/WindowsSerial.h")
    list(APPEND SOURCES "${SRC_DIR}/WindowsSerial.cc")
elseif(ANDROID)
    list(APPEND HEADERS "${SRC_DIR}/AndroidSerial.h")
    list(APPEND SOURCES "${SRC_DIR}/AndroidSerial.cc")
endif()

if(RSID_SECURE)
    list(APPEND HEADERS ${SRC_DIR}/MbedtlsWrapper.h ${SRC_DIR}/SecureSession.h)
    list(APPEND SOURCES ${SRC_DIR}/MbedtlsWrapper.cc ${SRC_DIR}/SecureSession.cc)
else()
    list(APPEND HEADERS ${SRC_DIR}/NonSecureSession.h)
    list(APPEND SOURCES ${SRC_DIR}/NonSecureSession.cc)
endif()

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${HEADERS} ${SOURCES})
target_include_directories(${LIBRSID_CPP_TARGET} PRIVATE "${SRC_DIR}")
