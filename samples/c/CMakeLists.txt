cmake_minimum_required(VERSION 3.14)
project(LibRealSenseID_C_Samples C)

if(NOT RSID_SECURE)
    # authenticate
    add_executable(authenticate-c-sample authenticate.c)
    target_link_libraries(authenticate-c-sample PRIVATE rsid_c)
    set_target_properties(authenticate-c-sample PROPERTIES FOLDER "samples/c")

    # enroll
    add_executable(enroll-c-sample enroll.c)
    target_link_libraries(enroll-c-sample PRIVATE rsid_c)
    set_target_properties(enroll-c-sample PROPERTIES FOLDER "samples/c")
endif()

if (RSID_PREVIEW)
    #preview
    add_executable(preview-c-sample preview.c)
    target_link_libraries(preview-c-sample PRIVATE rsid_c)
    set_target_properties(preview-c-sample PROPERTIES FOLDER "samples/c")
endif()
