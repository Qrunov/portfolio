cmake_minimum_required(VERSION 3.20)
project(PortfolioManagement CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# ============================================================================
# Find Dependencies
# ============================================================================

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(SQLite3 REQUIRED)
find_package(GTest REQUIRED)

# ============================================================================
# Core Library
# ============================================================================

add_library(portfolio_core STATIC
    src/Portfolio.cpp
    src/CSVDataSource.cpp
    src/BasePortfolioStrategy.cpp
    src/PortfolioManager.cpp
)

set_target_properties(portfolio_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(portfolio_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_core PUBLIC
    ${Boost_LIBRARIES}
#   sqlite3
)

# ============================================================================
# CLI Library
# ============================================================================

add_library(portfolio_cli STATIC
    src/CommandLineParser.cpp
    src/CommandExecutor.cpp
)

target_include_directories(portfolio_cli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_cli PUBLIC
    portfolio_core
    ${Boost_LIBRARIES}
)

# ============================================================================
# Main Application
# ============================================================================

add_executable(portfolio_app src/main.cpp)

target_include_directories(portfolio_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_app PRIVATE
    portfolio_core
    portfolio_cli
    ${Boost_LIBRARIES}
    dl
)

# ============================================================================
# Plugins
# ============================================================================

# InMemory Database Plugin
add_library(inmemory_db_plugin SHARED
    plugins/database/inmemory/InMemoryDatabase.cpp
    plugins/database/inmemory/InMemoryDatabasePlugin.cpp
)

target_include_directories(inmemory_db_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(inmemory_db_plugin PRIVATE
    portfolio_core
)

set_target_properties(inmemory_db_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "inmemory_db"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/database"
    SUFFIX ".so"
)

# SQLite Database Plugin
add_library(sqlite_db_plugin SHARED
    plugins/database/sqlite/SQLiteDatabase.cpp
    plugins/database/sqlite/SQLiteDatabasePlugin.cpp
)

target_include_directories(sqlite_db_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(sqlite_db_plugin PRIVATE
    portfolio_core
    ${SQLite3_LIBRARIES}
)

set_target_properties(sqlite_db_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sqlite_db"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/database"
    SUFFIX ".so"
)

# BuyHold Strategy Plugin
add_library(buyhold_strategy_plugin SHARED
    plugins/strategy/buyhold/BuyHoldStrategy.cpp
    plugins/strategy/buyhold/BuyHoldStrategyPlugin.cpp
)

target_include_directories(buyhold_strategy_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(buyhold_strategy_plugin PRIVATE
    portfolio_core
)

set_target_properties(buyhold_strategy_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "buyhold_strategy"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/strategy"
    SUFFIX ".so"
)

# ============================================================================
# Tests
# ============================================================================

enable_testing()

# ============================================================================
# Global Test Environment
# ============================================================================

# Устанавливаем переменную окружения для поиска плагинов
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILE "${CMAKE_BINARY_DIR}/CTestEnv.cmake")

# Создаем файл для установки переменных окружения
file(WRITE "${CMAKE_BINARY_DIR}/CTestEnv.cmake"
    "set(ENV{PORTFOLIO_PLUGIN_PATH} \"${CMAKE_BINARY_DIR}/plugins\")\n"
)

# Test: Command Line Parser and Executor
add_executable(test_command_line tests/test_command_line.cpp)
target_include_directories(test_command_line PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(test_command_line PRIVATE
    portfolio_core
    portfolio_cli
    ${Boost_LIBRARIES}
    gtest
    gtest_main
)
add_test(NAME CommandLineTests COMMAND test_command_line)

# Test: CSV Data Source
add_executable(test_csv_datasource tests/test_csv_datasource.cpp)
target_include_directories(test_csv_datasource PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_csv_datasource PRIVATE
    portfolio_core
    gtest
    gtest_main
)
add_test(NAME CSVDataSourceTests COMMAND test_csv_datasource)

# Test: Portfolio
add_executable(test_portfolio tests/test_portfolio.cpp)
target_include_directories(test_portfolio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_portfolio PRIVATE
    portfolio_core
    gtest
    gtest_main
)
add_test(NAME PortfolioTests COMMAND test_portfolio)

# Test: Plugin Manager (InMemory + SQLite)
add_executable(test_plugin_manager tests/test_plugin_manager.cpp)
target_include_directories(test_plugin_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_plugin_manager PRIVATE
    portfolio_core
    gtest
    gtest_main
    dl
)
add_test(NAME PluginManagerTests COMMAND test_plugin_manager)

# Test: SQLite Database (через плагин)
add_executable(test_sqlite_database tests/test_sqlite_database.cpp)
target_include_directories(test_sqlite_database PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_sqlite_database PRIVATE
    portfolio_core
    ${SQLite3_LIBRARIES}
    gtest
    gtest_main
    dl
)
add_test(NAME SQLiteDatabaseTests COMMAND test_sqlite_database)

# Test: BuyHold Strategy
add_executable(test_buyhold_strategy tests/test_buyhold_strategy.cpp)
target_include_directories(test_buyhold_strategy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_buyhold_strategy PRIVATE
    portfolio_core
    gtest
    gtest_main
    dl
)
add_test(NAME BuyHoldStrategyTests COMMAND test_buyhold_strategy)
set_tests_properties(BuyHoldStrategyTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    ENVIRONMENT "PORTFOLIO_PLUGIN_PATH=${CMAKE_BINARY_DIR}/plugins"
)


# ============================================================================
# Enable Testing
# ============================================================================

enable_testing()

# ============================================================================
# Installation Targets
# ============================================================================

install(TARGETS portfolio_app
    RUNTIME DESTINATION bin
)

install(TARGETS inmemory_db_plugin sqlite_db_plugin buyhold_strategy_plugin
    LIBRARY DESTINATION lib/portfolio/plugins
)

install(DIRECTORY include/
    DESTINATION include/portfolio
)

# ============================================================================
# Print Configuration
# ============================================================================

message(STATUS "=== Portfolio Management System Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Boost: ${Boost_VERSION}")
message(STATUS "SQLite3: ${SQLite3_VERSION}")
message(STATUS "GTest: Found")
message(STATUS "Plugins output directory: ${CMAKE_BINARY_DIR}/plugins")
message(STATUS "================================================")
