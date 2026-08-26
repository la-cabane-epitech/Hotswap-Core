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
#include <thread>
#include <unordered_map>

enum class FileStatus { created, modified, erased };

class FileWatcher {
public:
    FileWatcher(std::string path_to_watch, std::chrono::milliseconds delay)
        : path_to_watch(std::move(path_to_watch)), delay(delay)
    {
        for (const auto &entry : safe_iterate())
            _paths[entry.first] = entry.second;
    }

    void stop() { _running = false; }

    void start(const std::function<void(const std::string &, FileStatus)> &action)
    {
        while (_running) {
            std::this_thread::sleep_for(delay);

            for (auto it = _paths.begin(); it != _paths.end();) {
                std::error_code ec;
                if (!std::filesystem::exists(it->first, ec)) {
                    action(it->first, FileStatus::erased);
                    it = _paths.erase(it);
                } else {
                    ++it;
                }
            }

            for (const auto &[path, write_time] : safe_iterate()) {
                const auto known = _paths.find(path);

                if (known == _paths.end()) {
                    _paths[path] = write_time;
                    action(path, FileStatus::created);
                } else if (known->second != write_time) {
                    known->second = write_time;
                    action(path, FileStatus::modified);
                }
            }
        }
    }

    std::string               path_to_watch;
    std::chrono::milliseconds delay;

private:
    /*
    ** A file can vanish between being listed and having its timestamp read —
    ** an editor rewriting through a temporary file, for instance. The
    ** error_code overloads keep that from taking the watcher down.
    */
    std::unordered_map<std::string, std::filesystem::file_time_type> safe_iterate() const
    {
        std::unordered_map<std::string, std::filesystem::file_time_type> found;
        std::error_code ec;

        auto it = std::filesystem::recursive_directory_iterator(path_to_watch, ec);
        if (ec)
            return found;

        for (const auto &entry : it) {
            if (!entry.is_regular_file(ec) || ec)
                continue;
            const auto write_time = std::filesystem::last_write_time(entry.path(), ec);
            if (ec)
                continue;
            found[entry.path().string()] = write_time;
        }
        return found;
    }

    std::unordered_map<std::string, std::filesystem::file_time_type> _paths;
    bool _running = true;
};

#endif /* !FILEWATCHER_HPP_ */
