# ============================================================================
# Database Plugins
# ============================================================================

message(STATUS "Configuring database plugins...")

# Каждый плагин - отдельный модуль со своим CMakeLists.txt
add_subdirectory(inmemory)
add_subdirectory(sqlite)

message(STATUS "Database plugins configured")
