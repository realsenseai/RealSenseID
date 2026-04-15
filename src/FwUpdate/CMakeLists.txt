set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES       
    "${SRC_DIR}/IFwUpdater.h"
    "${SRC_DIR}/FwUpdaterApi.cc"
)

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${SOURCES})

add_subdirectory(Common)
add_subdirectory(F45x)
add_subdirectory(F50x)
