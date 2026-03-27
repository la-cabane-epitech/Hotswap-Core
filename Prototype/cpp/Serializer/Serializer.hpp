#pragma once

#include <vector>

// Forward declaration de la structure de données pour éviter les dépendances circulaires.
struct ModuleState;

namespace Serializer {

    // Sérialise un objet ModuleState dans un buffer.
    std::vector<char> saveState(const ModuleState& state);

    // Désérialise un buffer dans un objet ModuleState et retourne `true` en cas de succès.
    bool loadState(ModuleState& state, const std::vector<char>& buffer);

} // namespace Serializer