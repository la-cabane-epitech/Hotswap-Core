/*
** Demo host application.
**
** It owns the session state (`app_state`) and never loses it: code is reloaded
** underneath it, the data stays here.
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
    State app_state = {0};

    std::cout << "Starting host application." << std::endl;
    std::cout << "  active    : " << HS_PLUGIN_ACTIVE << std::endl;
    std::cout << "  candidate : " << HS_PLUGIN_CANDIDATE << "\n" << std::endl;

    DLLoader plugin_loader(HS_PLUGIN_ACTIVE, HS_PLUGIN_CANDIDATE);
    bool waiting_reported = false;

    while (true) {
        plugin_loader.poll(&app_state);

        if (plugin_loader.is_loaded()) {
            waiting_reported = false;
            plugin_loader.get_function()(&app_state);
            continue;
        }

        /* No plugin: wait without burning a core, and say so only once — the
        ** previous version flooded the output. */
        if (!waiting_reported) {
            std::cout << "[Host] No plugin loaded. Waiting..." << std::endl;
            waiting_reported = true;
        }
        std::this_thread::sleep_for(IDLE_DELAY);
    }

    return 0;
}
