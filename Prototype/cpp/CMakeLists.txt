cmake_minimum_required(VERSION 3.10)
project(HotReloadEngine CXX)

# On impose le C++17 (très pratique pour les features modernes des moteurs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. LE MODULE (La librairie dynamique)
# ==========================================
# Le mot-clé SHARED indique à CMake de générer un .so (et ajoute -fPIC automatiquement)
add_library(app_module SHARED
    App/AppModule.cpp
    Engine/Serializer.cpp
)

# On force CMake à placer "libmodule.so" dans un sous-dossier "Module/" 
# à l'intérieur du dossier de build, pour que ton dlopen("./Module/libmodule.so") fonctionne.
set_target_properties(app_module PROPERTIES
    PREFIX "libapp_module"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Module"
)
# On ajoute le dossier des bibliothèques aux chemins d'inclusion pour la cible 'module'
target_include_directories(app_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs ${CMAKE_CURRENT_SOURCE_DIR})


# ==========================================
# 2. LE CORE (L'exécutable principal)
# ==========================================
add_executable(core
    main.cpp
    Engine/ModuleManager.cpp
    Engine/FileWatcher.cpp
)

target_include_directories(core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# On lie la librairie système "dl" pour dlopen, dlsym, etc.
# ${CMAKE_DL_LIBS} est une variable magique de CMake qui marche sur Linux et macOS
target_link_libraries(core PRIVATE ${CMAKE_DL_LIBS})

# On place l'exécutable à la racine du dossier de build
set_target_properties(core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)