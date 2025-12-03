# ============================================================================
# BuyHold Strategy Plugin
# ============================================================================

message(STATUS "  Configuring BuyHold Strategy plugin...")

# ----------------------------------------------------------------------------
# Implementation Library
# ----------------------------------------------------------------------------

add_library(buyhold_strategy_impl STATIC
    BuyHoldStrategy.cpp
)

target_include_directories(buyhold_strategy_impl PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}  # Для BuyHoldStrategy.hpp
)

target_link_libraries(buyhold_strategy_impl PUBLIC
    portfolio_core
)

set_target_properties(buyhold_strategy_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Plugin Shared Library
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME buyhold_strategy
    TYPE strategy
    SOURCES BuyHoldStrategyPlugin.cpp
    LIBRARIES buyhold_strategy_impl
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

if(PORTFOLIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "    ✓ BuyHold Strategy plugin configured")
