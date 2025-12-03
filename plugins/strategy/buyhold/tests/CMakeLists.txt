# ============================================================================
# BuyHold Strategy Plugin Tests
# ============================================================================

message(STATUS "Configuring BuyHold Strategy tests...")

# Test for basic BuyHold Strategy functionality
portfolio_add_test(
    NAME buyhold_strategy
    SOURCES test_buyhold_strategy.cpp
    LIBRARIES buyhold_strategy_impl inmemory_db_impl
)

# Test for BuyHold Strategy with dividends
portfolio_add_test(
    NAME buyhold_with_dividends
    SOURCES test_buyhold_with_dividends.cpp
    LIBRARIES buyhold_strategy_impl inmemory_db_impl
)

message(STATUS "  ✓ Added test: buyhold_strategy")
message(STATUS "  ✓ Added test: buyhold_with_dividends")
