cmake_minimum_required(VERSION 3.20)
project(BacktestingEngine VERSION 1.0.0 LANGUAGES CXX)
enable_testing()

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use dynamic libraries for deployment to avoid complex dependency issues
# Static linking causes too many missing dependency issues with curl
set(CMAKE_FIND_LIBRARY_SUFFIXES .so .dylib ${CMAKE_FIND_LIBRARY_SUFFIXES})

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Eigen3 REQUIRED)

# Set Homebrew paths for macOS
if(APPLE)
    set(HOMEBREW_PREFIX "/opt/homebrew")
    set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}" ${CMAKE_PREFIX_PATH})
    set(CMAKE_LIBRARY_PATH "${HOMEBREW_PREFIX}/lib" ${CMAKE_LIBRARY_PATH})
    set(CMAKE_INCLUDE_PATH "${HOMEBREW_PREFIX}/include" ${CMAKE_INCLUDE_PATH})
    message(STATUS "Using Homebrew prefix: ${HOMEBREW_PREFIX}")
endif()

# Try to find spdlog with dynamic linking preference
find_package(spdlog REQUIRED CONFIG)
if(spdlog_FOUND)
    message(STATUS "Found spdlog: ${spdlog_DIR}")
else()
    # Fallback: try to find spdlog as a regular package
    find_package(spdlog REQUIRED)
endif()

# Force dynamic curl linking to avoid static dependency issues
pkg_check_modules(CURL REQUIRED libcurl)
# Ensure we use shared libraries
set(CURL_LIBRARIES ${CURL_LINK_LIBRARIES})
message(STATUS "CURL_LIBRARIES: ${CURL_LIBRARIES}")
message(STATUS "CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
message(STATUS "CURL_LIBRARY_DIRS: ${CURL_LIBRARY_DIRS}")

# Find yaml-cpp
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
link_directories(${YAML_CPP_LIBRARY_DIRS})
link_directories(${CURL_LIBRARY_DIRS})

# Find nlohmann/json
find_package(nlohmann_json REQUIRED CONFIG)

# Find TA-Lib with better detection
find_library(TALIB_LIBRARY NAMES ta_lib ta_lib_csr ta_common ta_abstract)
find_path(TALIB_INCLUDE_DIR ta_libc.h
    PATHS
    /usr/include
    /usr/local/include
    /opt/local/include
    /sw/include
)

# If TA-Lib is not found, try to find it in standard locations
if(NOT TALIB_LIBRARY)
    find_library(TALIB_LIBRARY NAMES ta_lib PATHS /usr/lib /usr/local/lib /opt/local/lib /sw/lib)
endif()

if(NOT TALIB_INCLUDE_DIR)
    find_path(TALIB_INCLUDE_DIR ta_libc.h PATHS /usr/include /usr/local/include /opt/local/include /sw/include)
endif()

# Check if TA-Lib is available
if(TALIB_LIBRARY AND TALIB_INCLUDE_DIR)
    message(STATUS "TA-Lib found: ${TALIB_LIBRARY}")
    message(STATUS "TA-Lib include dir: ${TALIB_INCLUDE_DIR}")
    set(HAS_TALIB TRUE)
else()
    message(WARNING "TA-Lib not found. Some technical indicators may not be available.")
    message(WARNING "TALIB_LIBRARY: ${TALIB_LIBRARY}")
    message(WARNING "TALIB_INCLUDE_DIR: ${TALIB_INCLUDE_DIR}")
    set(HAS_TALIB FALSE)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${YAML_CPP_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})
if(TALIB_INCLUDE_DIR)
    include_directories(${TALIB_INCLUDE_DIR})
endif()

# Library source files
set(LIB_SOURCES
    src/context/regime_classifier.cpp
    src/engine/backtest_engine.cpp
    src/strategy/strategy.cpp
    src/strategy/strategy_factory.cpp
    src/strategy/moving_average_strategy.cpp
    src/strategy/rsi_strategy.cpp
    src/strategy/macd_strategy.cpp
    src/strategy/bollinger_bands_strategy.cpp
    src/strategy/multi_leg_strategy.cpp
    src/strategy/advanced_strategies.cpp
    src/strategy/natural_language_parser.cpp
    src/strategy/code_generator.cpp
    src/data/data_handler_factory.cpp
    src/data/csv_data_handler.cpp
    src/data/api_data_handler.cpp
    src/data/alpha_vantage_handler.cpp
    src/data/yfinance_handler.cpp
    src/data/iex_handler.cpp
    src/data/polygon_handler.cpp
    src/data/data_source_router.cpp
    src/data/data_validator.cpp
    src/data/stock_symbols.cpp
    src/data/technical_indicators.cpp
    src/data/symbol_cache.cpp
    src/portfolio/portfolio_manager.cpp
    src/portfolio/multi_asset_portfolio.cpp
    src/execution/execution_handler.cpp
    src/risk/risk_manager.cpp
    src/risk/advanced_risk_manager.cpp
    src/performance/performance_analyzer.cpp
    src/cli/cli.cpp
)

# Header files
file(GLOB_RECURSE HEADERS "include/*.h")

# Create the main executable
add_executable(backtest_engine 
    src/main.cpp 
    ${LIB_SOURCES} 
    ${HEADERS}
)

# Link libraries
target_link_libraries(backtest_engine 
    PRIVATE 
    Eigen3::Eigen 
    spdlog::spdlog 
    ${CURL_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

# Link TA-Lib if available
if(HAS_TALIB)
    target_link_libraries(backtest_engine PRIVATE ${TALIB_LIBRARY})
    target_compile_definitions(backtest_engine PRIVATE HAS_TALIB)
endif()

# Compiler flags
target_compile_options(backtest_engine PRIVATE 
    -Wall -Wextra -O3 -march=native -fPIC
)

# Set runtime path for shared libraries
set_target_properties(backtest_engine PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# HTTP Server executable for deployment
add_executable(backtest_server
    src/server.cpp
    ${LIB_SOURCES}
)

target_link_libraries(backtest_server 
    PRIVATE 
    Eigen3::Eigen 
    spdlog::spdlog 
    ${CURL_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

if(HAS_TALIB)
    target_link_libraries(backtest_server PRIVATE ${TALIB_LIBRARY})
    target_compile_definitions(backtest_server PRIVATE HAS_TALIB)
endif()

# Compiler flags for server
target_compile_options(backtest_server PRIVATE 
    -Wall -Wextra -O3 -march=native -fPIC
)

# Set runtime path for shared libraries
set_target_properties(backtest_server PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

add_executable(test_csv_date_filter
    tests/csv_data_handler_date_filter_test.cpp
    ${LIB_SOURCES}
)

add_executable(test_risk_exposure_units
    tests/risk_exposure_units_test.cpp
    ${LIB_SOURCES}
)

target_link_libraries(test_csv_date_filter
    PRIVATE
    Eigen3::Eigen
    spdlog::spdlog
    ${CURL_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

target_link_libraries(test_risk_exposure_units
    PRIVATE
    Eigen3::Eigen
    spdlog::spdlog
    ${CURL_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

add_test(NAME test_csv_date_filter COMMAND test_csv_date_filter)
add_test(NAME test_risk_exposure_units COMMAND test_risk_exposure_units)

# Install targets
install(TARGETS backtest_engine backtest_server
    RUNTIME DESTINATION bin
)

# CPack configuration
set(CPACK_PACKAGE_NAME "BacktestingEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "High-performance C++ backtesting engine with advanced analytics and machine learning capabilities")
include(CPack)
