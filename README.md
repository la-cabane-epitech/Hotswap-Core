# Hotswap-Core

Outil de **Hot Reloading** pour C++ : recompile et recharge à chaud des bibliothèques
partagées (`.so`) sans redémarrer le programme hôte, pour éviter les longs cycles de
recompilation sur de gros projets.

Le depot contient actuellement un prototype C++ avec trois composants :

- `src/filewatcher/` : surveille un dossier et déclenche une recompilation quand un fichier
  `.cpp` est créé/modifié.
- `src/host/` : programme hôte (`main.cpp`) qui charge dynamiquement un plugin (`libplugin.so`)
  via `DLLoader`, et le recharge quand la lib change sur le disque.
- `src/plugin/` : plugin C++ chargé dynamiquement par le programme hôte.

## Périmètre

Hotswap-Core cible les projets qui **adoptent son architecture plugin** : le code
rechargé vit dans une bibliothèque partagée derrière une frontière `extern "C"`, et
l'état persistant est possédé par le programme hôte, pas par le plugin. L'outil
n'est pas applicable tel quel à une codebase existante sans restructuration.

Recharger du C++ arbitraire dans un process sans toucher à son architecture, ou
inférer automatiquement le remapping d'une struct depuis les informations de debug,
sont hors périmètre — voir [docs/MoSCoW.md](docs/MoSCoW.md).

Le hot reload en lui-même est un mécanisme connu, et le prototype le fait déjà. Ce
que le projet apporte est le **filet de sécurité autour** : un rechargement à chaud
qui ne peut pas faire perdre l'état de la session, même quand le code rechargé est
faux. C'est l'objet du protocole décrit plus bas.

## Prérequis

Le projet cible **Linux** (utilise `dlfcn.h` pour le chargement dynamique et
`system()` pour piloter la recompilation) et utilise CMake pour le build.

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

Le build est gere par CMake et peut etre lance depuis la racine du depot :

```bash
./build.sh
```

Le script genere `build/` pour les fichiers CMake et place les binaires du prototype
a la racine du depot :

```bash
# Lancer le FileWatcher et le programme hote
./FileWatcher ./main
```

Une fois lancé, modifier `plugin.cpp` déclenche automatiquement sa recompilation en
`libplugin.so`, rechargée par `main` sans interruption du processus.

## Protocole de statut (inter-composants)

Le pipeline complet (**Watcher/Build → Sandbox → Runtime**, plus **Reporting** en
observateur) tourne dans des processus qui ne partagent pas de mémoire — à la seule
exception de la Sandbox, enfant `fork()` du Runtime, détaillée plus bas. Ils
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

### Ce que valide la Sandbox

L'étape s'appelle *Sandbox* dans le protocole (et ses états restent préfixés
`sandbox_`), mais ce qu'elle exécute est un **canari**, pas la suite de tests du
projet :

```
fork()
 └─ dlopen(candidate)              # symboles manquants ou incompatibles
 └─ plugin_migrate(copie de l'état)  # si la version d'état a changé
 └─ plugin_update(...) × N          # sous alarm(timeout_ms)
 └─ _exit(0)

waitpid() côté parent :
  exit 0            → sandbox_passed
  SIGSEGV / SIGABRT → sandbox_failed
  SIGALRM           → sandbox_timeout
```

**Ce `fork()` est fait par le Runtime, pas par un processus tiers.** C'est la seule
façon d'obtenir une copie *copy-on-write* de l'état vivant : cet état n'existe que
dans la mémoire du Runtime, et c'est précisément contre lui qu'il faut valider le
candidat — un plugin qui passe sur un état par défaut et segfault sur l'état réel
de la session ne vaut rien. L'enfant s'exécute donc sur les vraies données sans
pouvoir corrompre celles du parent.

