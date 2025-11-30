# ============================================================================
# Portfolio Management System - CMake Helper Functions
# ============================================================================

# ----------------------------------------------------------------------------
# portfolio_add_plugin
# 
# Helper function to create a plugin with standard settings
#
# Arguments:
#   NAME        - Plugin name (without extension)
#   TYPE        - Plugin type (database or strategy)
#   SOURCES     - List of source files
#   LIBRARIES   - Additional libraries to link
# ----------------------------------------------------------------------------
function(portfolio_add_plugin)
    set(options "")
    set(oneValueArgs NAME TYPE)
    set(multiValueArgs SOURCES LIBRARIES)
    
    cmake_parse_arguments(
        PLUGIN
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    
    # Validate arguments
    if(NOT PLUGIN_NAME)
        message(FATAL_ERROR "portfolio_add_plugin: NAME is required")
    endif()
    
    if(NOT PLUGIN_TYPE)
        message(FATAL_ERROR "portfolio_add_plugin: TYPE is required")
    endif()
    
    if(NOT PLUGIN_SOURCES)
        message(FATAL_ERROR "portfolio_add_plugin: SOURCES is required")
    endif()
    
    # Create shared library
    set(target_name "${PLUGIN_NAME}_plugin")
    add_library(${target_name} SHARED ${PLUGIN_SOURCES})
    
    # Set properties
    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${PLUGIN_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${PORTFOLIO_PLUGIN_OUTPUT_DIR}/${PLUGIN_TYPE}"
        SUFFIX ".so"
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Include directories
    target_include_directories(${target_name} PRIVATE
        ${PORTFOLIO_INCLUDE_DIR}
    )
    
    # Link libraries
    target_link_libraries(${target_name} PRIVATE
        portfolio_core
        ${PLUGIN_LIBRARIES}
    )
    
    # Add to install
    install(TARGETS ${target_name}
        LIBRARY DESTINATION lib/portfolio/plugins/${PLUGIN_TYPE}
    )
    
    message(STATUS "Added plugin: ${PLUGIN_NAME} (${PLUGIN_TYPE})")
endfunction()

# ----------------------------------------------------------------------------
# portfolio_add_test
#
# Helper function to create a test with standard settings
#
# Arguments:
#   NAME        - Test name
#   SOURCES     - List of source files
#   LIBRARIES   - Additional libraries to link
# ----------------------------------------------------------------------------
function(portfolio_add_test)
    set(options "")
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES LIBRARIES)
    
    cmake_parse_arguments(
        TEST
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    
    if(NOT TEST_NAME)
        message(FATAL_ERROR "portfolio_add_test: NAME is required")
    endif()
    
    if(NOT TEST_SOURCES)
        message(FATAL_ERROR "portfolio_add_test: SOURCES is required")
    endif()
    
    # Create test executable
    set(target_name "test_${TEST_NAME}")
    add_executable(${target_name} ${TEST_SOURCES})
    
    # Include directories
    target_include_directories(${target_name} PRIVATE
        ${PORTFOLIO_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    
    # Link libraries
    target_link_libraries(${target_name} PRIVATE
        portfolio_core
        portfolio_cli
        gtest
        gtest_main
        ${TEST_LIBRARIES}
    )
    
    # Add test
    add_test(NAME ${TEST_NAME} COMMAND ${target_name})
    
    # Set test environment
    set_tests_properties(${TEST_NAME} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PORTFOLIO_PLUGIN_PATH=${PORTFOLIO_PLUGIN_OUTPUT_DIR}"
    )
    
    message(STATUS "Added test: ${TEST_NAME}")
endfunction()

# ----------------------------------------------------------------------------
# portfolio_print_configuration
#
# Print project configuration summary
# ----------------------------------------------------------------------------
function(portfolio_print_configuration)
    message(STATUS "")
    message(STATUS "=== Portfolio Management System Configuration ===")
    message(STATUS "Version:                ${PROJECT_VERSION}")
    message(STATUS "Build type:             ${CMAKE_BUILD_TYPE}")
    message(STATUS "C++ Standard:           ${CMAKE_CXX_STANDARD}")
    message(STATUS "")
    message(STATUS "Dependencies:")
    message(STATUS "  Boost:                ${Boost_VERSION}")
    message(STATUS "  SQLite3:              ${SQLite3_VERSION}")
    if(PORTFOLIO_BUILD_TESTS)
        message(STATUS "  GTest:                Found")
    endif()
    message(STATUS "")
    message(STATUS "Options:")
    message(STATUS "  Build tests:          ${PORTFOLIO_BUILD_TESTS}")
    message(STATUS "  Build plugins:        ${PORTFOLIO_BUILD_PLUGINS}")
    message(STATUS "  Enable coverage:      ${PORTFOLIO_ENABLE_COVERAGE}")
    message(STATUS "  Enable sanitizers:    ${PORTFOLIO_ENABLE_SANITIZERS}")
    message(STATUS "")
    message(STATUS "Output directories:")
    message(STATUS "  Binaries:             ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    message(STATUS "  Libraries:            ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
    message(STATUS "  Plugins:              ${PORTFOLIO_PLUGIN_OUTPUT_DIR}")
    message(STATUS "================================================")
    message(STATUS "")
endfunction()

# ----------------------------------------------------------------------------
# portfolio_enable_warnings
#
# Enable comprehensive compiler warnings for a target
# ----------------------------------------------------------------------------
function(portfolio_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wformat=2
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
        )
    endif()
endfunction()

# ----------------------------------------------------------------------------
# portfolio_enable_sanitizers
#
# Enable sanitizers for a target
# ----------------------------------------------------------------------------
function(portfolio_enable_sanitizers target)
    if(PORTFOLIO_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
        )
        message(STATUS "Enabled sanitizers for: ${target}")
    endif()
endfunction()
