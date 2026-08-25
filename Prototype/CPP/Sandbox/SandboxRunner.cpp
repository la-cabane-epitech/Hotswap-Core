#include "SandboxRunner.hpp"
#include "StatusWriter.hpp"
#include "plugin.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#include <dlfcn.h>

namespace {

struct ChildResult {
    bool timed_out = false;
    bool exited_normally = false;
    int exit_code = -1;
    bool signaled = false;
    int term_signal = 0;
};

// Polls the child instead of blocking on waitpid() so an infinite loop in
// the candidate can be killed instead of hanging the sandbox forever.
ChildResult wait_with_timeout(pid_t pid, int timeout_ms) {
    using namespace std::chrono;
    ChildResult result;
    auto deadline = steady_clock::now() + milliseconds(timeout_ms);

    while (true) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status)) {
                result.exited_normally = true;
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.signaled = true;
                result.term_signal = WTERMSIG(status);
            }
            return result;
        }
        if (steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0); // reap the zombie
            result.timed_out = true;
            return result;
        }
        std::this_thread::sleep_for(milliseconds(20));
    }
}

// Runs entirely inside the forked child: never returns.
[[noreturn]] void run_candidate_in_child(const std::string& candidate_path,
                                          const std::string& log_path) {
    int fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    void* handle = dlopen(candidate_path.c_str(), RTLD_NOW);
    if (!handle) {
        std::fprintf(stderr, "[Sandbox] dlopen failed: %s\n", dlerror());
        _exit(2);
    }

    dlerror();
    auto* update_func = reinterpret_cast<void (*)(State*)>(dlsym(handle, "plugin_update"));
    if (dlerror() != nullptr || update_func == nullptr) {
        std::fprintf(stderr, "[Sandbox] dlsym('plugin_update') failed\n");
        _exit(2);
    }

    State test_state{};
    update_func(&test_state);
    _exit(0);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

} // namespace

bool validate_candidate(const std::string& module_name,
                         const std::string& candidate_path,
                         const std::string& active_path,
                         int timeout_ms) {
    const std::string status_path = module_name + ".status.json";
    const std::string log_path = module_name + ".log";

    write_status(status_path, module_name, "sandbox_running", "sandbox",
                 candidate_path, active_path, "{}", log_path);

    auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("[Sandbox] fork failed");
        return false;
    }
    if (pid == 0) {
        run_candidate_in_child(candidate_path, log_path);
    }

    ChildResult r = wait_with_timeout(pid, timeout_ms);
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();

    if (r.timed_out) {
        std::cout << "[Sandbox] Timeout (" << timeout_ms << "ms) sur " << candidate_path
                   << " — candidat tué.\n";
        char detail[64];
        std::snprintf(detail, sizeof(detail), "{\"timeout_ms\":%d}", timeout_ms);
        write_status(status_path, module_name, "sandbox_timeout", "sandbox",
                     candidate_path, active_path, detail, log_path);
        return false;
    }

    if (r.exited_normally && r.exit_code == 0) {
        std::cout << "[Sandbox] Candidat validé en " << duration_ms << "ms, promotion.\n";
        char detail[64];
        std::snprintf(detail, sizeof(detail), "{\"duration_ms\":%lld}",
                       static_cast<long long>(duration_ms));
        write_status(status_path, module_name, "sandbox_passed", "sandbox",
                     candidate_path, active_path, detail, log_path);

        if (std::rename(candidate_path.c_str(), active_path.c_str()) != 0) {
            std::perror("[Sandbox] rename candidate->active failed");
            return false;
        }
        return true;
    }

    char detail[160];
    if (r.signaled) {
        std::snprintf(detail, sizeof(detail),
                       "{\"reason\":\"signal\",\"signal\":\"%s\"}",
                       json_escape(strsignal(r.term_signal)).c_str());
    } else {
        std::snprintf(detail, sizeof(detail),
                       "{\"reason\":\"exit_code\",\"exit_code\":%d}", r.exit_code);
    }
    std::cout << "[Sandbox] Candidat rejeté (" << detail << "), on reste sur " << active_path
              << ".\n";
    write_status(status_path, module_name, "sandbox_failed", "sandbox",
                 candidate_path, active_path, detail, log_path);
    return false;
}
