set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCES
    "${SRC_DIR}/FaceBox.h"
    "${SRC_DIR}/Inference.h"
    "${SRC_DIR}/Inference.cc"
    "${SRC_DIR}/Pipeline.h"
    "${SRC_DIR}/Pipeline.cc"
	"${SRC_DIR}/Preprocessing.h"
    "${SRC_DIR}/Preprocessing.cc"
    "${SRC_DIR}/Utils.h"
    "${SRC_DIR}/Utils.cc"
    "${SRC_DIR}/FaceSelector.h"
    "${SRC_DIR}/FaceSelector.cc"

)

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${SOURCES})
target_include_directories(${LIBRSID_CPP_TARGET} PRIVATE "${SRC_DIR}")
target_link_libraries(${LIBRSID_CPP_TARGET} PRIVATE onnxruntime stb ${CMAKE_DL_LIBS})
if(ANDROID)
    target_link_libraries(${LIBRSID_CPP_TARGET} PRIVATE android)
endif()

