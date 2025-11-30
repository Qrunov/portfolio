# ============================================================================
# Strategy Plugins
# ============================================================================

# ----------------------------------------------------------------------------
# BuyHold Strategy Plugin
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME buyhold_strategy
    TYPE strategy
    SOURCES
        buyhold/BuyHoldStrategy.cpp
        buyhold/BuyHoldStrategyPlugin.cpp
)

# ----------------------------------------------------------------------------
# Future strategies can be added here:
# ----------------------------------------------------------------------------

# portfolio_add_plugin(
#     NAME momentum_strategy
#     TYPE strategy
#     SOURCES
#         momentum/MomentumStrategy.cpp
#         momentum/MomentumStrategyPlugin.cpp
# )
