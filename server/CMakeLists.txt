set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/account.cpp
    src/user_manager.cpp
    src/transaction.cpp
)

add_executable(yellowcore_server ${SERVER_SOURCES})

# Dependencies (когда добавим)
# target_link_libraries(yellowcore_server pthread)

# Tests
add_subdirectory(tests)
