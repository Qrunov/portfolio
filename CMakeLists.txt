cmake_minimum_required(VERSION 3.20)
project(PortfolioDatabase VERSION 2.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -fPIC")

# ═══════════════════════════════════════════════════════════════════════════════
# ОПЦИИ СБОРКИ
# ═══════════════════════════════════════════════════════════════════════════════

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_PLUGINS "Build database plugins" ON)
option(BUILD_STRATEGIES "Build trading strategies" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# ═══════════════════════════════════════════════════════════════════════════════
# ВКЛЮЧАЕМЫЕ ДИРЕКТОРИИ
# ═══════════════════════════════════════════════════════════════════════════════

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Поиск зависимостей
find_package(GTest QUIET)
find_package(quant QUIET)

# ═══════════════════════════════════════════════════════════════════════════════
# CORE LIBRARY
# Содержит: интерфейсы, PluginManager, CSVDataSource, Portfolio, базовую стратегию
# ═══════════════════════════════════════════════════════════════════════════════

set(CORE_SOURCES
    src/PluginManager.cpp
    src/CSVDataSource.cpp
    src/Portfolio.cpp
    src/BasePortfolioStrategy.cpp
)

set(CORE_HEADERS
    include/IPortfolioDatabase.hpp
    include/IPluginFactory.hpp
    include/PluginManager.hpp
    include/IDataSource.hpp
    src/CSVDataSource.hpp
    include/Portfolio.hpp
    include/IPortfolioStrategy.hpp
    include/BasePortfolioStrategy.hpp
    include/IStrategyFactory.hpp
)

add_library(portfolio_core ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(portfolio_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(portfolio_core PUBLIC dl)

# ═══════════════════════════════════════════════════════════════════════════════
# DATABASE PLUGINS
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_PLUGINS)
    # ─────────────────────────────────────────────────────────────────────────
    # InMemoryDatabase Plugin
    # ─────────────────────────────────────────────────────────────────────────
    
    add_library(portfolio_inmemory_plugin SHARED
        src/InMemoryDatabase.cpp
        src/InMemoryDatabase.hpp
        src/InMemoryDatabasePlugin.cpp
        src/InMemoryDatabasePlugin.hpp
    )

    target_include_directories(portfolio_inmemory_plugin PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(portfolio_inmemory_plugin PUBLIC portfolio_core)

    install(TARGETS portfolio_inmemory_plugin
        LIBRARY DESTINATION lib/portfolio_plugins
    )

    message(STATUS "✅ Database plugins enabled (portfolio_inmemory_plugin)")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# STRATEGY PLUGINS
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_STRATEGIES)
    # ─────────────────────────────────────────────────────────────────────────
    # Buy & Hold Strategy Plugin
    # ─────────────────────────────────────────────────────────────────────────
    
    add_library(portfolio_buyandhold_strategy SHARED
        src/BuyAndHoldStrategy.cpp
        src/BuyAndHoldStrategy.hpp
        src/BuyAndHoldStrategyPlugin.cpp
        src/BuyAndHoldStrategyPlugin.hpp
    )

    target_include_directories(portfolio_buyandhold_strategy PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(portfolio_buyandhold_strategy PUBLIC portfolio_core)

    if(quant_FOUND)
        target_link_libraries(portfolio_buyandhold_strategy PUBLIC quant)
        message(STATUS "✅ libquant found - trading calendar support enabled")
    else()
        message(STATUS "⚠️  libquant not found - strategies without trading calendar")
    endif()

    install(TARGETS portfolio_buyandhold_strategy
        LIBRARY DESTINATION lib/portfolio_strategies
    )

    message(STATUS "✅ Strategy plugins enabled (portfolio_buyandhold_strategy)")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# ТЕСТЫ
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTS)
    enable_testing()

    if(NOT GTest_FOUND)
        message(WARNING "Google Test not found - skipping tests")
    else()
        # ─────────────────────────────────────────────────────────────────────
        # Test: Portfolio
        # ─────────────────────────────────────────────────────────────────────
        
        add_executable(test_portfolio tests/test_portfolio.cpp)

        target_link_libraries(test_portfolio
            portfolio_core
            GTest::gtest
            GTest::gtest_main
        )

        add_test(NAME PortfolioTests COMMAND test_portfolio)
        message(STATUS "✅ Portfolio tests added")

        # ─────────────────────────────────────────────────────────────────────
        # Test: CSVDataSource
        # ─────────────────────────────────────────────────────────────────────
        
        add_executable(test_csv_datasource tests/test_csv_datasource.cpp)

        target_link_libraries(test_csv_datasource
            portfolio_core
            GTest::gtest
            GTest::gtest_main
        )

        add_test(NAME CSVDataSourceV2Tests COMMAND test_csv_datasource_v2)
        message(STATUS "✅ CSVDataSource tests added")

        # ─────────────────────────────────────────────────────────────────────
        # Test: PluginManager
        # ─────────────────────────────────────────────────────────────────────
        
        add_executable(test_plugin_manager tests/test_plugin_manager.cpp)

        target_link_libraries(test_plugin_manager
            portfolio_core
            GTest::gtest
            GTest::gtest_main
        )

        add_test(NAME PluginManagerTests COMMAND test_plugin_manager)
        message(STATUS "✅ PluginManager tests added")

        # ─────────────────────────────────────────────────────────────────────
        # Test: InMemoryDatabase (requires plugin)
        # ─────────────────────────────────────────────────────────────────────
        
        if(BUILD_PLUGINS)
            add_executable(test_inmemory_database tests/test_inmemory_database.cpp)

            target_link_libraries(test_inmemory_database
                portfolio_inmemory_plugin
                GTest::gtest
                GTest::gtest_main
            )

            add_test(NAME InMemoryDatabaseTests COMMAND test_inmemory_database)
            message(STATUS "✅ InMemoryDatabase tests added")
        endif()

        # ─────────────────────────────────────────────────────────────────────
        # Test: Portfolio Strategy (requires plugin + inmemory)
        # ─────────────────────────────────────────────────────────────────────
        
        if(BUILD_STRATEGIES AND BUILD_PLUGINS)
            add_executable(test_strategy tests/test_strategy_with_inmemory.cpp)

            target_link_libraries(test_strategy
                portfolio_core
                portfolio_inmemory_plugin
                portfolio_buyandhold_strategy
                GTest::gtest
                GTest::gtest_main
            )

            if(quant_FOUND)
                target_link_libraries(test_strategy quant)
            endif()

            add_test(NAME StrategyTests COMMAND test_strategy)
            message(STATUS "✅ Strategy tests added")
        endif()

        message(STATUS "✅ All unit tests configured")
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# CODE COVERAGE
# ═══════════════════════════════════════════════════════════════════════════════

if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    message(STATUS "✅ Code coverage enabled")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# INSTALLATION
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS portfolio_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    include/IPortfolioDatabase.hpp
    include/IPluginFactory.hpp
    include/PluginManager.hpp
    include/Portfolio.hpp
    include/IPortfolioStrategy.hpp
    include/BasePortfolioStrategy.hpp
    include/IStrategyFactory.hpp
    include/IDataSource.hpp
    DESTINATION include/portfolio
)

# ═══════════════════════════════════════════════════════════════════════════════
# BUILD INFORMATION
# ═══════════════════════════════════════════════════════════════════════════════

message(STATUS "")
message(STATUS "╔════════════════════════════════════════════════════════════╗")
message(STATUS "║   Portfolio Database v${PROJECT_VERSION}                      ║")
message(STATUS "╚════════════════════════════════════════════════════════════╝")
message(STATUS "")
message(STATUS "📋 Configuration:")
message(STATUS "   C++ Standard:          ${CMAKE_CXX_STANDARD}")
message(STATUS "   Build Type:            ${CMAKE_BUILD_TYPE}")
message(STATUS "   Build Tests:           ${BUILD_TESTS}")
message(STATUS "   Build Plugins:         ${BUILD_PLUGINS}")
message(STATUS "   Build Strategies:      ${BUILD_STRATEGIES}")
message(STATUS "   Enable Coverage:       ${ENABLE_COVERAGE}")
message(STATUS "   libquant available:    ${quant_FOUND}")
message(STATUS "   Google Test found:     ${GTest_FOUND}")
message(STATUS "   Install Prefix:        ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "📚 Core Library:")
message(STATUS "   • portfolio_core")
message(STATUS "     ├─ PluginManager (plugin manager)")
message(STATUS "     ├─ Portfolio (portfolio management)")
message(STATUS "     ├─ CSVDataSourceV2 (CSV data loading)")
message(STATUS "     ├─ IDataSource (data source interface)")
message(STATUS "     ├─ IPortfolioStrategy (strategy interface)")
message(STATUS "     └─ BasePortfolioStrategy (template method base)")
message(STATUS "")

if(BUILD_PLUGINS)
    message(STATUS "🔌 Database Plugins:")
    message(STATUS "   • portfolio_inmemory_plugin (libportfolio_inmemory_plugin.so)")
    message(STATUS "     └─ InMemoryDatabase (in-memory database implementation)")
    message(STATUS "")
endif()

if(BUILD_STRATEGIES)
    message(STATUS "📈 Strategy Plugins:")
    message(STATUS "   • portfolio_buyandhold_strategy (libportfolio_buyandhold_strategy.so)")
    message(STATUS "     └─ BuyAndHoldStrategy (buy & hold trading strategy)")
    message(STATUS "")
endif()

if(BUILD_TESTS AND GTest_FOUND)
    message(STATUS "🧪 Tests:")
    message(STATUS "   • test_portfolio (25+ tests)")
    message(STATUS "   • test_csv_datasource_v2 (20+ tests)")
    message(STATUS "   • test_plugin_manager (14+ tests)")
    if(BUILD_PLUGINS)
        message(STATUS "   • test_inmemory_database (30+ tests)")
    endif()
    if(BUILD_STRATEGIES AND BUILD_PLUGINS)
        message(STATUS "   • test_strategy (20+ tests with InMemoryDatabase)")
    endif()
    message(STATUS "   Total: 100+ tests")
    message(STATUS "")
endif()

message(STATUS "🚀 Quick Start:")
message(STATUS "   mkdir build && cd build")
message(STATUS "   cmake .. -DCMAKE_BUILD_TYPE=Release \\")
message(STATUS "            -DBUILD_TESTS=ON \\")
message(STATUS "            -DBUILD_PLUGINS=ON \\")
message(STATUS "            -DBUILD_STRATEGIES=ON")
message(STATUS "   cmake --build . -j4")
message(STATUS "   ctest --verbose")
message(STATUS "")
message(STATUS "📦 Install:")
message(STATUS "   cmake --install . --prefix /usr/local")
message(STATUS "")
message(STATUS "╚════════════════════════════════════════════════════════════╝")
message(STATUS "")
