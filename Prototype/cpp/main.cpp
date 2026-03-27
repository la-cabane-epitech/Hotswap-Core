#include <iostream>
#include <string>
#include <mutex>

#include "Engine/FileWatcher.hpp"
#include "Engine/ModuleManager.hpp"

extern std::mutex g_io_mutex;

int main() {
    const std::string libPath = "./Module/libapp_moduleapp_module.so";
    ModuleManager moduleManager(libPath);

    const std::vector<std::string> watchedPaths = {
        "../App/AppModule.cpp",
        "../App/AppModule.hpp",
        "../Engine/Serializer.cpp",
        "../Engine/Serializer.hpp",
    };

    auto onRebuildCallback = [&moduleManager]() {
        {
            std::lock_guard<std::mutex> lock(g_io_mutex);
            std::cout << "[Watcher] Recompilation réussie." << std::endl;
            if (moduleManager.isLoaded()) {
                std::cout << "[Watcher] Rechargement automatique en cours..." << std::endl;
            }
            std::cout << "\n> " << std::flush;
        }
        if (moduleManager.isLoaded()) {
            moduleManager.load();
        }
    };

    FileWatcher watcher(watchedPaths, onRebuildCallback);
    watcher.start();

    std::cout << "Démo de Hot Reloading" << std::endl;
    std::cout << "Commandes : 'load' (ou 'reload'), 'update', 'unload', 'exit'" << std::endl;
    std::cout << "Le guetteur de fichiers est actif en arrière-plan." << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);

        if (input == "exit") break;
        if (input == "load" || input == "reload") {
            moduleManager.load();
        } else if (input == "update") {
            moduleManager.update();
        } else if (input == "unload") {
            moduleManager.unload();
        } else {
            std::cout << "Commande inconnue. Commandes disponibles : 'load', 'update', 'unload', 'exit'" << std::endl;
        }
    }

    watcher.stop();
    std::cout << "Programme terminé." << std::endl;
    return 0;
}