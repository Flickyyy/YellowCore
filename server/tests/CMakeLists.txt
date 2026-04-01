find_package(Threads REQUIRED)

add_executable(server_tests
    auth_tests.cpp
    bank_tests.cpp
    stock_tests.cpp
    concurrent_tests.cpp
)
target_link_libraries(server_tests yellowcore_server_lib gtest gtest_main Threads::Threads)
add_test(NAME ServerTests COMMAND server_tests)

add_executable(server_network_tests
    network_tests.cpp
)
target_link_libraries(server_network_tests yellowcore_transport_lib gtest gtest_main Threads::Threads)
add_test(NAME ServerNetworkTests COMMAND server_network_tests)
