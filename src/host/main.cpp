/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Demo host application
*/

/*
** The host keeps the session state alive across reloads without knowing its
** type: the plugin creates it, the Runtime (DLLoader) holds an opaque pointer.
** There is no validation step before a candidate is promoted, though — a
** plugin that crashes or hangs once called takes this process down for real.
*/

#include <chrono>
#include <iostream>
#include <thread>

#include "DLLoader.hpp"

#ifndef HS_PLUGIN_ACTIVE
#define HS_PLUGIN_ACTIVE "./libplugin.so"
#endif
#ifndef HS_PLUGIN_CANDIDATE
#define HS_PLUGIN_CANDIDATE "./libplugin.so.candidate"
#endif

namespace {
constexpr auto IDLE_DELAY = std::chrono::milliseconds(200);
}

int main()
{
    std::cout << "Starting host application." << std::endl;
    std::cout << "  active    : " << HS_PLUGIN_ACTIVE << std::endl;
    std::cout << "  candidate : " << HS_PLUGIN_CANDIDATE << "\n" << std::endl;

    DLLoader runtime(HS_PLUGIN_ACTIVE, HS_PLUGIN_CANDIDATE);
    bool waiting_reported = false;

    while (true) {
        runtime.poll();

        if (runtime.is_loaded()) {
            waiting_reported = false;
            runtime.update();
            continue;
        }

        /* No plugin: wait without burning a core, and say so only once. */
        if (!waiting_reported) {
            std::cout << "[Host] No plugin loaded. Waiting..." << std::endl;
            waiting_reported = true;
        }
        std::this_thread::sleep_for(IDLE_DELAY);
    }

    return 0;
}
