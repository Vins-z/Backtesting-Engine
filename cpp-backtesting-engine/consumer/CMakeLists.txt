cmake_minimum_required(VERSION 3.20)
project(backtesting_engine_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(BacktestingEngine CONFIG REQUIRED)

add_executable(consumer_smoke main.cpp)
target_link_libraries(consumer_smoke PRIVATE BacktestingEngine::backtesting_engine_shared)

