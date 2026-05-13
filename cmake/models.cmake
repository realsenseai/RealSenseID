# Copy the ONNX model next to a target's output file (post-build).
# The model is loaded at runtime relative to the shared library via dladdr.
function(copy_models_to_target TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/models/fd_ret_v19.onnx"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/fd_ret_v19.onnx"
        COMMENT "Copying fd_ret_v19.onnx"
    )
endfunction()
