#pragma once

#include <string>
#include <ctime>

#include "plugin.hpp"

/*
** Runtime: owns the active plugin and validates candidates before adopting them.
**
** A candidate is never loaded straight into this process. It first runs inside a
** canary — a disposable forked child — and is only promoted if it comes back
** alive. Any failure leaves the active version in place.
*/
class DLLoader {
public:
    using PluginUpdateFunc = void (*)(State*);

    DLLoader(std::string active_path, std::string candidate_path);
    ~DLLoader();

    DLLoader(const DLLoader&) = delete;
    DLLoader& operator=(const DLLoader&) = delete;

    /* Loads the active version if needed, then validates and promotes any
    ** pending candidate. `state` gives the canary a real dataset to run on. */
    void poll(State* state);

    bool is_loaded() const { return _update != nullptr; }
    PluginUpdateFunc get_function() const { return _update; }

private:
    enum class Canary {
        Passed,
        LoadFailed,      /* dlopen failed */
        SymbolMissing,   /* plugin_update not found */
        Crashed,         /* killed by a signal (SIGSEGV, SIGABRT...) */
        TimedOut,        /* SIGALRM: infinite loop */
        ExitedNonZero,
    };

    bool   load_active();
    void   unload();
    void   promote();
    Canary run_canary(const std::string &library, State *state) const;

    static const char *reason(Canary result);

    std::string _active_path;
    std::string _candidate_path;
    std::string _previous_path;
    std::string _canary_log;

    void             *_handle          = nullptr;
    PluginUpdateFunc  _update          = nullptr;
    time_t            _active_mtime    = 0;
    time_t            _candidate_mtime = 0;
};
