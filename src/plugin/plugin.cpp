// plugin.cpp
#include "plugin.hpp"
#include <iostream>
#include <unistd.h>

void __divise(State* state) {
    state->counter = 2;
}
void plugin_update(State* state) {
    state->counter++;
    __divise(state);
    std::cout << "[Plugin v1] Le compteur est maintenant à : " << state->counter << std::endl;
    sleep(1);
}
