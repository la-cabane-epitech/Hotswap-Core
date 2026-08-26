#include "DLLoader.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <utility>

namespace {

/* How many times the canary calls the entry point before approving. */
constexpr int  CANARY_ITERATIONS = 3;
/* Past this, the candidate is considered stuck (infinite loop). */
constexpr long CANARY_TIMEOUT_MS = 2000;

/* Canary exit codes, so a load failure can be told apart from a crash during
** execution. */
constexpr int EXIT_DLOPEN_FAILED  = 10;
constexpr int EXIT_SYMBOL_MISSING = 11;

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

    /* Logs sit next to the library, in the pipeline's artifact directory. */
    const std::filesystem::path directory = std::filesystem::path(_active_path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    _canary_log = (directory / "canary.log").string();
}

DLLoader::~DLLoader()
{
    unload();
}

void DLLoader::poll(State *state)
{
    time_t candidate_mtime = 0;

    if (mtime_of(_candidate_path, candidate_mtime) && candidate_mtime > _candidate_mtime) {
        _candidate_mtime = candidate_mtime;

        std::cout << "\n[Runtime] Candidate detected, running canary..." << std::endl;
        const Canary result = run_canary(_candidate_path, state);

        if (result == Canary::Passed) {
            std::cout << "[Runtime] Canary passed." << std::endl;
            promote();
            return;
        }

        std::cout << "[Runtime] Canary rejected (" << reason(result) << ")." << std::endl;
        std::cout << "[Runtime] Keeping the active version. Details: "
                  << _canary_log << "\n" << std::endl;
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
** The canary: a forked child loads and runs the candidate in our place.
**
** The child inherits a copy-on-write copy of the state, so the candidate runs on
** the real session data without being able to corrupt ours. If it crashes, only
** the child dies and its exit status tells us why.
*/
DLLoader::Canary DLLoader::run_canary(const std::string &library, State *state) const
{
    const pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "[Runtime] fork() failed, rejecting the candidate to be safe." << std::endl;
        return Canary::Crashed;
    }

    if (pid == 0) {
        /* --- child --- */

        /* Send the candidate's output to a log file: without this, every save
        ** would print the plugin traces twice. */
        const int fd = open(_canary_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        /* Guard against infinite loops: SIGALRM kills the child by default. */
        struct itimerval timer = {};
        timer.it_value.tv_sec  = CANARY_TIMEOUT_MS / 1000;
        timer.it_value.tv_usec = (CANARY_TIMEOUT_MS % 1000) * 1000;
        setitimer(ITIMER_REAL, &timer, nullptr);

        /* RTLD_NOW: we want missing symbols now, not on the first call. */
        void *handle = dlopen(library.c_str(), RTLD_NOW);
        if (handle == nullptr) {
            std::fprintf(stderr, "dlopen: %s\n", dlerror());
            _exit(EXIT_DLOPEN_FAILED);
        }

        auto update = reinterpret_cast<PluginUpdateFunc>(dlsym(handle, "plugin_update"));
        if (update == nullptr) {
            std::fprintf(stderr, "dlsym(plugin_update): %s\n", dlerror());
            _exit(EXIT_SYMBOL_MISSING);
        }

        for (int i = 0; i < CANARY_ITERATIONS; ++i)
            update(state);

        _exit(0);
    }

    /* --- parent --- */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return Canary::Crashed;

    if (WIFSIGNALED(status))
        return WTERMSIG(status) == SIGALRM ? Canary::TimedOut : Canary::Crashed;

    if (WIFEXITED(status)) {
        switch (WEXITSTATUS(status)) {
            case 0:                   return Canary::Passed;
            case EXIT_DLOPEN_FAILED:  return Canary::LoadFailed;
            case EXIT_SYMBOL_MISSING: return Canary::SymbolMissing;
            default:                  return Canary::ExitedNonZero;
        }
    }
    return Canary::Crashed;
}

/*
** Promotion: the candidate becomes the active version. The old one is kept as
** `.previous` — if loading failed despite the canary, we put it back.
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

    /* Should not happen: the canary loaded this very file successfully. */
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

const char *DLLoader::reason(Canary result)
{
    switch (result) {
        case Canary::Passed:        return "passed";
        case Canary::LoadFailed:    return "dlopen";
        case Canary::SymbolMissing: return "symbol";
        case Canary::Crashed:       return "signal";
        case Canary::TimedOut:      return "timeout";
        case Canary::ExitedNonZero: return "exit_code";
    }
    return "unknown";
}
