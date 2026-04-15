set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEADERS     
    "${SRC_DIR}/Utilities.h"
    "${SRC_DIR}/Cmds.h"
    "${SRC_DIR}/ModuleInfo.h"
    "${SRC_DIR}/FwUpdaterCommF50x.h"
    "${SRC_DIR}/FwUpdateEngineF50x.h"
	"${SRC_DIR}/FwUpdaterF50x.h"
)

set(SOURCES 
    "${SRC_DIR}/Utilities.cc"
    "${SRC_DIR}/Cmds.cc"
    "${SRC_DIR}/FwUpdaterCommF50x.cc"
    "${SRC_DIR}/FwUpdateEngineF50x.cc"
	"${SRC_DIR}/FwUpdaterF50x.cc"
)

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${HEADERS} ${SOURCES})
