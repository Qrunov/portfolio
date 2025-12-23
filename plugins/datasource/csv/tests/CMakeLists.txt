#============================================================================
# CSV DataSource Plugin Tests
# ============================================================================

message(STATUS "Configuring CSV DataSource plugin tests...")

# Test for CSV DataSource plugin
portfolio_add_test(
    NAME csv_datasource_plugin
    SOURCES test_csv_datasource_plugin.cpp
    LIBRARIES csv_datasource_impl
)

# ----------------------------------------------------------------------------
# CSV DataSource Tests
# ----------------------------------------------------------------------------

portfolio_add_test(
    NAME csv_datasource
    SOURCES test_csv_datasource.cpp
    LIBRARIES csv_datasource_impl
)



# Добавляем зависимость от плагина, чтобы он был собран перед тестами
add_dependencies(test_csv_datasource_plugin csv_plugin)

# Устанавливаем переменную окружения для тестов, чтобы они знали, где искать плагины
set_tests_properties(csv_datasource_plugin PROPERTIES
    ENVIRONMENT "PORTFOLIO_PLUGIN_PATH=${CMAKE_BINARY_DIR}/plugins"
)

message(STATUS "  ✓ Added test: csv_datasource_plugin")
