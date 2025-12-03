# ============================================================================
# SQLite Database Plugin Tests
# ============================================================================

message(STATUS "Configuring SQLite Database tests...")

# Test for SQLite Database implementation
portfolio_add_test(
    NAME sqlite_database
    SOURCES test_sqlite_database.cpp
    LIBRARIES sqlite_db_impl
)

message(STATUS "  ✓ Added test: sqlite_database")
