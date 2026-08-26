#include "plugin.hpp"
#include "Calcul.hpp"
#include <iostream>
#include <unistd.h>

Calcul::Calcul() {

}

Calcul::~Calcul() {

}

void Calcul::__divise(State* state, int number) {
    state->counter /= number;
}

void Calcul::__add(State* state, int number) {
    state->counter += number;
}