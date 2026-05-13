# Unity test framework (vendored, MIT license — see unity/LICENSE.txt)
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)
target_compile_definitions(unity PUBLIC UNITY_OUTPUT_COLOR)
set_target_properties(unity PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "rsid_embedded")

# Test executable
add_executable(rsid_embedded_tests
    test_main.c
    test_packet.c
    test_session.c
    test_authenticator.c
    test_device_controller.c
    test_robustness.c
    test_api.c
    mock_platform.c
)

target_link_libraries(rsid_embedded_tests PRIVATE rsid_embedded unity)
target_include_directories(rsid_embedded_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(rsid_embedded_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "rsid_embedded"
)

if(MSVC)
    target_compile_definitions(rsid_embedded_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME rsid_embedded_tests COMMAND rsid_embedded_tests)
