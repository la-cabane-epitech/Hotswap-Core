/*
** EPITECH PROJECT, 2026
** PoC_CPP
** File description:
** Core
*/

#pragma once

#include "FileWatcher.hpp"
#include "SandboxRunner.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

class Core {
public:
    Core() = default;
    ~Core() = default;

    void onFileChanged(const std::string& path, FileStatus status) {
        if (std::filesystem::path(path).extension() != ".cpp") {
            return;
        }

        if (is_host_source(std::filesystem::path(path).filename().string())) {
            // main.cpp / DLLoader.cpp appartiennent au binaire hôte, pas au
            // module rechargé : les ignorer évite de les compiler en plugin.
            return;
        }

        if (status == FileStatus::created || status == FileStatus::modified) {
            std::cout << "[Core] " << (status == FileStatus::created ? "Fichier créé: " : "Fichier modifié: ") << path << ". Recompilation du module..." << std::endl;

            std::vector<std::string> sources = collect_plugin_sources();
            if (sources.empty()) {
                std::cout << "[Core] Aucune source de plugin trouvée." << std::endl;
                return;
            }

            std::string build_cmd = "g++ -std=c++17 -shared -fPIC -o libplugin.so.candidate.tmp";
            for (const auto& src : sources) {
                build_cmd += " " + src;
            }
            build_cmd += " && mv libplugin.so.candidate.tmp libplugin.so.candidate";

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

private:
    // Convention (documentée dans le README racine) : le module "plugin" est
    // composé de tous les .cpp du dossier surveillé, sauf ceux qui composent
    // le binaire hôte lui-même.
    static bool is_host_source(const std::string& filename) {
        static const std::vector<std::string> host_sources = {"main.cpp", "DLLoader.cpp"};
        return std::find(host_sources.begin(), host_sources.end(), filename) != host_sources.end();
    }

    static std::vector<std::string> collect_plugin_sources() {
        std::vector<std::string> sources;
        for (auto& entry : std::filesystem::recursive_directory_iterator(".")) {
            if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
                continue;
            }
            if (is_host_source(entry.path().filename().string())) {
                continue;
            }
            sources.push_back(entry.path().string());
        }
        std::sort(sources.begin(), sources.end()); // ordre déterministe entre deux builds
        return sources;
    }
};
