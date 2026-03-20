/*
** EPITECH PROJECT, 2026
** PoC_CPP
** File description:
** FileWatcher
*/

#ifndef FILEWATCHER_HPP_
#define FILEWATCHER_HPP_

#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <functional>

enum class FileStatus {created, modified, erased};
class FileWatcher {
    public:
        std::string path_to_watch;
        std::chrono::duration<int, std::milli> delay;

        FileWatcher(std::string path_to_watch, std::chrono::duration<int, std::milli> delay) : path_to_watch{path_to_watch}, delay{delay} {
            for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
                _paths[file.path().string()] = std::filesystem::last_write_time(file);
            }
        }

        void start(const std::function<void (std::string, FileStatus)> &action) {
            while(_running) {
                std::this_thread::sleep_for(delay);

                auto it = _paths.begin();
                while (it != _paths.end()) {
                    if (!std::filesystem::exists(it->first)) {
                        action(it->first, FileStatus::erased);
                        it = _paths.erase(it);
                    }
                    else {
                        it++;
                    }
                }

                for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
                    auto current_file_last_write_time = std::filesystem::last_write_time(file);

                    if(!contains(file.path().string())) {
                        _paths[file.path().string()] = current_file_last_write_time;
                        action(file.path().string(), FileStatus::created);
                    } else {
                        if(_paths[file.path().string()] != current_file_last_write_time) {
                            _paths[file.path().string()] = current_file_last_write_time;
                            action(file.path().string(), FileStatus::modified);
                        }
                    }
                }
            }
        }

    protected:
    private:
        std::unordered_map<std::string, std::filesystem::file_time_type> _paths;
        bool _running = true;

        bool contains(const std::string &key) {
            auto el = _paths.find(key);
            return el != _paths.end();
        }
};

#endif /* !FILEWATCHER_HPP_ */
