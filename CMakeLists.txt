cmake_minimum_required(VERSION 3.20)
project(YellowCore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Directories
add_subdirectory(server)
add_subdirectory(client)

# Testing
enable_testing()
