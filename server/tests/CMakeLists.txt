add_executable(server_tests
    transaction_tests.cpp
    account_tests.cpp
)

# Когда добавим GoogleTest / Boost.Test
# target_link_libraries(server_tests gtest gtest_main)

add_test(NAME ServerTests COMMAND server_tests)
