cmake_minimum_required(VERSION 3.16)

project(HotswapCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -pedantic)

find_package(Threads REQUIRED)

add_executable(FileWatcher
    src/filewatcher/main.cpp
)

target_include_directories(FileWatcher PRIVATE
    src/filewatcher
)

target_link_libraries(FileWatcher PRIVATE Threads::Threads)

add_executable(hotswap_host
    src/host/main.cpp
    src/host/DLLoader.cpp
)

target_include_directories(hotswap_host PRIVATE
    src/host
    src/plugin
)

target_link_libraries(hotswap_host PRIVATE dl)

add_library(plugin SHARED
    src/plugin/plugin.cpp
)

target_include_directories(plugin PRIVATE
    src/plugin
)

set_target_properties(plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "plugin"
)

set_target_properties(FileWatcher hotswap_host plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_target_properties(hotswap_host PROPERTIES
    OUTPUT_NAME "main"
)
