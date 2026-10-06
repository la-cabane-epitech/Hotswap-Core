/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Test plugin — compiled in several variants, see tests/CMakeLists.txt
*/

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "hotswap/abi.hpp"

#ifndef TEST_STATE_VERSION
#define TEST_STATE_VERSION 1
#endif
#ifndef TEST_STEP
#define TEST_STEP 1
#endif
#ifndef TEST_ABI_VERSION
#define TEST_ABI_VERSION HOTSWAP_ABI_VERSION
#endif

namespace {

struct State {
#ifdef TEST_LAYOUT_PADDED
    double padding = 0.0; /* shifts `counter`: a raw copy would read garbage */
#endif
    int counter = 0;
};

} // namespace

/* Not part of the ABI: lets the tests read the opaque state. */
extern "C" HOTSWAP_EXPORT int test_counter(const void *state)
{
    return static_cast<const State *>(state)->counter;
}

int hotswap_abi_version(void)
{
    return TEST_ABI_VERSION;
}

uint64_t plugin_state_version(void)
{
    return TEST_STATE_VERSION;
}

void *plugin_state_create(void)
{
    return new (std::nothrow) State{};
}

void plugin_state_destroy(void *state)
{
    delete static_cast<State *>(state);
}

/* A deliberately naive name=value snapshot, just enough to test the Runtime. */
size_t plugin_state_save(const void *state, char *out, size_t cap)
{
    char text[32];
    const int length = std::snprintf(text, sizeof(text), "counter=%d",
                                     static_cast<const State *>(state)->counter);
    if (length < 0)
        return 0;
    if (out != nullptr && cap >= static_cast<size_t>(length))
        std::memcpy(out, text, static_cast<size_t>(length));
    return static_cast<size_t>(length);
}

#ifndef TEST_OMIT_LOAD
bool plugin_state_load(void *state, const char *in, size_t len)
{
#ifdef TEST_LOAD_FAILS
    (void)state;
    (void)in;
    (void)len;
    return false;
#else
    const std::string text(in, len);
    return std::sscanf(text.c_str(), "counter=%d", &static_cast<State *>(state)->counter) == 1;
#endif
}
#endif

void plugin_update(void *state)
{
    static_cast<State *>(state)->counter += TEST_STEP;
}
