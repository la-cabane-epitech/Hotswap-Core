# Hotswap-Core

Outil de **Hot Reloading** pour C++ : recompile et recharge à chaud des bibliothèques
partagées (`.so`) sans redémarrer le programme hôte, pour éviter les longs cycles de
recompilation sur de gros projets.

Le dépôt contient actuellement un prototype (`Prototype/CPP/`) avec deux briques :

- `FileWatcher/` : surveille un dossier et déclenche une recompilation quand un fichier
  `.cpp` est créé/modifié.
- `test1/` : programme hôte (`main.cpp`) qui charge dynamiquement un plugin (`libplugin.so`)
  via `DLLoader`, et le recharge quand la lib change sur le disque.
- `Sandbox/` : valide un `.so` candidat dans un process isolé (fork + timeout) avant de
  le promouvoir en `libplugin.so` — voir `Prototype/CPP/Sandbox/README`.

## Prérequis

Le projet cible **Linux** (utilise `dlfcn.h` pour le chargement dynamique et
`system()` pour piloter la recompilation).

| Outil | Version minimale | Vérifier |
|---|---|---|
| `g++` (GCC) | supportant C++17 | `g++ --version` |
| `make` (optionnel, pas encore utilisé par le prototype) | — | `make --version` |

Sur cette machine (Arch Linux), tout est déjà installé :

```
g++ (GCC) 16.2.1
gcc (GCC) 16.2.1
GNU Make
```

### Installer les prérequis si besoin

```bash
# Arch Linux
sudo pacman -S base-devel

# Debian / Ubuntu
sudo apt install build-essential

# Fedora
sudo dnf groupinstall "Development Tools"
```

`base-devel` / `build-essential` fournissent `g++`, `gcc`, `make` et les headers
standards (dont `dlfcn.h`, utilisé pour `dlopen`/`dlsym`/`dlclose`).

## Build & lancement du prototype

Il n'y a pas encore de `Makefile`/`CMakeLists.txt` : la compilation se fait à la main
(voir `Prototype/CPP/test1/README`).

```bash
cd Prototype/CPP

# 1. Compiler le FileWatcher (il embarque directement la validation sandbox,
#    d'où -I../Sandbox et -ldl), puis le déplacer à côté du programme hôte
cd FileWatcher
g++ -std=c++17 -Wall -Wextra -pedantic -I../Sandbox \
    main.cpp ../Sandbox/SandboxRunner.cpp ../Sandbox/StatusWriter.cpp \
    -o FileWatcher -ldl && mv ./FileWatcher ../test1
cd ../test1

# 2. Compiler le programme hôte
g++ -std=c++17 -o main main.cpp DLLoader.cpp -ldl

# 3. Compiler la lib plugin de base (sera rechargée à chaud à chaque modification)
g++ -std=c++17 -shared -fPIC -o libplugin.so plugin.cpp

# 4. Lancer : le FileWatcher surveille le dossier, build/valide/promeut, et
#    pilote le programme hôte
./FileWatcher ./main
```

`Sandbox/` reste aussi compilable en CLI standalone (`Sandbox/README`) pour tester
la validation d'un candidat sans passer par tout le pipeline FileWatcher.

Une fois lancé, modifier `plugin.cpp` déclenche : recompilation en `libplugin.so.candidate`,
validation par `sandbox_runner` dans un process isolé (avec timeout), puis — seulement si
elle passe — promotion en `libplugin.so`, rechargée par `main` sans interruption du
processus. Un candidat qui plante, boucle ou dépasse le timeout est rejeté sans jamais
toucher à `libplugin.so` : le host continue de tourner sur la dernière version valide.

## Protocole de statut (inter-composants)

Le pipeline complet (**Watcher/Build → Sandbox → Runtime**, plus **Reporting** en
observateur) tourne dans des processus séparés qui ne partagent pas de mémoire. Ils
communiquent par un fichier de statut par module, pas par IPC directe — même logique
que le polling déjà utilisé par `DLLoader::reload_if_changed` pour détecter un `.so`
modifié.

### Machine à états

```
building → build_ok → sandbox_running → sandbox_passed → swapped
   │             │            │
build_failed  sandbox_timeout sandbox_failed
   └──────────────┴───────────────┘
                   │
              rolled_back
```

Tout échec, à n'importe quelle étape, converge vers `rolled_back` : le Runtime ne
swap jamais sans être passé par `sandbox_passed`.

### Fichiers

```
build/
├── plugin.so             # version active, chargée par le Runtime
├── plugin.so.candidate   # nouvelle version en attente de validation
├── plugin.status.json    # source de vérité du pipeline pour ce module
└── plugin.log            # stderr compil + stdout/stderr sandbox, concaténés
```

`plugin.status.json` s'écrit comme le `.so` : sur un `.tmp`, puis `rename()` — jamais
en place, pour qu'aucun lecteur ne tombe sur un JSON à moitié écrit.

### Format de `plugin.status.json`

```json
{
  "schema_version": 1,
  "module": "plugin",
  "state": "sandbox_failed",
  "producer": "sandbox",
  "timestamp": "2026-08-25T14:32:10Z",
  "candidate_path": "build/plugin.so.candidate",
  "active_path": "build/plugin.so",
  "detail": { "reason": "signal", "signal": "SIGSEGV" },
  "log_path": "build/plugin.log"
}
```

| Champ | Type | Rôle |
|---|---|---|
| `schema_version` | int | Fait évoluer le format sans casser les lecteurs existants |
| `module` | string | Nom du plugin concerné (utile dès le multi-modules) |
| `state` | enum | Le champ pivot — un des états de la machine ci-dessus |
| `producer` | enum | `watcher` / `sandbox` / `runtime` — qui a écrit ce statut |
| `timestamp` | ISO 8601 UTC | Horodatage de la dernière transition |
| `candidate_path` / `active_path` | string | Chemins des deux `.so` du module |
| `detail` | objet libre | Forme différente selon `state`, voir ci-dessous |
| `log_path` | string | Chemin du log complet |

`detail` selon l'état :

| État | Champs de `detail` |
|---|---|
| `build_failed` | `compiler_exit_code`, `stderr_excerpt` |
| `sandbox_timeout` | `timeout_ms` |
| `sandbox_failed` | `reason` (`signal` / `exit_code` / `assertion`), `signal`, `exit_code` |
| `sandbox_passed` | `duration_ms` |
| `swapped` | `previous_active_path` |
| `rolled_back` | `cause_state` — l'état qui a déclenché le rollback |

### Qui écrit, qui lit

| Composant | Écrit | Lit |
|---|---|---|
| Watcher / Build | `building` → `build_ok` / `build_failed` | — |
| Sandbox | `sandbox_running` → `sandbox_passed` / `_failed` / `_timeout` | `build_ok` |
| Runtime / DLLoader | `swapped` / `rolled_back` | `sandbox_passed`, tout état d'échec |
| Reporting | rien (pur observateur) | tout |

### Règles

1. **Écriture atomique toujours** — `.tmp` + `rename()`, jamais de write direct sur `*.status.json`.
2. **Détection par polling du mtime** — réutilise le pattern déjà présent dans `DLLoader::reload_if_changed`.
3. **Un seul statut à la fois** — le fichier contient le dernier état connu, pas un historique (l'historique complet vit dans `plugin.log`).
4. **Jamais de swap sans `sandbox_passed`** — le Runtime ignore tout candidat dont le statut n'est pas explicitement ce state.
