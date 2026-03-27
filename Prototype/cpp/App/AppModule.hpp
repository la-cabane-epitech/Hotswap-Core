#pragma once

#include "App/IModule.hpp"
#include <string>

class AppModule : public IModule {
private:
    // L'état persistant est maintenant directement membre de la classe.
    int update_count = 0;
    std::string module_name = "Default Name";
    int health = 100;
    int mana = 50;

public:
    // La logique de sérialisation est maintenant une méthode de cette classe.
    // Bitsery la trouvera automatiquement.
    template <typename S>
    void serialize(S& s) {
        s.value4b(update_count);
        s.text1b(module_name, 50);
        s.value4b(health);
        s.value4b(mana);
    }

    AppModule();
    ~AppModule() override;

    void update() override;

    std::vector<char> serialize() override;
    void deserialize(const std::vector<char>& state) override;
};