/*
** EPITECH PROJECT, 2025
** cpp
** File description:
** IModule.hpp
*/

#ifndef IMODULE_HPP_
#define IMODULE_HPP_

#include <vector>

class IModule {
    public:
        virtual ~IModule() = default;
        virtual void update() = 0;

        // Sérialise l'état interne du module dans un blob de données.
        virtual std::vector<char> serialize() = 0;
        // Restaure l'état du module à partir d'un blob de données.
        virtual void deserialize(const std::vector<char>& state) = 0;
};

typedef IModule* (*CreateModuleFunc)();

#endif /* !IMODULE_HPP_ */