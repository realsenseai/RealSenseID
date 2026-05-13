
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEADERS "${SRC_DIR}/DiscoverDevicesCommon.h")

if(UNIX OR ANDROID)
	list(APPEND SOURCES "${SRC_DIR}/DiscoverDevices_Linux.cc")
	if(RSID_PREVIEW)
		if(NOT RSID_LIBUVC)
			list(APPEND SOURCES "${SRC_DIR}/DiscoverCapture_V4L2.cc")
		else()
			list(APPEND SOURCES "${SRC_DIR}/DiscoverCapture_LibUVC.cc")
		endif()
	endif()
elseif(WIN32)
	list(APPEND SOURCES "${SRC_DIR}/DiscoverDevices_Win32.cc")
endif()

target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${SOURCES} ${HEADERS})
