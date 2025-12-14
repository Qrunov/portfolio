cmake_minimum_required(VERSION 3.20)
set(PATCH_VERSION "1" CACHE INTERNAL "Patch version")
project(PortfolioManagement VERSION 1.0.${PATCH_VERSION} LANGUAGES CXX)

# ============================================================================
# Project Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Project Options
# ============================================================================

option(PORTFOLIO_BUILD_TESTS "Build tests" ON)
option(PORTFOLIO_BUILD_PLUGINS "Build plugins" ON)
option(PORTFOLIO_ENABLE_COVERAGE "Enable code coverage" OFF)
option(PORTFOLIO_ENABLE_SANITIZERS "Enable sanitizers" OFF)

# ============================================================================
# CMake Module Path
# ============================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PortfolioHelpers)

# ============================================================================
# Find Dependencies
# ============================================================================

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(SQLite3 REQUIRED)

if(PORTFOLIO_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()

# ============================================================================
# Global Include Directories
# ============================================================================

set(PORTFOLIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ============================================================================
# Output Directories
# ============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Plugin output directory
set(PORTFOLIO_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")

# ============================================================================
# Subdirectories
# ============================================================================

add_subdirectory(src)

if(PORTFOLIO_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(PORTFOLIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS portfolio
    RUNTIME DESTINATION bin
)

#install(DIRECTORY include/
#    DESTINATION include/portfolio
#    FILES_MATCHING PATTERN "*.hpp"
#)

# Install plugin directories structure
install(DIRECTORY "${PORTFOLIO_PLUGIN_OUTPUT_DIR}/"
    DESTINATION lib/portfolio/plugins
    FILES_MATCHING PATTERN "*.so"
)

# ============================================================================
# Package Configuration
# ============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PortfolioManagementConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PortfolioManagementConfig.cmake"
    INSTALL_DESTINATION lib/cmake/PortfolioManagement
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PortfolioManagementConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PortfolioManagementConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PortfolioManagementConfigVersion.cmake"
    DESTINATION lib/cmake/PortfolioManagement
)

install(FILES ${CMAKE_BINARY_DIR}/portfolio_sample.db  DESTINATION ${CMAKE_INSTALL_DATADIR})

file(ARCHIVE_EXTRACT
    INPUT ${CMAKE_SOURCE_DIR}/database/portfolio_sample.db.tar.gz 
    DESTINATION  ${CMAKE_BINARY_DIR}
)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-Linux")
set(CPACK_PACKAGE_CONTACT example@example.com)

# Включите автоматическое определение зависимостей
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)

# Установите контрольные уровни
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_STRICT_LEVEL 1)  # 0-3: 0=минимальный, 3=строгий
include(CPack)

# ============================================================================
# Print Configuration Summary
# ============================================================================

portfolio_print_configuration()
