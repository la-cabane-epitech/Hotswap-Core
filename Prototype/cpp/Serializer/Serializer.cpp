#include "Serializer.hpp"
#include "App/AppState.hpp" // Nous avons besoin de la définition complète ici.

#include <iostream>
#include <cstdint>

// Les dépendances à Bitsery sont maintenant contenues dans ce fichier.
#include <bitsery/bitsery.h>
#include <bitsery/adapter/buffer.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/string.h>
#include <bitsery/serializer.h>
#include <bitsery/deserializer.h>

// La description de la sérialisation est maintenant une fonction libre.
// Bitsery la trouvera car elle opère sur AppState qui est dans le namespace global.
template <typename S>
void serialize(S& s, AppState& data) { // C'est la sérialisation pour la V2
    s.value4b(data.update_count);
    s.text1b(data.module_name, 50);
    s.value4b(data.health);
    s.value4b(data.mana);
}

namespace Serializer {

    // Définissez la version actuelle de votre structure de données.
    // Incrémentez-la chaque fois que vous apportez une modification non rétrocompatible à ModuleState.
    constexpr uint32_t CURRENT_STATE_VERSION = 2;

    // Fonction de migration pour lire un état V1 et le convertir en V2
    void migrate_v1_to_v2(bitsery::Deserializer<bitsery::InputBufferAdapter<const std::vector<char>>>& d, AppState& state) {
        d.value4b(state.update_count);
        d.text1b(state.module_name, 50);
        float old_health = 0.0f;
        d.value4b(old_health);
        state.health = static_cast<int>(old_health); // On convertit le float en int
        // 'mana' conservera sa valeur par défaut (50) car il n'existait pas en V1.
    }

    std::vector<char> saveState(const AppState& state) {
        std::vector<char> buffer;
        // On utilise le Sérialiseur complet pour écrire un en-tête de version.
        // On doit spécifier le type de l'adaptateur pour le Serializer car la déduction de template (CTAD) échoue.
        using Adapter = bitsery::OutputBufferAdapter<std::vector<char>>;
        bitsery::Serializer<Adapter> s{Adapter(buffer)};

        s.value4b(CURRENT_STATE_VERSION);
        s.object(state);
        s.adapter().flush();

        auto writtenSize = s.adapter().writtenBytesCount();
        std::cout << "[Serializer] État sauvegardé (version " << CURRENT_STATE_VERSION << ", " << writtenSize << " octets)." << std::endl;
        return buffer;
    }

    bool loadState(AppState& state, const std::vector<char>& buffer) {
        // On doit également spécifier le type de l'adaptateur pour le Deserializer.
        using Adapter = bitsery::InputBufferAdapter<const std::vector<char>>;
        bitsery::Deserializer<Adapter> d{Adapter(buffer.begin(), buffer.end())};

        uint32_t buffer_version = 0;
        d.value4b(buffer_version);

        if (buffer_version > CURRENT_STATE_VERSION) {
            std::cerr << "[Serializer] Erreur: L'état sauvegardé provient d'une version plus récente du module." << std::endl;
            return false;
        }

        if (buffer_version == 2) {
            d.object(state);
        } else if (buffer_version == 1) {
            std::cout << "[Serializer] Migration de l'état v1 -> v2..." << std::endl;
            migrate_v1_to_v2(d, state);
        }

        auto result = d.adapter().error();
        bool success = result == bitsery::ReaderError::NoError;
        if (success) {
            std::cout << "[Serializer] État chargé (version " << buffer_version << "). Compteur: " << state.update_count << std::endl;
        } else {
            std::cerr << "[Serializer] Erreur lors du chargement de l'état." << std::endl;
        }
        return success;
    }

}