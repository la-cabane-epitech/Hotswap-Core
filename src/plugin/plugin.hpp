#pragma once

struct State {
    int counter;
};

#ifdef __cplusplus
extern "C" {
#endif

void plugin_update(State* state);

#ifdef __cplusplus
}
#endif
