
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})


target_compile_definitions(${LIBRSID_CPP_TARGET} PRIVATE RSID_PREVIEW)
if(RSID_LIBUVC)
    target_compile_definitions(${LIBRSID_CPP_TARGET} PRIVATE RSID_LIBUVC)
endif()

list(APPEND HEADERS "${SRC_DIR}/PreviewImpl.h")
list(APPEND SOURCES 
	"${SRC_DIR}/PreviewApi.cc"
	"${SRC_DIR}/PreviewImpl.cc"
)
	
target_sources(${LIBRSID_CPP_TARGET} PRIVATE ${SOURCES})

# use libjpeg-turbo
if(NOT WIN32)
	add_dependencies(${LIBRSID_CPP_TARGET} libjpeg-turbo)
	message(STATUS "libjpeg turbo dir: ${LIBJPEG_TURBO_INSTALL_DIR}")
	target_include_directories(${LIBRSID_CPP_TARGET} PRIVATE "${LIBJPEG_TURBO_INSTALL_DIR}/include")
	if(ANDROID)
		target_link_directories(${LIBRSID_CPP_TARGET} PRIVATE "${LIBJPEG_TURBO_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}")
		target_include_directories(${LIBRSID_CPP_TARGET} PRIVATE "${LIBUSB_SRC_DIR}/libusb")
		target_link_libraries(${LIBRSID_CPP_TARGET} PRIVATE LibUVC::UVC "${LIBJPEG_TURBO_INSTALL_DIR}/lib/libjpeg.a")
	elseif(NOT RSID_LIBUVC)
		target_link_libraries(${LIBRSID_CPP_TARGET} PRIVATE JPEG_TURBO_LIBRARY)
	else()
		target_link_libraries(${LIBRSID_CPP_TARGET} PRIVATE LibUVC::UVC JPEG_TURBO_LIBRARY)
	endif ()
endif()

add_subdirectory("${SRC_DIR}/Capture")

