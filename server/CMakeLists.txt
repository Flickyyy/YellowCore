find_package(Threads REQUIRED)

add_library(yellowcore_server_lib
    src/auth_service.cpp
    src/bank_service.cpp
    src/stock_service.cpp
    src/price_engine.cpp
)
target_include_directories(yellowcore_server_lib PUBLIC src)

add_library(yellowcore_transport_lib
    src/command_dispatcher.cpp
    src/tcp_session.cpp
    src/tcp_server.cpp
)
target_include_directories(yellowcore_transport_lib PUBLIC src ../shared)
target_link_libraries(yellowcore_transport_lib
    PUBLIC yellowcore_server_lib
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC Boost::system
)

add_executable(yellowcore_server
    src/server_main.cpp
)
target_link_libraries(yellowcore_server
    PRIVATE yellowcore_transport_lib
    PRIVATE Threads::Threads
)

add_subdirectory(tests)