/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Demo plugin — implements the contract of hotswap/abi.hpp
*/

#include "plugin.hpp"

#include <chrono>
#include <iostream>
#include <new>
#include <thread>

#include "hotswap/abi.hpp"

int hotswap_abi_version(void)
{
    return HOTSWAP_ABI_VERSION;
}

/* TODO(serialization): computed from the field list (REFLECT or library),
** never typed by hand — a forgotten bump means silent memory corruption. */
uint64_t plugin_state_version(void)
{
    return 1;
}

void *plugin_state_create(void)
{
    return new (std::nothrow) State{};
}

void plugin_state_destroy(void *state)
{
    delete static_cast<State *>(state);
}

/* TODO(serialization): stub. An empty snapshot means a layout change resets
** the state to its defaults — state survives only same-version swaps for now. */
size_t plugin_state_save(const void * /* state */, char * /* out */, size_t /* cap */)
{
    return 0;
}

/* TODO(serialization): stub, see plugin_state_save. */
bool plugin_state_load(void * /* state */, const char * /* in */, size_t /* len */)
{
    return true;
}

void plugin_update(void *opaque)
{
    State *state = static_cast<State *>(opaque);

    state->counter--;
    std::cout << "[Plugin v1] counter = " << state->counter << std::endl;

    /* Demo pacing only, so the printed counter is readable. */
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}
