#include "Serializer.hpp"
#include "App/AppModule.hpp"

#include <iostream>
#include <cstdint>

#include <bitsery/bitsery.h>
#include <bitsery/adapter/buffer.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/string.h>
#include <bitsery/serializer.h>
#include <bitsery/deserializer.h>

namespace Serializer {

    constexpr uint32_t CURRENT_STATE_VERSION = 3; // Nouvelle version de l'architecture

    std::vector<char> saveState(const AppModule& module) {
        std::vector<char> buffer;
        using Adapter = bitsery::OutputBufferAdapter<std::vector<char>>;
        bitsery::Serializer<Adapter> s{Adapter(buffer)};

        s.value4b(CURRENT_STATE_VERSION);
        s.object(module);
        s.adapter().flush();

        auto writtenSize = s.adapter().writtenBytesCount();
        std::cout << "[Serializer] État sauvegardé (version " << CURRENT_STATE_VERSION << ", " << writtenSize << " octets)." << std::endl;
        return buffer;
    }

    bool loadState(AppModule& module, const std::vector<char>& buffer) {
        // On doit également spécifier le type de l'adaptateur pour le Deserializer.
        using Adapter = bitsery::InputBufferAdapter<const std::vector<char>>;
        bitsery::Deserializer<Adapter> d{Adapter(buffer.begin(), buffer.end())};

        uint32_t buffer_version = 0;
        d.value4b(buffer_version);

        if (buffer_version > CURRENT_STATE_VERSION) {
            std::cerr << "[Serializer] Erreur: L'état sauvegardé provient d'une version plus récente du module." << std::endl;
            return false;
        }

        // Pour l'instant, on ne gère que la version actuelle.
        if (buffer_version == CURRENT_STATE_VERSION) {
            d.object(module);
        } else {
            std::cerr << "[Serializer] Version de l'état incompatible (" << buffer_version << "), attendue (" << CURRENT_STATE_VERSION << "). L'état n'est pas chargé." << std::endl;
            return false;
        }

        auto result = d.adapter().error();
        bool success = result == bitsery::ReaderError::NoError;
        if (success) {
            std::cout << "[Serializer] État chargé (version " << buffer_version << ")." << std::endl;
        } else {
            std::cerr << "[Serializer] Erreur lors du chargement de l'état." << std::endl;
        }
        return success;
    }

}