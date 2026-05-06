find_package(Qt6 OPTIONAL_COMPONENTS Test QUIET)

if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6::Test not found — skipping client tests")
    return()
endif()

add_executable(yellowcore_client_tests
    test_tcpclient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TcpClient.cpp
)

target_include_directories(yellowcore_client_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(yellowcore_client_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME ClientTests COMMAND yellowcore_client_tests)
