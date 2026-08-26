// plugin.cpp
#include "plugin.hpp"
#include "Calcul.hpp"
#include <iostream>
#include <unistd.h>

void plugin_update(State* state) {
    state->counter++;
    Calcul cal;
    cal.__add(state, 2);
    std::cout << "[Plugin v1] Le compteur est maintenant à : " << state->counter << std::endl;
    sleep(1);
}
