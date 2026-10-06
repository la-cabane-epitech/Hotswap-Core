/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Watcher — watches the plugin sources and triggers the build
*/

#include <chrono>
#include <iostream>
#include <string>

#include "Core.hpp"
#include "FileWatcher.hpp"

#ifndef HS_PLUGIN_SRC
#define HS_PLUGIN_SRC "./src/plugin"
#endif
#ifndef HS_PLUGIN_CANDIDATE
#define HS_PLUGIN_CANDIDATE "./libplugin.so.candidate"
#endif
#ifndef HS_ABI_INCLUDE
#define HS_ABI_INCLUDE "./include"
#endif
#ifndef HS_COMPILER
#define HS_COMPILER "c++"
#endif

namespace {
constexpr auto POLL_DELAY = std::chrono::milliseconds(300);
}

int main(int ac, char **av)
{
    /*
    ** The previous version watched "./" and rebuilt any .cpp in the repository
    ** as the plugin — including the host's own sources. We now watch the plugin
    ** directory only.
    */
    const std::string source_dir = (ac > 1) ? av[1] : HS_PLUGIN_SRC;

    std::cout << "Hotswap-Core watcher" << std::endl;
    std::cout << "  sources   : " << source_dir << std::endl;
    std::cout << "  candidate : " << HS_PLUGIN_CANDIDATE << std::endl;
    std::cout << "  compiler  : " << HS_COMPILER << "\n" << std::endl;

    /* The plugin sees its own headers and the ABI contract, nothing else. */
    Core core(source_dir, HS_PLUGIN_CANDIDATE, HS_COMPILER, {source_dir, HS_ABI_INCLUDE});
    FileWatcher watcher(source_dir, POLL_DELAY);

    watcher.start([&core](const std::string &path, FileStatus status) {
        core.onFileChanged(path, status);
    });

    return 0;
}
