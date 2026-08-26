/*
** Demo plugin.
**
** Edit this file while the host is running: the watcher rebuilds it into a
** candidate, the canary validates it, and the host adopts it without
** restarting. `state` belongs to the host, it survives every reload.
*/

#include "plugin.hpp"

#include <chrono>
#include <iostream>
#include <thread>

void plugin_update(State *state)
{
    state->counter++;
    std::cout << "[Plugin v1] counter = " << state->counter << std::endl;

    /* Demo pacing only. The canary calls this function several times under a
    ** timeout: a plugin sleeping too long here would be rejected. */
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}
