# Hotswap-Core

Outil de **hot reloading** pour C++ : recharge à chaud une bibliothèque partagée
dans un programme qui tourne, sans le redémarrer et **sans lui faire perdre son
état de session**.

Le hot reload seul est un mécanisme connu. Ce que ce projet apporte est le filet
de sécurité autour : avant tout échange, la nouvelle version est exécutée par un
**canari** — un processus enfant jetable. Si elle plante, boucle à l'infini ou ne
compile pas, l'application continue sur la dernière version valide, sans rien
perdre.

Le dépôt contient trois composants :

- `src/filewatcher/` — surveille les sources du plugin et les recompile en candidat
- `src/host/` — le Runtime : valide le candidat par canari, l'adopte ou le rejette
- `src/plugin/` — plugin de démonstration, rechargé à chaud

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
Windows n'est pas supporté : le chargeur repose sur `dlfcn.h` et le canari sur
`fork()`.

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
[Build]   Candidate published, waiting for Runtime validation.
[Runtime] Candidate detected, running canary...
[Runtime] Canary passed.
[Runtime] Swap done, session state preserved.
[Plugin]  counter = 8           ← reprend où il en était, il n'est pas reparti de zéro
```

Les messages du programme sont en anglais, la documentation reste en français.

### Vérifier le filet de sécurité

Le comportement qui distingue l'outil se constate en cassant volontairement le
plugin, les deux processus étant lancés. Dans les trois cas, **l'hôte survit et
continue sur la dernière version valide** :

| Ce qu'on écrit dans `plugin_update` | Ce que fait le pipeline |
|---|---|
| `int *p = nullptr; *p = 42;` | `[Runtime] Canary rejected (signal).` |
| `while (true) {}` | `[Runtime] Canary rejected (timeout).` |
| `state->no_such_field = 1;` | `[Build] FAILED (exit 1)` + l'erreur du compilateur affichée |

Tous les artefacts de runtime — bibliothèque active, candidat et logs — sont
regroupés dans `.hotswap/`, à la racine du dépôt :

```
.hotswap/
├── libplugin.dylib            # version active, chargée par le Runtime
├── libplugin.dylib.candidate  # candidat en attente de validation
├── libplugin.dylib.previous   # version précédente, pour le rollback
├── canary.log                 # sortie du candidat exécuté par le canari
└── build.log                  # stderr du compilateur
```

Le dossier est ignoré par git dans son ensemble ; le suffixe dépend de la
plateforme (`.so` sur Linux, `.dylib` sur macOS).

### État d'implémentation

Le pipeline **Build → Canari → Swap / Rollback** est fonctionnel.

Deux choses spécifiées dans `docs/` ne le sont pas encore. Le protocole de statut
par fichier JSON ([protocole.md](docs/protocole.md)) : les deux processus se
coordonnent aujourd'hui par la seule présence du fichier candidat, et le reporting
passe par la sortie standard. Et la sérialisation de l'état
([etat.md](docs/etat.md)) : l'état survit au rechargement, mais pas encore à un
changement de layout de la struct.

## Documentation

| Document | Contenu |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Périmètre, à qui ça s'adresse, coût du portage, pattern de frontière |
| [docs/protocole.md](docs/protocole.md) | Pipeline Build → Canari → Swap, machine à états, format de statut |
| [docs/etat.md](docs/etat.md) | Persistance de l'état, sérialisation, remapping de structure |
| [docs/MoSCoW.md](docs/MoSCoW.md) | Périmètre fonctionnel : Must / Should / Could / Won't |

La référence de l'API est générée par Doxygen à chaque push sur `main` et publiée
sur GitHub Pages.
