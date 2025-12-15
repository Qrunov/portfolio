# ============================================================================
# CSV Data Source Plugin
# ============================================================================

message(STATUS "  Configuring CSV DataSource plugin...")

# ----------------------------------------------------------------------------
# Implementation Library
# ----------------------------------------------------------------------------

add_library(csv_datasource_impl STATIC
    ${CMAKE_SOURCE_DIR}/src/CSVDataSource.cpp
)

target_include_directories(csv_datasource_impl PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src  # Для CSVDataSource.hpp
)

target_link_libraries(csv_datasource_impl PUBLIC
    portfolio_core
)

set_target_properties(csv_datasource_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Plugin Shared Library
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME csv
    TYPE datasource
    SOURCES CSVDataSourcePlugin.cpp
    LIBRARIES csv_datasource_impl
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

if(PORTFOLIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "    ✓ CSV DataSource plugin configured")
