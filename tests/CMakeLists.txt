# ============================================================================
# Portfolio Management System - Core Tests
# ============================================================================

message(STATUS "Configuring core tests...")

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
# Plugin Manager Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME plugin_manager
    SOURCES test_plugin_manager.cpp
    LIBRARIES ${CMAKE_DL_LIBS}
)

message(STATUS "Core tests configured successfully")
