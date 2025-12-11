# ============================================================================
# BuyHold Strategy Plugin Tests
# ============================================================================

message(STATUS "Configuring BuyHold Strategy tests...")

# Test for basic BuyHold Strategy functionality
portfolio_add_test(
    NAME buyhold_strategy
    SOURCES test_buyhold_strategy.cpp
    LIBRARIES buyhold_strategy_impl inmemory_db_impl portfolio_core
)


message(STATUS "  ✓ Added test: buyhold_strategy")

