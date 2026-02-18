add_executable(client_tests
    client_tests.cpp
)

# Когда добавим Qt Test
# target_link_libraries(client_tests Qt6::Test)

add_test(NAME ClientTests COMMAND client_tests)
