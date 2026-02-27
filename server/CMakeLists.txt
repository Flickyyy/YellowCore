add_library(yellowcore_server_lib
    src/auth_service.cpp
    src/bank_service.cpp
    src/stock_service.cpp
    src/price_engine.cpp
)
target_include_directories(yellowcore_server_lib PUBLIC src)

add_subdirectory(tests)
