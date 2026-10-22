/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** FileWatcher — watches a directory by polling modification times
*/

#ifndef FILEWATCHER_HPP_
#define FILEWATCHER_HPP_

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

enum class FileStatus {
    created,
    modified,
    erased
};

class FileWatcher {
public:
    FileWatcher(std::string path_to_watch, std::chrono::milliseconds delay);

    void stop();
    void start(const std::function<void(const std::string &, FileStatus)> &action);

    std::string               path_to_watch;
    std::chrono::milliseconds delay;

private:
    /*
    ** A file can vanish between being listed and having its timestamp read —
    ** an editor rewriting through a temporary file, for instance. The
    ** error_code overloads keep that from taking the watcher down.
    */
    std::unordered_map<std::string, std::filesystem::file_time_type> safe_iterate() const;

    std::unordered_map<std::string, std::filesystem::file_time_type> _paths;
    bool _running = true;
};

#endif /* !FILEWATCHER_HPP_ */
