# ============================================================================
# Portfolio Management System - Tests
# ============================================================================

# ----------------------------------------------------------------------------
# Command Line Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME command_line
    SOURCES test_command_line.cpp
)

# ----------------------------------------------------------------------------
# CSV DataSource Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME csv_datasource
    SOURCES test_csv_datasource.cpp
)

# ----------------------------------------------------------------------------
# Portfolio Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME portfolio
    SOURCES test_portfolio.cpp
)

# ----------------------------------------------------------------------------
# InMemory Database Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME inmemory_database
    SOURCES test_inmemory_database.cpp
)

# ----------------------------------------------------------------------------
# Plugin Manager Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME plugin_manager
    SOURCES test_plugin_manager.cpp
    LIBRARIES ${CMAKE_DL_LIBS}
)

# ----------------------------------------------------------------------------
# SQLite Database Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME sqlite_database
    SOURCES test_sqlite_database.cpp
    LIBRARIES ${SQLite3_LIBRARIES} ${CMAKE_DL_LIBS}
)

target_include_directories(test_sqlite_database PRIVATE
    ${SQLite3_INCLUDE_DIRS}
)

# ----------------------------------------------------------------------------
# BuyHold Strategy Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME buyhold_strategy
    SOURCES test_buyhold_strategy.cpp
    LIBRARIES ${CMAKE_DL_LIBS}
)

# ----------------------------------------------------------------------------
# Test Resources
# ----------------------------------------------------------------------------

# Copy test data files if needed
# file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data
#     DESTINATION ${CMAKE_BINARY_DIR}/tests
# )
