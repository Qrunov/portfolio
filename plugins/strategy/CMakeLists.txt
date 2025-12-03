# ============================================================================
# Strategy Plugins
# ============================================================================

message(STATUS "Configuring strategy plugins...")

# Каждый плагин - отдельный модуль со своим CMakeLists.txt
add_subdirectory(buyhold)

# Будущие стратегии можно легко добавить:
# add_subdirectory(momentum)
# add_subdirectory(meanreversion)

message(STATUS "Strategy plugins configured")
