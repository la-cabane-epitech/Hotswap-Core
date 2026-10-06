# Test suite. Run it after ./build.sh with:
#     ctest --test-dir build --output-on-failure
#
# Adding a test: write it in a test_*.cpp using check.hpp, then register it
# below with hotswap_add_test(). Each executable is one CTest entry.

function(hotswap_add_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Runtime -----------------------------------------------------------------
# One source, several variants: each one exercises one path of the swap.
# Pairs of "name" and "definitions", definitions separated by commas.
set(TEST_PLUGIN_VARIANTS
    v1               TEST_DEFAULTS
    v1_step10        TEST_STEP=10
    v2_padded        TEST_STATE_VERSION=2,TEST_LAYOUT_PADDED
    load_fails       TEST_STATE_VERSION=3,TEST_LOAD_FAILS
    abi_mismatch     TEST_ABI_VERSION=999
    missing_load     TEST_OMIT_LOAD
)

hotswap_add_test(test_runtime hotswap_runtime)

list(LENGTH TEST_PLUGIN_VARIANTS variant_items)
math(EXPR last "${variant_items} - 1")
foreach(index RANGE 0 ${last} 2)
    math(EXPR definitions_index "${index} + 1")
    list(GET TEST_PLUGIN_VARIANTS ${index} variant)
    list(GET TEST_PLUGIN_VARIANTS ${definitions_index} definitions)
    string(REPLACE "," ";" definitions "${definitions}")

    set(target test_plugin_${variant})
    add_library(${target} MODULE plugins/test_plugin.cpp)
    target_link_libraries(${target} PRIVATE hotswap_abi)
    target_compile_definitions(${target} PRIVATE ${definitions})
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")

    string(TOUPPER ${variant} upper)
    target_compile_definitions(test_runtime PRIVATE
        TEST_PLUGIN_${upper}="$<TARGET_FILE:${target}>")
    add_dependencies(test_runtime ${target})
endforeach()
