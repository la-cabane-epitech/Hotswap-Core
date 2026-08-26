#pragma once

#include <string>
#include <iostream>
#include <functional>
#include <dlfcn.h>
#include <sys/stat.h>

#include "plugin.hpp"

class DLLoader {
public:
    using PluginUpdateFunc = std::function<void(State*)>;

    DLLoader(std::string library_path);
    ~DLLoader();

    void reload_if_changed();
    bool is_loaded() const;
    PluginUpdateFunc get_function();

private:
    bool load();
    void unload();

    std::string         _library_path;
    void*               _handle;
    time_t              _last_mod_time;
    PluginUpdateFunc    _update_func;
};