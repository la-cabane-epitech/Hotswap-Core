#include "ModuleManager.hpp"
#include <dlfcn.h>
#include <iostream>

std::mutex g_io_mutex;

ModuleManager::ModuleManager(const std::string& libPath)
    : m_libPath(libPath) {}

ModuleManager::~ModuleManager() {
    unload();
}

bool ModuleManager::isLoaded() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_libHandle != nullptr;
}

void ModuleManager::unload() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_module) {
        m_savedState = m_module->serialize();
        m_module.reset();
    }
    if (m_libHandle) {
        dlclose(m_libHandle);
        m_libHandle = nullptr;
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cout << "Bibliothèque du module déchargée." << std::endl;
    }
}

void ModuleManager::load() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_module) {
        m_savedState = m_module->serialize();
        m_module.reset();
    }
    if (m_libHandle) {
        dlclose(m_libHandle);
        m_libHandle = nullptr;
    }

    {
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cout << "Tentative de chargement du module depuis : " << m_libPath << std::endl;
    }
    m_libHandle = dlopen(m_libPath.c_str(), RTLD_LAZY);
    if (!m_libHandle) {
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cerr << "Erreur lors du chargement de la bibliothèque : " << dlerror() << std::endl;
        return;
    }

    dlerror();

    auto create_func = (CreateModuleFunc)dlsym(m_libHandle, "create_module");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cerr << "Erreur lors de la récupération du symbole 'create_module': " << dlsym_error << std::endl;
        dlclose(m_libHandle);
        m_libHandle = nullptr;
        return;
    }

    m_module.reset(create_func());
    if (!m_savedState.empty()) {
        m_module->deserialize(m_savedState);
    }
    {
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cout << "Module chargé avec succès." << std::endl;
    }
}

void ModuleManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_module) {
        m_module->update();
    } else {
        std::lock_guard<std::mutex> io_lock(g_io_mutex);
        std::cout << "Aucun module n'est chargé. Utilisez la commande 'load'." << std::endl;
    }
}