#include "FileWatcher.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>

extern std::mutex g_io_mutex;

FileWatcher::FileWatcher(const std::vector<std::string>& paths, std::function<void()> onRebuild)
    : m_paths(paths)
    , m_onRebuildCallback(onRebuild) {}

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&FileWatcher::watchLoop, this);
}

void FileWatcher::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FileWatcher::watchLoop() {
    std::vector<std::filesystem::file_time_type> last_write_times;
    for (const auto& path : m_paths) {
        last_write_times.push_back(std::filesystem::exists(path)
                                       ? std::filesystem::last_write_time(path)
                                       : std::filesystem::file_time_type::min());
    }

    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        bool needs_rebuild = false;
        for (size_t i = 0; i < m_paths.size(); ++i) {
            if (!std::filesystem::exists(m_paths[i])) continue;
            auto current_write_time = std::filesystem::last_write_time(m_paths[i]);
            if (current_write_time > last_write_times[i]) {
                last_write_times[i] = current_write_time;
                needs_rebuild = true;
            }
        }

        if (needs_rebuild) {
            {
                std::lock_guard<std::mutex> lock(g_io_mutex);
                std::cout << "\n[Watcher] Changement détecté. Recompilation en cours..." << std::endl;
            }

            int result = system("cmake --build . --target app_module");

            if (result == 0 && m_onRebuildCallback) {
                m_onRebuildCallback();
            }
        }
    }
}