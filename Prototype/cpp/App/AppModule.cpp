/*
** EPITECH PROJECT, 2025
** cpp
** File description:
** Module.cpp
*/
#include "AppModule.hpp" // On inclut notre propre en-tête
#include "Engine/Serializer.hpp"
#include <iostream>

AppModule::AppModule() {
    std::cout << "[Module] Instance créée (constructeur)." << std::endl;
}

AppModule::~AppModule() {
    std::cout << "[Module] Instance détruite (destructeur)." << std::endl;
}

void AppModule::update() {
    update_count++;
    if (update_count == 1) module_name = "Module Actif";
    health -= 5;
    std::cout << "[Module] " << module_name << " - Update N°" << update_count << " - Health: " << health << std::endl;
}

std::vector<char> AppModule::serialize() {
    return Serializer::saveState(*this);
}

void AppModule::deserialize(const std::vector<char>& state) {
    Serializer::loadState(*this, state);
}

extern "C" IModule* create_module() {
    return new AppModule();
}