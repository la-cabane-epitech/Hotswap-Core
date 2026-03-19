#include "DLLoader.hpp"
#include <utility>

DLLoader::DLLoader(std::string library_path)
    : _library_path(std::move(library_path)),
      _handle(nullptr),
      _last_mod_time(0),
      _update_func(nullptr)
{
    reload_if_changed();
}

DLLoader::~DLLoader()
{
    unload();
}

void DLLoader::reload_if_changed()
{
    struct stat file_stats;
    if (stat(_library_path.c_str(), &file_stats) == 0) {
        if (file_stats.st_mtime > _last_mod_time) {
            std::cout << "\n[Hôte] Détection d'une nouvelle version du plugin. Rechargement..." << std::endl;
            _last_mod_time = file_stats.st_mtime;

            if (load()) {
                std::cout << "[Hôte] Rechargement réussi.\n" << std::endl;
            } else {
                std::cout << "[Hôte] Échec du rechargement.\n" << std::endl;
            }
        }
    } else {
        if (_handle) {
            std::cout << "\n[Hôte] Le fichier du plugin a été supprimé. Déchargement..." << std::endl;
            unload();
            _last_mod_time = 0;
        }
    }
}

bool DLLoader::is_loaded() const
{
    return static_cast<bool>(_update_func);
}

DLLoader::PluginUpdateFunc DLLoader::get_function()
{
    return _update_func;
}

bool DLLoader::load()
{
    unload();
    _handle = dlopen(_library_path.c_str(), RTLD_LAZY);
    if (!_handle) {
        std::cerr << "[Hôte] Erreur lors du chargement de la bibliothèque : " << dlerror() << std::endl;
        return false;
    }

    void (*func_ptr)(State*) = reinterpret_cast<void (*)(State*)>(dlsym(_handle, "plugin_update"));
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        std::cerr << "[Hôte] Erreur lors de la recherche du symbole 'plugin_update': " << dlsym_error << std::endl;
        unload();
        return false;
    }

    _update_func = func_ptr;
    return true;
}

void DLLoader::unload()
{
    if (_handle) {
        dlclose(_handle);
        _handle = nullptr;
        _update_func = nullptr;
    }
}