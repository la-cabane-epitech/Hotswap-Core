#include "plugin.hpp"

#include <chrono>
#include <iostream>
#include <thread>

void plugin_update(State *state)
{
    state->counter++;
    std::cout << "[Plugin v1] counter = " << state->counter << std::endl;

    /* Demo pacing only, so the printed counter is readable. */
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}
