# Hotswap-Core

Outil de **hot reloading** pour C++ : recharge à chaud une bibliothèque partagée
dans un programme qui tourne, sans le redémarrer et **sans lui faire perdre son
état de session** — y compris lorsque la structure de cet état change entre deux
versions.

Le hot reload seul est un mécanisme connu, et beaucoup de projets le font déjà à
la main. Ce que cet outil apporte est la **conservation de l'état à travers le
rechargement** : un snapshot auto-descriptif transfère les valeurs de l'ancienne
version vers la nouvelle, champ par champ, même quand la struct a changé de
layout — sans quoi le rechargement fait perdre exactement ce qu'il devait
préserver.

**Il n'y a pas d'étape de validation avant adoption.** Un candidat qui compile
et charge est promu directement. Un plugin qui plante ou boucle une fois
réellement appelé fait tomber l'application pour de vrai — ce choix concentre
l'effort sur la promesse centrale plutôt que sur un filet de sécurité autour.

Le dépôt contient trois composants, reliés par un contrat figé :

- `include/hotswap/abi.hpp` — le contrat binaire entre le Runtime et tout plugin, voir [docs/abi.md](docs/abi.md)
- `src/filewatcher/` — surveille les sources du plugin et les recompile en candidat
- `src/host/` — le Runtime : détient l'état sans en connaître le type, adopte le candidat dès qu'il est détecté
- `src/plugin/` — plugin de démonstration, rechargé à chaud
- `tests/` — tests du Runtime contre de vraies bibliothèques partagées

L'outil s'adresse aux projets qui ont déjà une frontière de rechargement, ou
peuvent en isoler une rapidement, et dont l'état de session est cher à
reconstruire — jeu vidéo, simulation, robotique. Voir
[docs/architecture.md](docs/architecture.md) pour les critères exacts et le coût
d'adoption.

## Prérequis

| Outil | Version | Vérifier |
|---|---|---|
| CMake | ≥ 3.16 | `cmake --version` |
| Un compilateur C++17 | GCC, Clang ou AppleClang | `c++ --version` |

Le projet est développé sous Linux et macOS. Le compilateur et le suffixe de
bibliothèque (`.so` / `.dylib`) sont détectés par CMake, rien n'est codé en dur.
Windows n'est pas supporté : le chargeur repose sur `dlfcn.h`.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake

# Arch Linux
sudo pacman -S base-devel cmake

# macOS
xcode-select --install && brew install cmake
```

## Build & lancement

Le build est géré par CMake et se lance depuis la racine du dépôt :

```bash
./build.sh
ctest --test-dir build --output-on-failure   # tests, aussi lancés par la CI
```

Les binaires sont placés à la racine. Le Runtime et le Watcher sont deux processus
distincts, à lancer dans deux terminaux :

```bash
# terminal 1 — l'application hôte, qui détient l'état de session
./main

# terminal 2 — le watcher, qui recompile les sources du plugin en candidat
./FileWatcher
```

Le watcher surveille `src/plugin/` par défaut ; un autre dossier peut être passé en
argument. Le chemin de la bibliothèque, le compilateur et le suffixe de plateforme
sont fournis par CMake à la compilation — il n'y a plus de `.so` ni de `g++` codés
en dur.

Modifier `src/plugin/plugin.cpp` déclenche alors le cycle complet :

```
[Build]   plugin.cpp changed, building candidate...
[Build]   Candidate published.
[Runtime] Candidate detected, promoting.
[Runtime] Swap done, session state kept as is.
[Plugin]  counter = 8           ← reprend où il en était, il n'est pas reparti de zéro
```

Les messages du programme sont en anglais, la documentation reste en français.

### Ce qui se passe si le plugin plante

Il n'y a pas d'étape de validation avant adoption : un candidat qui compile et
charge est promu directement. Casser volontairement le plugin, les deux
processus étant lancés, illustre les deux issues possibles :

| Ce qu'on écrit dans `plugin_update` | Ce que fait le pipeline |
|---|---|
| `state->no_such_field = 1;` | `[Build] FAILED (exit 1)` + l'erreur du compilateur affichée — **l'hôte survit**, rien n'est jamais promu |
| `int *p = nullptr; *p = 42;` (ou `while (true) {}`) | Compile, se charge, et plante l'hôte **pour de vrai** au premier appel réel |

Tous les artefacts de runtime — bibliothèque active, candidat et logs — sont
regroupés dans `.hotswap/`, à la racine du dépôt :

```
.hotswap/
├── libplugin.dylib            # dernière version promue, chargée au démarrage du Runtime
├── libplugin.dylib.candidate  # candidat en attente de promotion
├── libplugin.dylib.gen<N>     # copie chargée par le Runtime pour le swap N
└── build.log                  # stderr du compilateur
```

Le dossier est ignoré par git dans son ensemble ; le suffixe dépend de la
plateforme (`.so` sur Linux, `.dylib` sur macOS).

### État d'implémentation

Le hot reload fonctionne, sans filet de sécurité autour de l'exécution. Le
contrat de [abi.md](docs/abi.md) est figé et le Runtime le respecte entièrement
([DLLoader.cpp](src/host/DLLoader.cpp)) : l'hôte ne connaît plus le type de
l'état, un changement de layout passe par un snapshot, et un candidat qui ne se
charge pas ou refuse le snapshot est jeté sans toucher à la version active. Un
canari existait et a été retiré pour concentrer l'effort sur la promesse
centrale — voir *Historique* dans [protocole.md](docs/protocole.md).

**Le cœur du projet reste à finir.** La sérialisation avec remapping par nom de
champ ([etat.md](docs/etat.md)) n'est pas codée : le plugin de démo n'a que des
bouchons, donc un changement de layout remet l'état à ses valeurs par défaut. Le
protocole de statut par fichier JSON ([protocole.md](docs/protocole.md)) n'est
pas codé non plus. La répartition de ce travail est dans
[chantiers.md](docs/chantiers.md).

## Documentation

| Document | Contenu |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Périmètre, à qui ça s'adresse, coût du portage, pattern de frontière |
| [docs/abi.md](docs/abi.md) | Contrat binaire plugin/hôte : symboles exportés, signatures, qui appelle quoi et quand |
| [docs/protocole.md](docs/protocole.md) | Pipeline Build → Swap, machine à états, format de statut |
| [docs/etat.md](docs/etat.md) | Persistance de l'état, sérialisation, remapping de structure |
| [docs/MoSCoW.md](docs/MoSCoW.md) | Périmètre fonctionnel : Must / Should / Could / Won't |
| [docs/chantiers.md](docs/chantiers.md) | Répartition du travail en parallèle, règles communes |

La référence de l'API est générée par Doxygen à chaque push sur `main` et publiée
sur GitHub Pages.
