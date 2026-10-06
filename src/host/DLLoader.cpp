/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Runtime — loads the plugin, owns its opaque state, swaps in candidates
*/

#include "DLLoader.hpp"

#include <dlfcn.h>

#include <iostream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

/* Suffix of the per-swap copies a candidate is loaded from. */
const std::string GENERATION_TAG = ".gen";

template <typename Fn>
bool resolve(void *handle, const std::string &path, const char *name, Fn &out)
{
    out = reinterpret_cast<Fn>(dlsym(handle, name));
    if (out == nullptr)
        std::cerr << "[Runtime] " << path << ": missing symbol " << name << std::endl;
    return out != nullptr;
}

} // namespace

DLLoader::DLLoader(std::string active_path, std::string candidate_path)
    : _active_path(std::move(active_path)),
      _candidate_path(std::move(candidate_path))
{
    /* The active library's directory is the pipeline's artifact directory. */
    std::error_code ec;
    fs::create_directories(fs::path(_active_path).parent_path(), ec);
    remove_stale_generations();
}

DLLoader::~DLLoader()
{
    if (_state != nullptr)
        _current.destroy(_state);
    _state = nullptr;
    retire(_current);
}

void DLLoader::poll()
{
    std::error_code ec;

    if (fs::exists(_candidate_path, ec)) {
        std::cout << "\n[Runtime] Candidate detected, promoting." << std::endl;
        promote();
        return;
    }
    if (!is_loaded())
        load_active();
}

void DLLoader::update()
{
    if (is_loaded())
        _current.update(_state);
}

void *DLLoader::symbol(const char *name) const
{
    return is_loaded() ? dlsym(_current.handle, name) : nullptr;
}

/*
** dlopen + every symbol of the contract + ABI version check. On any failure the
** library is closed again and `out` is left untouched.
*/
bool DLLoader::open(const std::string &path, Plugin &out)
{
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::cerr << "[Runtime] dlopen: " << dlerror() << std::endl;
        return false;
    }

    Plugin plugin;
    plugin.handle = handle;
    plugin.path = path;

    hotswap::AbiVersionFn   abi_version   = nullptr;
    hotswap::StateVersionFn state_version = nullptr;

    bool ok = resolve(handle, path, "hotswap_abi_version", abi_version);
    if (ok && abi_version() != HOTSWAP_ABI_VERSION) {
        std::cerr << "[Runtime] " << path << ": built against ABI v" << abi_version()
                  << ", this Runtime speaks v" << HOTSWAP_ABI_VERSION << std::endl;
        ok = false;
    }
    /* Each one is resolved even after a failure, so that every missing symbol
    ** is reported at once. */
    ok = resolve(handle, path, "plugin_state_version", state_version) && ok;
    ok = resolve(handle, path, "plugin_state_create", plugin.create) && ok;
    ok = resolve(handle, path, "plugin_state_destroy", plugin.destroy) && ok;
    ok = resolve(handle, path, "plugin_state_save", plugin.save) && ok;
    ok = resolve(handle, path, "plugin_state_load", plugin.load) && ok;
    ok = resolve(handle, path, "plugin_update", plugin.update) && ok;

    if (!ok) {
        dlclose(handle);
        return false;
    }

    plugin.state_version = state_version();
    out = std::move(plugin);
    return true;
}

void DLLoader::close(Plugin &plugin)
{
    if (plugin.handle != nullptr)
        dlclose(plugin.handle);
    plugin = Plugin{};
}

/* Two calls, the snprintf way: the first one only asks for the size. */
bool DLLoader::snapshot(const Plugin &plugin, const void *state, std::string &out)
{
    const size_t needed = plugin.save(state, nullptr, 0);
    std::vector<char> buffer(needed);

    if (needed > 0 && plugin.save(state, buffer.data(), buffer.size()) != needed)
        return false;
    out.assign(buffer.begin(), buffer.end());
    return true;
}

/*
** Initial load, straight from the active path: there is no state to carry over
** yet, the plugin creates a fresh one.
*/
void DLLoader::load_active()
{
    std::error_code ec;
    const fs::file_time_type time = fs::last_write_time(_active_path, ec);
    if (ec || (_active_rejected && time == _rejected_time))
        return;

    Plugin plugin;
    void *state = nullptr;

    if (open(_active_path, plugin)) {
        state = plugin.create();
        if (state == nullptr) {
            std::cerr << "[Runtime] plugin_state_create failed." << std::endl;
            close(plugin);
        }
    }
    if (state == nullptr) {
        _active_rejected = true;
        _rejected_time = time;
        return;
    }

    _active_rejected = false;
    _current = std::move(plugin);
    _state = state;
    std::cout << "[Runtime] Loaded " << _active_path << std::endl;
}

