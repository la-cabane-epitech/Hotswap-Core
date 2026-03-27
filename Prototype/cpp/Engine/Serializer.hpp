#pragma once

#include <vector>

// Forward declaration de la structure de données pour éviter les dépendances circulaires.
class AppModule;

namespace Serializer {

    // Sérialise un objet AppModule dans un buffer.
    std::vector<char> saveState(const AppModule& module);

    // Désérialise un buffer dans un objet AppModule et retourne `true` en cas de succès.
    bool loadState(AppModule& module, const std::vector<char>& buffer);

} // namespace Serializer