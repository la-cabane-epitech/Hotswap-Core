/*
** EPITECH PROJECT, 2026
** PoC_CPP
** File description:
** Core
*/

#pragma once

#include "FileWatcher.hpp"
#include <string>
#include <iostream>
#include <filesystem>
#include <cstdlib>

class Core {
public:
    Core() = default;
    ~Core() = default;

    void onFileChanged(const std::string& path, FileStatus status) {
        if (std::filesystem::path(path).extension() != ".cpp") {
            return;
        }

        if (status == FileStatus::created || status == FileStatus::modified) {
            std::cout << "[Core] " << (status == FileStatus::created ? "Fichier créé: " : "Fichier modifié: ") << path << ". Recompilation..." << std::endl;
            std::string cmd = "g++ -shared -fPIC -o libplugin.so.tmp " + path + " && mv libplugin.so.tmp libplugin.so";
            system(cmd.c_str());
        } else if (status == FileStatus::erased) {
            std::cout << "[Core] Fichier supprimé: " << path << std::endl;
        }
    }
};