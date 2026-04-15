# =================================================================================
# setup_rsid_pipeline(TARGET_NAME)
#
# Configures the RealSenseID pipeline for the given target by:
# - Setting the correct include and library directories for Debug/Release.
# - Linking the appropriate library (`rsid-pipeline.lib` or `rsid-pipeline_debug.lib`).
# - Copying runtime files from `bin/debug` or `bin/release` to the target binary directory.
# - Copying all model files from `models/` to the target binary directory.
# - Allowing users to override RSID_PIPELINE_DIR using `-DRSID_PIPELINE_DIR=...`.
#
# Expected RSID_PIPELINE_DIR Structure:
#
# RSID_PIPELINE_DIR/
# ├── include/              # Header files (common)
# ├── bin/
# │   ├── Debug/            rsid-pipeline_debug.dll
# │   ├── Release/          rsid-pipeline.dll
# ├── lib/
# │   ├── Debug/           
# │   │       ├── rsid-pipeline_debug.lib
# │   │       ├── 
# │   ├── Release/          # Release libraries
# │           ├── rsid-pipeline.lib
# =================================================================================

function(setup_rsid_pipeline TARGET_NAME)
    set(RSID_PIPELINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../HostPipeline/build/package" CACHE FILEPATH "Path to RSID pipeline package")
    file(REAL_PATH "${RSID_PIPELINE_DIR}" RSID_PIPELINE_DIR)

    set(BUILD_TYPE "$<CONFIG>")

    # Adjust paths according to folder structure
    set(INCLUDE_DIR "${RSID_PIPELINE_DIR}/include")
    set(LIB_DIR "${RSID_PIPELINE_DIR}/lib/${BUILD_TYPE}")
    set(BIN_DIR "${RSID_PIPELINE_DIR}/bin/${BUILD_TYPE}")
    set(MODELS_DIR "${RSID_PIPELINE_DIR}/models")
    set(TARGET_BIN_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>")

    message(STATUS "[${TARGET_NAME}] Pipeline location: ${RSID_PIPELINE_DIR}")

    target_compile_definitions(${TARGET_NAME} PRIVATE RSID_ONE2ONE)
    target_include_directories(${TARGET_NAME} PRIVATE "${INCLUDE_DIR}")
    target_link_libraries(${TARGET_NAME} PRIVATE "${LIB_DIR}/rsid-pipeline$<IF:$<CONFIG:Debug>,_debug,>.lib")

    if(WIN32)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${BIN_DIR}" "${TARGET_BIN_DIR}"
            COMMENT "Copying runtime files from ${BIN_DIR} to ${TARGET_BIN_DIR}")       
    endif()
endfunction()
