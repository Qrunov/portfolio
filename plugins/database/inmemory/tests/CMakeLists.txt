# ============================================================================
# InMemory Database Plugin Tests
# ============================================================================

message(STATUS "Configuring InMemory Database tests...")

# Test for InMemory Database implementation
portfolio_add_test(
    NAME inmemory_database
    SOURCES test_inmemory_database.cpp
    LIBRARIES inmemory_db_impl csv_datasource_impl
)

message(STATUS "  ✓ Added test: inmemory_database")
