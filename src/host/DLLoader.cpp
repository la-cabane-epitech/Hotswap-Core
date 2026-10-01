#include "DLLoader.hpp"

#include <dlfcn.h>
#include <sys/stat.h>

#include <filesystem>
#include <iostream>
#include <utility>

namespace {

bool mtime_of(const std::string &path, time_t &out)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    out = st.st_mtime;
    return true;
}

} // namespace

DLLoader::DLLoader(std::string active_path, std::string candidate_path)
    : _active_path(std::move(active_path)),
      _candidate_path(std::move(candidate_path))
{
    _previous_path = _active_path + ".previous";

    /* The active library's directory is the pipeline's artifact directory. */
    const std::filesystem::path directory = std::filesystem::path(_active_path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
}

DLLoader::~DLLoader()
{
    unload();
}

void DLLoader::poll()
{
    time_t candidate_mtime = 0;

    if (mtime_of(_candidate_path, candidate_mtime) && candidate_mtime > _candidate_mtime) {
        _candidate_mtime = candidate_mtime;
        std::cout << "\n[Runtime] Candidate detected, promoting." << std::endl;
        promote();
        return;
    }

    time_t active_mtime = 0;

    if (mtime_of(_active_path, active_mtime)) {
        if (active_mtime > _active_mtime) {
            _active_mtime = active_mtime;
            if (!load_active())
                std::cerr << "[Runtime] Failed to load the active version." << std::endl;
        }
    } else if (_handle != nullptr) {
        std::cout << "\n[Runtime] Plugin file removed. Unloading." << std::endl;
        unload();
        _active_mtime = 0;
    }
}

/*
** Promotion: the candidate becomes the active version directly, no validation
** step first. The old one is kept as `.previous` — if the dlopen/dlsym that
** follows still fails, we put it back.
*/
void DLLoader::promote()
{
    unload();

    const bool had_active = (rename(_active_path.c_str(), _previous_path.c_str()) == 0);

    if (rename(_candidate_path.c_str(), _active_path.c_str()) != 0) {
        std::cerr << "[Runtime] Promotion failed, restoring previous version." << std::endl;
        if (had_active)
            rename(_previous_path.c_str(), _active_path.c_str());
        load_active();
        return;
    }

    mtime_of(_active_path, _active_mtime);

    if (load_active()) {
        std::cout << "[Runtime] Swap done, session state preserved.\n" << std::endl;
        return;
    }

    /* dlopen/dlsym failed on a file that was just built successfully — a
    ** missing or renamed symbol, typically. Roll back. */
    std::cerr << "[Runtime] Post-swap load failed, rolling back." << std::endl;
    if (had_active && rename(_previous_path.c_str(), _active_path.c_str()) == 0) {
        mtime_of(_active_path, _active_mtime);
        load_active();
    }
}

bool DLLoader::load_active()
{
    unload();

    _handle = dlopen(_active_path.c_str(), RTLD_NOW);
    if (_handle == nullptr) {
        std::cerr << "[Runtime] dlopen: " << dlerror() << std::endl;
        return false;
    }

    auto update = reinterpret_cast<PluginUpdateFunc>(dlsym(_handle, "plugin_update"));
    if (update == nullptr) {
        std::cerr << "[Runtime] dlsym(plugin_update): " << dlerror() << std::endl;
        unload();
        return false;
    }

    _update = update;
    return true;
}

void DLLoader::unload()
{
    if (_handle != nullptr) {
        dlclose(_handle);
        _handle = nullptr;
    }
    _update = nullptr;
}
