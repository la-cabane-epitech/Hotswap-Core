/*
** EPITECH PROJECT, 2026
** PoC_CPP
** File description:
** Core
*/

#pragma once

#include "FileWatcher.hpp"
#include "SandboxRunner.hpp"
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

            std::string build_cmd = "g++ -std=c++17 -shared -fPIC -o libplugin.so.candidate.tmp " + path
                                   + " && mv libplugin.so.candidate.tmp libplugin.so.candidate";
            if (system(build_cmd.c_str()) != 0) {
                std::cout << "[Core] Échec de compilation, candidat rejeté." << std::endl;
                return;
            }

            bool ok = validate_candidate("plugin", "./libplugin.so.candidate", "./libplugin.so", 2000);
            if (!ok) {
                std::cout << "[Core] Candidat rejeté par la sandbox, version active inchangée." << std::endl;
            }
        } else if (status == FileStatus::erased) {
            std::cout << "[Core] Fichier supprimé: " << path << std::endl;
        }
    }
};