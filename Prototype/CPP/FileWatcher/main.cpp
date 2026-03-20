/*
** EPITECH PROJECT, 2026
** PoC_CPP
** File description:
** main
*/

#include "FileWatcher.hpp"
#include "Core.hpp"
#include <iostream>
#include <thread>

int main(int ac, char **av) {
    Core core;
    FileWatcher fw("./", std::chrono::milliseconds(500));
    
    std::thread host_app_thread([ac, av] {
        if (ac == 2) {
            system(av[1]);
        } else {
            system("./main");
        }
    });

    fw.start([&core](const std::string& path, FileStatus status) {
        core.onFileChanged(path, status);
    });

    host_app_thread.join();

    return 0;
}
