set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(HEADERS
	"${SRC_DIR}/Impl/IFaceAuthenticator.h"
    "${SRC_DIR}/Impl/FaceAuthenticatorCommon.h"
	"${SRC_DIR}/Impl/FaceAuthenticatorF45x.h"
	"${SRC_DIR}/Impl/FaceAuthenticatorF50x.h"
	"${SRC_DIR}/Impl/DownloadImage.h"
)
set(SOURCES
    "${SRC_DIR}/FaceAuthenticatorApi.cc"
    "${SRC_DIR}/Impl/FaceAuthenticatorCommon.cc"
	"${SRC_DIR}/Impl/FaceAuthenticatorF45x.cc"
	"${SRC_DIR}/Impl/FaceAuthenticatorF50x.cc"
	"${SRC_DIR}/Impl/DownloadImage.cc"
)


target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${HEADERS} ${SOURCES})
