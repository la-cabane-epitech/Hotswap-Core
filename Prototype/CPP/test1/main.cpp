// main.cpp
#include <iostream>
#include <unistd.h>

#include "DLLoader.hpp"

int main() {
    State app_state = {0};

    std::cout << "Démarrage de l'application hôte. En attente du plugin..." << std::endl;

    DLLoader plugin_loader("./libplugin.so");

    while (true) {
        plugin_loader.reload_if_changed();

        if (plugin_loader.is_loaded()) {
            auto update_func = plugin_loader.get_function();
            update_func(&app_state);
        } else {
            std::cout << "[Hôte] Aucun plugin chargé. En attente..." << std::endl;
        }
    }

    return 0;
}
