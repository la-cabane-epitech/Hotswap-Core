/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Core — rebuilds the plugin sources into a candidate
*/

#pragma once

#include <sys/wait.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "FileWatcher.hpp"

/*
** Build: produces a *candidate*, never the active library.
**
** Adopting that candidate is the Runtime's call, after its canary validates it.
** Writing straight to the active library would leave the pipeline with no way
** to roll back.
*/
class Core {
public:
    Core(std::string source_dir, std::string candidate_path, std::string compiler,
         std::string include_dir)
        : _source_dir(std::move(source_dir)),
          _candidate_path(std::move(candidate_path)),
          _compiler(std::move(compiler)),
          _include_dir(std::move(include_dir))
    {
        /* Logs sit next to the candidate, in the pipeline's artifact directory. */
        const std::filesystem::path directory =
            std::filesystem::path(_candidate_path).parent_path();
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        _log_path = (directory / "build.log").string();
    }

    void onFileChanged(const std::string &path, FileStatus status)
    {
        if (status == FileStatus::erased) {
            std::cout << "[Build] Source removed: " << path << std::endl;
            return;
        }

        const auto extension = std::filesystem::path(path).extension();
        if (extension != ".cpp" && extension != ".hpp")
            return;

        /* An editor may fire several events for a single save; no need to
        ** rebuild that many times. */
        const auto now = std::chrono::steady_clock::now();
        if (now - _last_build < std::chrono::milliseconds(300))
            return;
        _last_build = now;

        std::cout << "\n[Build] " << path << " changed, building candidate..." << std::endl;
        build();
    }

private:
    void build()
    {
        const std::vector<std::string> sources = collect_sources();
        if (sources.empty()) {
            std::cerr << "[Build] No .cpp source found in " << _source_dir << std::endl;
            return;
        }

        const std::string temporary = _candidate_path + ".tmp";

        std::string command = _compiler + " -std=c++17 -shared -fPIC -I" + _include_dir;
        for (const auto &source : sources)
            command += " " + source;
        command += " -o " + temporary + " 2> " + _log_path;

        const int status = std::system(command.c_str());

        /* The previous version ignored this return value: a build failure went
        ** completely unnoticed. system() returns a wait()-style status, not the
        ** compiler's exit code. */
        if (status != 0) {
            const int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
            std::cout << "[Build] FAILED (exit " << code << ") — no candidate produced."
                      << std::endl;
            report_errors();
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            return;
        }

        /* Atomic publish: the Runtime must never see a partial library. */
        std::error_code ec;
        std::filesystem::rename(temporary, _candidate_path, ec);
        if (ec) {
            std::cerr << "[Build] Could not publish candidate: " << ec.message() << std::endl;
            return;
        }

        std::cout << "[Build] Candidate published, waiting for Runtime validation." << std::endl;
    }

    std::vector<std::string> collect_sources() const
    {
        std::vector<std::string> sources;
        std::error_code ec;

        auto it = std::filesystem::recursive_directory_iterator(_source_dir, ec);
        if (ec)
            return sources;

        for (const auto &entry : it) {
            if (entry.is_regular_file(ec) && !ec && entry.path().extension() == ".cpp")
                sources.push_back(entry.path().string());
        }
        return sources;
    }

    void report_errors() const
    {
        std::ifstream log(_log_path);
        if (!log)
            return;

        std::string line;
        for (int printed = 0; printed < MAX_REPORTED_LINES && std::getline(log, line); ++printed)
            std::cout << "  " << line << std::endl;

        std::cout << "  (full log: " << _log_path << ")\n" << std::endl;
    }

    static constexpr int MAX_REPORTED_LINES = 12;

    std::string _source_dir;
    std::string _candidate_path;
    std::string _compiler;
    std::string _include_dir;
    std::string _log_path;

    std::chrono::steady_clock::time_point _last_build = {};
};
