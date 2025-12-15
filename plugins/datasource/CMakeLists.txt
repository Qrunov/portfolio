# ============================================================================
# Data Source Plugins
# ============================================================================

message(STATUS "Configuring datasource plugins...")

# CSV DataSource plugin
add_subdirectory(csv)

# Здесь можно добавить другие типы источников данных:
# add_subdirectory(json)
# add_subdirectory(xml)
# add_subdirectory(api)

message(STATUS "DataSource plugins configured")
