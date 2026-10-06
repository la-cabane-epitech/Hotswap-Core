/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Demo plugin — its session state, known by the plugin only
*/

#pragma once

/*
** The Runtime never sees this type: it holds an opaque pointer created by
** plugin_state_create(). Changing this struct must change
** plugin_state_version(), see plugin.cpp.
*/
struct State {
    int counter = 0;
};