La Sandbox est donc une **étape** du pipeline, pas un quatrième programme : elle
s'exécute dans un enfant du Runtime, déclenchée quand celui-ci lit `build_ok`. Le
champ `producer` du statut nomme l'étape responsable, pas le processus qui a
appelé `write()` — les états `sandbox_*` portent `producer: "sandbox"` bien qu'ils
soient écrits par le Runtime après son `waitpid()`.

Ce canari couvre les trois classes de fautes qu'un compilateur ne peut pas signaler
et qui tuent le processus hôte — segfault, boucle infinie, symbole manquant — plus
une migration d'état fautive, qui plante exactement comme du code de plugin fautif.
Il coûte quelques millisecondes et ne demande au projet utilisateur aucun test.

**Les erreurs de compilation ne passent pas par la Sandbox.** Un build raté est
détecté au code de retour du compilateur, produit `build_failed`, et court-circuite
le pipeline : il n'y a pas de `.so` à valider. Le stderr du compilateur relève du
Reporting.

Deux limites connues : `fork()` ne clone que le thread appelant, donc l'enfant est
un environnement dégradé si l'hôte est multi-threadé ; et les descripteurs de
fichiers, sockets ou contextes GPU ne survivent pas à la duplication.

L'exécution de la suite de tests du projet sur le candidat reste possible en option
explicite, jamais par défaut : à chaque sauvegarde, elle ajouterait au cycle la
latence que l'outil cherche justement à supprimer.

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
| `sandbox_failed` | `reason` (`dlopen` / `symbol` / `migrate` / `signal` / `exit_code`), `signal`, `exit_code` |
| `sandbox_passed` | `duration_ms` |
| `swapped` | `previous_active_path`, `state_version_from`, `state_version_to` |
| `rolled_back` | `cause_state` — l'état qui a déclenché le rollback |

Les valeurs de `reason` suivent les étapes du canari décrites plus haut :
`dlopen` et `symbol` pour un candidat qui ne se charge pas, `migrate` pour une
migration d'état fautive, `signal` et `exit_code` pour une faute pendant les appels
au point d'entrée.

### Migration de l'état

Quand la version d'état déclarée par le candidat diffère de celle du plugin actif,
le Runtime alloue un buffer à la nouvelle taille, appelle le hook de migration du
plugin, puis libère l'ancien — le tout pendant le swap, après `sandbox_passed`.

Cette migration n'a pas d'état dédié dans la machine ci-dessus, volontairement :
elle est atomique du point de vue d'un lecteur du statut, et un état intermédiaire
ne serait observable que quelques microsecondes pour le coût d'une écriture
fichier. Elle est tracée dans le `detail` de `swapped` via `state_version_from` et
`state_version_to`. Un échec de migration est déjà attrapé en amont par le canari
(`sandbox_failed`, `reason: "migrate"`), et converge donc vers `rolled_back` comme
n'importe quel autre échec.

### Qui écrit, qui lit

| Étape | Processus | Écrit | Lit |
|---|---|---|---|
| Watcher / Build | Watcher | `building` → `build_ok` / `build_failed` | — |
| Sandbox | enfant du Runtime | `sandbox_running` → `sandbox_passed` / `_failed` / `_timeout` | `build_ok` |
| Runtime / DLLoader | Runtime | `swapped` / `rolled_back` | `sandbox_passed`, tout état d'échec |
| Reporting | Reporting | rien (pur observateur) | tout |

Il n'y a donc que **trois programmes** — Watcher, Runtime, Reporting — pour quatre
étapes : la Sandbox vit dans un enfant éphémère du Runtime, pour la raison exposée
plus haut.

### Règles

1. **Écriture atomique toujours** — `.tmp` + `rename()`, jamais de write direct sur `*.status.json`.
2. **Détection par polling du mtime** — réutilise le pattern déjà présent dans `DLLoader::reload_if_changed`.
3. **Un seul statut à la fois** — le fichier contient le dernier état connu, pas un historique (l'historique complet vit dans `plugin.log`).
4. **Jamais de swap sans `sandbox_passed`** — le Runtime ignore tout candidat dont le statut n'est pas explicitement ce state.
