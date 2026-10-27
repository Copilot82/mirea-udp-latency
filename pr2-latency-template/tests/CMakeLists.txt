if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
find_package(Catch2 3.5.2 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.5.2
        URL_HASH SHA256=269543a49eb76f40b3f93ff231d4c24c27a7e16c90e47d2e45bcc564de470c6e)
    FetchContent_MakeAvailable(catch2)
endif()
add_executable(unit_tests test_protocol.cpp test_telemetry.cpp test_reliable_channel.cpp)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain protocol telemetry reliability)
add_test(NAME unit_tests COMMAND unit_tests)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
add_test(NAME udp_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.py
    --server $<TARGET_FILE:server> --client $<TARGET_FILE:client>)
set_tests_properties(udp_integration PROPERTIES TIMEOUT 40)
add_test(NAME analysis_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_analysis.py)

set_tests_properties(unit_tests analysis_tests PROPERTIES TIMEOUT 30)
add_test(NAME launcher_demo COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_launcher.py
    --build-dir $<TARGET_FILE_DIR:client>)
set_tests_properties(launcher_demo PROPERTIES TIMEOUT 20)
add_test(NAME reliability_udp COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reliability_udp.py
    --server $<TARGET_FILE:reliable_server> --client $<TARGET_FILE:reliable_client>)
set_tests_properties(reliability_udp PROPERTIES TIMEOUT 40)
add_test(NAME reliability_analysis COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reliability_analysis.py)
set_tests_properties(reliability_analysis PROPERTIES TIMEOUT 10)
