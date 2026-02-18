set(CLIENT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/network_client.cpp
)

add_executable(yellowcore_client ${CLIENT_SOURCES})

# Qt dependencies (когда добавим)
# target_link_libraries(yellowcore_client Qt6::Core Qt6::Gui Qt6::Widgets)

# Tests
add_subdirectory(tests)
