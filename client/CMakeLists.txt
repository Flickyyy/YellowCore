cmake_minimum_required(VERSION 3.20)
project(YellowCoreClient CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)

add_executable(yellowcore_client
    src/main.cpp
    src/TcpClient.cpp
    src/NetworkWorker.cpp
    src/LoginWindow.cpp
    src/MainWindow.cpp
)

target_link_libraries(yellowcore_client PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

add_subdirectory(tests)
