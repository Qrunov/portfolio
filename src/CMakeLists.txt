# ============================================================================
# Core Library
# ============================================================================

set(PORTFOLIO_CORE_SOURCES
    Portfolio.cpp
    CSVDataSource.cpp
    BasePortfolioStrategy.cpp
    PortfolioManager.cpp
    TaxCalculator.cpp
    TradingCalendar.cpp
    InflationAdjuster.cpp  # Добавить
    #    InMemoryDatabase.cpp
)

add_library(portfolio_core STATIC ${PORTFOLIO_CORE_SOURCES})

set_target_properties(portfolio_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(portfolio_core PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_core PUBLIC
    ${Boost_LIBRARIES}
)

# Enable warnings
portfolio_enable_warnings(portfolio_core)

# ============================================================================
# CLI Library
# ============================================================================

set(PORTFOLIO_CLI_SOURCES
    CommandLineParser.cpp
    CommandExecutor.cpp
)

add_library(portfolio_cli STATIC ${PORTFOLIO_CLI_SOURCES})

target_include_directories(portfolio_cli PUBLIC
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_cli PUBLIC
    portfolio_core
    ${Boost_LIBRARIES}
)

# Enable warnings
portfolio_enable_warnings(portfolio_cli)

# ============================================================================
# Main Application
# ============================================================================

add_executable(portfolio_app main.cpp)

target_include_directories(portfolio_app PRIVATE
    ${PORTFOLIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(portfolio_app PRIVATE
    portfolio_core
    portfolio_cli
    ${Boost_LIBRARIES}
    ${CMAKE_DL_LIBS}
)

# Enable warnings
portfolio_enable_warnings(portfolio_app)

# Optional sanitizers
if(PORTFOLIO_ENABLE_SANITIZERS)
    portfolio_enable_sanitizers(portfolio_app)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS portfolio_core portfolio_cli
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