/*
** Candidate → running version. Until the point of no return, the current
** version and its state are never touched: any failure only discards the
** candidate.
*/
void DLLoader::promote()
{
    /* A unique path per swap: dlopen() matches already loaded libraries by
    ** path, so reusing the candidate's name would hand back the old code. */
    const std::string staged = _active_path + GENERATION_TAG + std::to_string(++_generation);

    std::error_code ec;
    fs::rename(_candidate_path, staged, ec);
    if (ec) {
        std::cerr << "[Runtime] Cannot stage the candidate: " << ec.message() << std::endl;
        fs::remove(_candidate_path, ec);
        return;
    }

    Plugin next;
    void *next_state = nullptr;

    if (!open(staged, next) || !transfer_state(next, next_state)) {
        close(next);
        fs::remove(staged, ec);
        std::cerr << "[Runtime] Candidate rejected, "
                  << (is_loaded() ? "the running version is kept." : "nothing loaded.")
                  << std::endl;
        return;
    }

    /* Point of no return. The old state is destroyed by the code that created
    ** it, while that code is still loaded. */
    const char *outcome = "fresh state created.";
    if (_state != nullptr && next_state == _state) {
        outcome = "session state kept as is.";
    } else if (_state != nullptr) {
        _current.destroy(_state);
        outcome = "state migrated to the new layout.";
    }
    retire(_current);

    _current = std::move(next);
    _state = next_state;
    publish_as_active(staged);

    std::cout << "[Runtime] Swap done, " << outcome << "\n" << std::endl;
}

/*
** Gives the next version a state: a fresh one if there is none yet, the same
** one if the layout is unchanged, a migrated one otherwise.
*/
bool DLLoader::transfer_state(const Plugin &next, void *&next_state) const
{
    if (_state == nullptr) {
        next_state = next.create();
        if (next_state == nullptr)
            std::cerr << "[Runtime] plugin_state_create failed." << std::endl;
        return next_state != nullptr;
    }

    /* Fast path, the common case: same layout, nothing is serialized. */
    if (next.state_version == _current.state_version) {
        next_state = _state;
        return true;
    }

    std::cout << "[Runtime] State layout changed (" << std::hex << _current.state_version
              << " -> " << next.state_version << std::dec << "), migrating." << std::endl;

    std::string data;
    if (!snapshot(_current, _state, data)) {
        std::cerr << "[Runtime] plugin_state_save failed." << std::endl;
        return false;
    }

    void *fresh = next.create();
    if (fresh == nullptr) {
        std::cerr << "[Runtime] plugin_state_create failed." << std::endl;
        return false;
    }
    if (!next.load(fresh, data.data(), data.size())) {
        std::cerr << "[Runtime] plugin_state_load rejected the snapshot." << std::endl;
        next.destroy(fresh);
        return false;
    }

    next_state = fresh;
    return true;
}

/* Unloads a version and deletes its per-swap copy, never the active file. */
void DLLoader::retire(Plugin &old)
{
    const std::string path = old.path;

    close(old);
    if (!path.empty() && path != _active_path) {
        std::error_code ec;
        fs::remove(path, ec);
    }
}

/*
** The active file always holds the latest promoted version, so a restarted
** Runtime starts from it. Copy + rename: the loaded file itself is never
** rewritten in place.
*/
void DLLoader::publish_as_active(const std::string &path) const
{
    const std::string temporary = _active_path + ".tmp";
    std::error_code ec;

    fs::copy_file(path, temporary, fs::copy_options::overwrite_existing, ec);
    if (!ec)
        fs::rename(temporary, _active_path, ec);
    if (ec)
        std::cerr << "[Runtime] Could not refresh " << _active_path << ": " << ec.message()
                  << std::endl;
}

/* Per-swap copies left behind by a Runtime that did not exit cleanly. */
void DLLoader::remove_stale_generations() const
{
    const fs::path active(_active_path);
    const std::string prefix = active.filename().string() + GENERATION_TAG;
    std::error_code ec;

    for (const auto &entry : fs::directory_iterator(active.parent_path(), ec)) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }
}
