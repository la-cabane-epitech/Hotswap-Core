#pragma once

#include <string>
#include <ctime>

#include "plugin.hpp"

/*
** Runtime: owns the active plugin and swaps it for a freshly built candidate
** as soon as one appears on disk.
**
** There is no validation step before adoption: a candidate that dlopen()s and
** dlsym()s successfully is promoted directly. A candidate that crashes or
** hangs once actually called does so in this process, for real — there used
** to be a canary step forking off a disposable child to absorb exactly that;
** it was removed to keep the project to its core promise (state survives a
** reload, including a struct layout change). Revisit if a real need shows up.
*/
class DLLoader {
public:
    using PluginUpdateFunc = void (*)(State*);

    DLLoader(std::string active_path, std::string candidate_path);
    ~DLLoader();

    DLLoader(const DLLoader&) = delete;
    DLLoader& operator=(const DLLoader&) = delete;

    /* Loads the active version if needed, then promotes any pending candidate. */
    void poll();

    bool is_loaded() const { return _update != nullptr; }
    PluginUpdateFunc get_function() const { return _update; }

private:
    bool load_active();
    void unload();
    void promote();

    std::string _active_path;
    std::string _candidate_path;
    std::string _previous_path;

    void             *_handle          = nullptr;
    PluginUpdateFunc  _update          = nullptr;
    time_t            _active_mtime    = 0;
    time_t            _candidate_mtime = 0;
};
