#pragma once

#include "App/IModule.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>


class ModuleManager {
public:
    ModuleManager(const std::string& libPath);
    ~ModuleManager();
 
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;
    ModuleManager(ModuleManager&&) = delete;
    ModuleManager& operator=(ModuleManager&&) = delete;

    void load();
    void unload();
    void update();

    bool isLoaded() const;

private:
    const std::string m_libPath;
    void* m_libHandle = nullptr;
    std::unique_ptr<IModule> m_module = nullptr;
    std::vector<char> m_savedState;
    mutable std::mutex m_mutex;
};