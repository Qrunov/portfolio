# ============================================================================
# Database Plugins
# ============================================================================

# ----------------------------------------------------------------------------
# InMemory Database Plugin
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME inmemory_db
    TYPE database
    SOURCES
        inmemory/InMemoryDatabase.cpp
        inmemory/InMemoryDatabasePlugin.cpp
)

# ----------------------------------------------------------------------------
# SQLite Database Plugin
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME sqlite_db
    TYPE database
    SOURCES
        sqlite/SQLiteDatabase.cpp
        sqlite/SQLiteDatabasePlugin.cpp
    LIBRARIES
        ${SQLite3_LIBRARIES}
)

# Add SQLite include directories
target_include_directories(sqlite_db_plugin PRIVATE
    ${SQLite3_INCLUDE_DIRS}
)
