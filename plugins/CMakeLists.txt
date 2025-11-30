# ============================================================================
# Portfolio Plugins
# ============================================================================

message(STATUS "Configuring plugins...")

# Add database plugins
add_subdirectory(database)

# Add strategy plugins
add_subdirectory(strategy)

# Future plugin types can be added here:
# add_subdirectory(datasource)
# add_subdirectory(optimizer)
