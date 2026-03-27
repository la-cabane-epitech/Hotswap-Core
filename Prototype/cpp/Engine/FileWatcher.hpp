#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class FileWatcher {
public:
    FileWatcher(const std::vector<std::string>& paths, std::function<void()> onRebuild);
    ~FileWatcher();

    void start();
    void stop();

private:
    void watchLoop();

    std::vector<std::string> m_paths;
    std::function<void()> m_onRebuildCallback;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};