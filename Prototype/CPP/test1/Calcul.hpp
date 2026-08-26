#pragma once
#include "plugin.hpp"
#include <iostream>
#include <unistd.h>

class Calcul {
    public:
        Calcul();
        ~Calcul();

        void __divise(State* state, int number);
        void __add(State* state, int number);
};