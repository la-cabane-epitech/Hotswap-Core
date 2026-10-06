cmake_minimum_required(VERSION 3.16)

project(HotswapCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -pedantic)

find_package(Threads REQUIRED)

# Every runtime artifact of the pipeline lives here: active library, candidate
# and logs. One directory to ignore instead of five file patterns, and a
# repository root that stays readable.
set(HS_ARTIFACT_DIR "${CMAKE_SOURCE_DIR}/.hotswap")
file(MAKE_DIRECTORY "${HS_ARTIFACT_DIR}")

# The suffix comes from CMake instead of a hardcoded ".so": otherwise the host
# looks for a file the build never produces on macOS (.dylib).
set(HS_PLUGIN_ACTIVE    "${HS_ARTIFACT_DIR}/libplugin${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(HS_PLUGIN_CANDIDATE "${HS_PLUGIN_ACTIVE}.candidate")
set(HS_PLUGIN_SRC       "${CMAKE_SOURCE_DIR}/src/plugin")
set(HS_ABI_INCLUDE      "${CMAKE_SOURCE_DIR}/include")

# The frozen contract between the Runtime and any plugin: include/hotswap/abi.hpp.
add_library(hotswap_abi INTERFACE)
target_include_directories(hotswap_abi INTERFACE "${HS_ABI_INCLUDE}")

# The Runtime as a library, shared by the host executable and the tests.
add_library(hotswap_runtime STATIC
    src/host/DLLoader.cpp
)
target_include_directories(hotswap_runtime PUBLIC src/host)
# CMAKE_DL_LIBS is empty on macOS (dlopen lives in libc) and "dl" on Linux:
# a hardcoded "dl" does not link everywhere.
target_link_libraries(hotswap_runtime PUBLIC hotswap_abi ${CMAKE_DL_LIBS})

add_executable(FileWatcher
    src/filewatcher/main.cpp
)

target_include_directories(FileWatcher PRIVATE
    src/filewatcher
)

target_compile_definitions(FileWatcher PRIVATE
    HS_PLUGIN_SRC="${HS_PLUGIN_SRC}"
    HS_PLUGIN_CANDIDATE="${HS_PLUGIN_CANDIDATE}"
    HS_ABI_INCLUDE="${HS_ABI_INCLUDE}"
    HS_COMPILER="${CMAKE_CXX_COMPILER}"
)

target_link_libraries(FileWatcher PRIVATE Threads::Threads)

# The host never sees src/plugin: it only knows the ABI, through the runtime.
add_executable(hotswap_host
    src/host/main.cpp
)

target_compile_definitions(hotswap_host PRIVATE
    HS_PLUGIN_ACTIVE="${HS_PLUGIN_ACTIVE}"
    HS_PLUGIN_CANDIDATE="${HS_PLUGIN_CANDIDATE}"
)

target_link_libraries(hotswap_host PRIVATE hotswap_runtime)

add_library(plugin SHARED
    src/plugin/plugin.cpp
)

target_include_directories(plugin PRIVATE
    src/plugin
)
target_link_libraries(plugin PRIVATE hotswap_abi)

set_target_properties(plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "plugin"
)

# Executables at the root, the plugin alongside the other pipeline artifacts.
set_target_properties(FileWatcher hotswap_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_target_properties(plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${HS_ARTIFACT_DIR}"
)

set_target_properties(hotswap_host PROPERTIES
    OUTPUT_NAME "main"
)

option(HOTSWAP_BUILD_TESTS "Build the test suite" ON)
if(HOTSWAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
