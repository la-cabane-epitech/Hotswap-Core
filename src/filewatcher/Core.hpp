/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** Core — rebuilds the plugin sources into a candidate
*/

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "FileWatcher.hpp"

/*
** Build: produces a *candidate*, never the active library.
**
** Adopting that candidate is the Runtime's call — it promotes it directly, with
** no validation step first. Writing straight to the active library would
** replace the code under the running process, with nothing to fall back on.
*/
class Core {
public:
    Core(std::string source_dir, std::string candidate_path, std::string compiler,
         std::vector<std::string> include_dirs);

    void onFileChanged(const std::string &path, FileStatus status);

private:
    void build();
    std::vector<std::string> collect_sources() const;
    void report_errors() const;

    static constexpr int MAX_REPORTED_LINES = 12;

    std::string _source_dir;
    std::string _candidate_path;
    std::string _compiler;
    std::vector<std::string> _include_dirs;
    std::string _log_path;

    std::chrono::steady_clock::time_point _last_build = {};
};
