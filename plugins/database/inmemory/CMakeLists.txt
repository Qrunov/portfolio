# ============================================================================
# InMemory Database Plugin
# ============================================================================

message(STATUS "  Configuring InMemory Database plugin...")

# ----------------------------------------------------------------------------
# Implementation Library
# ----------------------------------------------------------------------------

add_library(inmemory_db_impl STATIC
    InMemoryDatabase.cpp
)

target_include_directories(inmemory_db_impl PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}  # Для InMemoryDatabase.hpp
)

target_link_libraries(inmemory_db_impl PUBLIC
    portfolio_core
)

set_target_properties(inmemory_db_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Plugin Shared Library
# ----------------------------------------------------------------------------

portfolio_add_plugin(
    NAME inmemory_db
    TYPE database
    SOURCES InMemoryDatabasePlugin.cpp
    LIBRARIES inmemory_db_impl
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

if(PORTFOLIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "    ✓ InMemory Database plugin configured")
