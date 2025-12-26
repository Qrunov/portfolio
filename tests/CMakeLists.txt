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

# ----------------------------------------------------------------------------
# Tax Calculator Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME tax_calculator
    SOURCES test_tax_calculator.cpp
)

message(STATUS "Core tests configured successfully")


# ----------------------------------------------------------------------------
# Trading Calendar Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME trading_calendar
    SOURCES test_trading_calendar.cpp
    LIBRARIES inmemory_db_impl
)

message(STATUS "  ✓ Added test: trading_calendar")

# ----------------------------------------------------------------------------
# Inflation Adjuster Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME inflation_adjuster
    SOURCES test_inflation_adjuster.cpp
    LIBRARIES inmemory_db_impl
)

message(STATUS "  ✓ Added test: inflation_adjuster")

portfolio_add_test(
    NAME risk_free_rate_calculator
    SOURCES test_risk_free_rate_calculator.cpp
    LIBRARIES inmemory_db_impl
)

message(STATUS "  ✓ Added test: risk_free_rate_calculator")

portfolio_add_test(
    NAME test_instrument_delete_filters
    SOURCES test_instrument_delete_filters.cpp
    LIBRARIES inmemory_db_impl
)

message(STATUS "  ✓ Added test: test_instrument_delete_filters")


portfolio_add_test(
    NAME recharge_functionality
    SOURCES test_recharge_functionality.cpp
    LIBRARIES inmemory_db_impl
)

message(STATUS "  ✓ Added test: test_recharge_functionality")

