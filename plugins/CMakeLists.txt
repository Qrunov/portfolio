# ============================================================================
# Plugins
# ============================================================================

message(STATUS "Configuring plugins...")

# Каждый тип плагина - отдельная поддиректория
add_subdirectory(database)
add_subdirectory(strategy)
add_subdirectory(datasource)

message(STATUS "All plugins configured")
