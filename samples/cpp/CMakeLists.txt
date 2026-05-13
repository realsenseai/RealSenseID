cmake_minimum_required(VERSION 3.14)
project(RealSenseID_CPP_Samples CXX)

if(NOT RSID_SECURE)
    # authenticate
	add_executable(authenticate-cpp-sample authenticate.cc)
    target_link_libraries(authenticate-cpp-sample PRIVATE rsid)
    set_target_properties(authenticate-cpp-sample PROPERTIES FOLDER "samples/cpp")

    # enroll
    add_executable(enroll-cpp-sample enroll.cc)
    target_link_libraries(enroll-cpp-sample PRIVATE rsid)
    set_target_properties(enroll-cpp-sample PROPERTIES FOLDER "samples/cpp")

    # host-mode
    add_executable(host-mode-cpp-sample host-mode.cc)
    target_link_libraries(host-mode-cpp-sample PRIVATE rsid)
    set_target_properties(host-mode-cpp-sample PROPERTIES FOLDER "samples/cpp")
 
    # F500+ detect/decode samples (no secure mode dependency)
    add_executable(detect-persons-cpp-sample detect-persons.cc)
    target_link_libraries(detect-persons-cpp-sample PRIVATE rsid)
    set_target_properties(detect-persons-cpp-sample PROPERTIES FOLDER "samples/cpp")

    add_executable(detect-poses-cpp-sample detect-poses.cc)
    target_link_libraries(detect-poses-cpp-sample PRIVATE rsid)
    set_target_properties(detect-poses-cpp-sample PROPERTIES FOLDER "samples/cpp")
	
    add_executable(detect-body-parts-cpp-sample detect-body-parts.cc)
    target_link_libraries(detect-body-parts-cpp-sample PRIVATE rsid)
    set_target_properties(detect-body-parts-cpp-sample PROPERTIES FOLDER "samples/cpp")
    
else() # secure mode samples
    add_executable(pair-device-cpp-sample pair-device.cc secure_mode_helper.cc secure_mode_helper.h)
    target_link_libraries(pair-device-cpp-sample PRIVATE rsid mbedtls)
    set_target_properties(pair-device-cpp-sample PROPERTIES FOLDER "samples/cpp")
endif()


if (RSID_PREVIEW)
    # simple preview
    add_executable(preview-cpp-sample preview.cc)
    target_link_libraries(preview-cpp-sample PRIVATE rsid)
    set_target_properties(preview-cpp-sample PROPERTIES FOLDER "samples/cpp")

    if(NOT RSID_SECURE)
        # detect face from preview
        add_executable(host-detect-face-cpp-sample host-detect-face.cc)
        target_link_libraries(host-detect-face-cpp-sample PRIVATE rsid)
        set_target_properties(host-detect-face-cpp-sample PROPERTIES
            FOLDER "samples/cpp"
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:host-detect-face-cpp-sample>"
        )
        
        # multi-camera
        add_executable(multi-camera-cpp-sample multi-camera.cc)
        target_link_libraries(multi-camera-cpp-sample PRIVATE rsid)
        set_target_properties(multi-camera-cpp-sample PROPERTIES FOLDER "samples/cpp")

        # snapshot preview
        add_executable(preview-snapshot-cpp-sample preview.cc)
        target_link_libraries(preview-snapshot-cpp-sample PRIVATE rsid)
        set_target_properties(preview-snapshot-cpp-sample PROPERTIES FOLDER "samples/cpp")
    else()
        # snapshot preview
        add_executable(preview-snapshot-cpp-sample preview_snapshot.cc secure_mode_helper.cc secure_mode_helper.h)
        target_link_libraries(preview-snapshot-cpp-sample PRIVATE rsid mbedtls)
        set_target_properties(preview-snapshot-cpp-sample PROPERTIES FOLDER "samples/cpp")
    endif()
endif()