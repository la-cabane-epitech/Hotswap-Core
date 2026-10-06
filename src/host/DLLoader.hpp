/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Runtime — loads the plugin, owns its opaque state, swaps in candidates
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "hotswap/abi.hpp"

/*
** Owns the active plugin and the opaque state it created, and swaps in a
** freshly built candidate as soon as one appears on disk.
**
** The candidate is opened *before* the active version is closed, under a path
** unique to this swap: both versions are in memory at once, so the state can be
** carried over (fast path when plugin_state_version() matches, snapshot
** otherwise) and any failure leaves the running version and its state
** untouched. A loaded file is never renamed or rewritten, so debuggers keep
** finding it.
**
** There is no validation step before adoption: a candidate that loads and
** takes the state over is promoted directly. A plugin that crashes or hangs
** once actually called takes this process down, for real.
*/
class DLLoader {
public:
    DLLoader(std::string active_path, std::string candidate_path);
    ~DLLoader();

    DLLoader(const DLLoader &) = delete;
    DLLoader &operator=(const DLLoader &) = delete;

    /* Promotes a pending candidate, or loads the active version if nothing is
    ** loaded yet. Called between two updates, never during one. */
    void poll();

    bool is_loaded() const { return _current.handle != nullptr; }

    /* Calls plugin_update() on the current state. No-op if nothing is loaded. */
    void update();

    /* The opaque state, for tests and tooling. Never dereference it. */
    void *state() const { return _state; }

    /* Looks a symbol up in the current version, for tests and tooling. */
    void *symbol(const char *name) const;

private:
    struct Plugin {
        void                    *handle        = nullptr;
        std::string              path;
        uint64_t                 state_version = 0;
        hotswap::StateCreateFn   create        = nullptr;
        hotswap::StateDestroyFn  destroy       = nullptr;
        hotswap::StateSaveFn     save          = nullptr;
        hotswap::StateLoadFn     load          = nullptr;
        hotswap::UpdateFn        update        = nullptr;
    };

    static bool open(const std::string &path, Plugin &out);
    static void close(Plugin &plugin);
    static bool snapshot(const Plugin &plugin, const void *state, std::string &out);

    void load_active();
    void promote();
    bool transfer_state(const Plugin &next, void *&next_state) const;
    void retire(Plugin &old);
    void publish_as_active(const std::string &path) const;
    void remove_stale_generations() const;

    std::string _active_path;
    std::string _candidate_path;

    Plugin   _current;
    void    *_state      = nullptr;
    unsigned _generation = 0;

    /* An active file that failed to load is not retried until it changes. */
    bool                            _active_rejected = false;
    std::filesystem::file_time_type _rejected_time   = {};
};
