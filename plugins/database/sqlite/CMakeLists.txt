# ============================================================================
# SQLite Database Plugin
# ============================================================================

message(STATUS "  Configuring SQLite Database plugin...")

# ----------------------------------------------------------------------------
# Implementation Library
# ----------------------------------------------------------------------------

add_library(sqlite_db_impl STATIC
    SQLiteDatabase.cpp
)

target_include_directories(sqlite_db_impl PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}  # Для SQLiteDatabase.hpp
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(sqlite_db_impl PUBLIC
    portfolio_core
    ${SQLite3_LIBRARIES}
)

set_target_properties(sqlite_db_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Plugin Shared Library
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME sqlite_db
    TYPE database
    SOURCES SQLiteDatabasePlugin.cpp
    LIBRARIES sqlite_db_impl
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

if(PORTFOLIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "    ✓ SQLite Database plugin configured")
