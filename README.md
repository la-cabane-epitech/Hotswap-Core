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

Hotswap-Core repose sur une **architecture plugin** : le code rechargé vit dans une
bibliothèque partagée derrière une frontière `extern "C"`, et l'état persistant est
possédé par le programme hôte, jamais par le plugin.

Le hot reload en lui-même est un mécanisme connu, et le prototype le fait déjà. Ce
que le projet apporte est le **filet de sécurité autour** : un rechargement à chaud
qui ne peut pas faire perdre l'état de la session, même quand le code rechargé est
faux. C'est l'objet du protocole décrit plus bas.

Et parce que cet état est sérialisable, il devient manipulable : snapshots nommés,
retour à un état antérieur, survie au crash de l'hôte, export d'une session pour
qu'un collègue reproduise le bug. La promesse passe de *« ton rechargement ne te
fera pas perdre ta session »* à *« ta session est un objet que tu manipules »*.

### À qui ça s'adresse

Ce pattern — code métier dans une lib rechargeable, état dans une struct possédée
par l'hôte — n'est pas une invention de ce projet. C'est déjà le standard dans le
jeu vidéo et la simulation, adopté **volontairement et sans outil**, parce que le
cycle recompiler / relancer / revenir dans le cas qu'on debug y coûte des minutes à
chaque itération. Même profil en robotique, en trading, sur tout process à longue
durée de vie dont l'état de démarrage est cher à reconstruire.

Pour ces projets, le coût d'adoption est proche de zéro : l'architecture est déjà
là. Ce qui leur manque est le filet — dans ces implémentations maison, un plugin qui
segfault emporte le process et la session avec lui.

Deux critères, donc : **une frontière de rechargement déjà en place ou isolable
rapidement**, et **un état de session cher à reconstruire**. Si l'un des deux
manque, l'outil n'apporte rien.

### Une couture, pas une refonte

L'outil n'exige pas que toute l'application soit en plugin. Il exige **une couture** :
un endroit où l'on itère souvent, et où le code est déjà séparable de la donnée.

Deux choses bougent au portage, et c'est la seconde qu'on sous-estime :

1. le code à recharger entre derrière la frontière ;
2. **l'état que ce code manipule doit en sortir**, pour remonter dans l'hôte.

Tout ce qui vit à l'intérieur du plugin est détruit à chaque rechargement — les
variables globales et statiques du plugin comprises, qui repartent silencieusement
à leur valeur initiale. Le refactor ne se propage donc pas le long du code, mais le
long des **données**.

D'où le critère de coût :

| Peu coûteux à porter | Coûteux |
|---|---|
| Une fonction d'update : `update(World&, float dt)` | Une classe qui possède un graphe d'objets profond |
| Un handler : `handle(Request&, Response&)` | Du RAII sur ressource : socket, fichier, contexte GPU |
| Une évaluation de règle métier, une stratégie | Un état interne accumulé sur la durée de la session |

De la logique qui opère sur des données qu'on lui passe se porte quasi gratuitement.
Un objet qui possède son monde est le refactor cher.

### Le pattern de frontière

Dans le cas le plus simple, le code rechargé est une fonction libre qui reçoit
l'état de l'hôte — c'est ce que fait le prototype avec `plugin_update`.

Quand le code est naturellement un objet, **l'instance ne se conserve pas d'une
version à l'autre** : le plugin exporte une fabrique, pas un objet vivant.

```c
extern "C" void* plugin_create(void* state);
extern "C" void  plugin_destroy(void* self);
```

L'hôte détruit l'ancienne instance *avant* le `dlclose`, recrée la nouvelle après le
`dlopen`, et lui repasse l'état qu'il possède. L'objet est jetable, la donnée
survit.

Ce n'est pas une préférence de style, c'est une contrainte : le pointeur de vtable
d'un objet dont la classe est définie dans le plugin pointe **dans le `.so`**. Si
l'hôte conserve un `std::unique_ptr<IStrategy>` sur cet objet à travers un
`dlclose`, le code est démappé sous ses pieds et le prochain appel virtuel saute
dans le vide — pas au moment du rechargement, mais plus tard et ailleurs, sans
rapport apparent avec la cause.

### Hors périmètre

Recharger du C++ arbitraire dans un process sans toucher à son architecture, et
inférer automatiquement le remapping d'une struct depuis les informations de debug.
Voir [docs/MoSCoW.md](docs/MoSCoW.md) pour les motifs.

Conséquence à assumer : **la valeur de l'outil est proportionnelle à la part des
itérations qui tombent dans la frontière.** Tout ce qui est modifié hors du plugin
impose toujours un redémarrage complet.

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

Le pipeline **Build → Canari → Swap / Rollback** est fonctionnel. Le protocole de
statut par fichier JSON décrit ci-dessous ne l'est pas encore : les deux processus
se coordonnent aujourd'hui par la seule présence du fichier candidat, et le
reporting passe par la sortie standard. La sérialisation de l'état (section
*Persistance et remapping*) n'est pas implémentée — l'état survit au rechargement,
mais pas encore à un changement de layout de la struct.

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
 └─ dlopen(candidate)               # symboles manquants ou incompatibles
 └─ plugin_state_load(snapshot)     # si la version d'état a changé
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
une désérialisation d'état fautive, qui plante exactement comme du code de plugin
fautif. Il coûte quelques millisecondes et ne demande au projet utilisateur aucun
test.

**Les erreurs de compilation ne passent pas par la Sandbox.** Un build raté est
détecté au code de retour du compilateur, produit `build_failed`, et court-circuite
le pipeline : il n'y a pas de `.so` à valider. Le stderr du compilateur relève du
Reporting.

### Canari n'est pas sandbox

Malgré le nom de l'étape, l'enfant n'est **pas** un environnement contraint. Les
deux notions répondent à des questions différentes :

| | Question à laquelle ça répond |
|---|---|
| **Sandbox** | Qu'est-ce que ce code a le **droit** de faire ? |
| **Canari** | **Qui encaisse** s'il plante ? |

L'enfant a exactement les mêmes droits que le parent. Il n'est pas isolé, il est
jetable.

### Limites du canari

**Les effets de bord sont réels, et dupliqués.** C'est la limite la plus importante
et la plus facile à oublier. Si le code rechargé écrit un fichier de sauvegarde,
envoie une requête réseau, publie sur une socket ou touche une base de données, le
canari le fait **pour de vrai** — puis rapporte `sandbox_passed`, et le Runtime
recommence. L'action a lieu deux fois.

D'où la règle à respecter côté utilisateur : **le code placé derrière la frontière
doit être pauvre en effets de bord.** C'est le même critère que *code séparable de
la donnée*, vu sous un autre angle. Une contention réelle — filtres `seccomp` sur
les écritures et le réseau dans l'enfant — reste possible plus tard, mais alors
l'étape devient une vraie sandbox, avec le coût correspondant.

**Le canari ne valide pas le code.** Il prouve *« ce candidat n'a pas planté sur cet
état-là, en N itérations »* — rien de plus. Un bug de logique qui calcule faux passe
sans encombre, et c'est normal : le canari protège la session, il ne vérifie pas la
correction. C'est précisément la question à laquelle répond la suite de tests, qui
reste possible en option explicite mais jamais par défaut — activée à chaque
sauvegarde, elle ajouterait au cycle la latence que l'outil cherche à supprimer.

**`fork()` ne clone que le thread appelant**, donc l'enfant est un environnement
dégradé si l'hôte est multi-threadé.

**Descripteurs de fichiers, sockets et contextes GPU ne survivent pas à la
duplication.**

### Fichiers

```
.hotswap/
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
  "candidate_path": ".hotswap/plugin.so.candidate",
  "active_path": ".hotswap/plugin.so",
  "detail": { "reason": "signal", "signal": "SIGSEGV" },
  "log_path": ".hotswap/plugin.log"
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
| `sandbox_failed` | `reason` (`dlopen` / `symbol` / `state_load` / `signal` / `exit_code`), `signal`, `exit_code` |
| `sandbox_passed` | `duration_ms` |
| `swapped` | `previous_active_path`, `state_version_from`, `state_version_to` |
| `rolled_back` | `cause_state` — l'état qui a déclenché le rollback |

Les valeurs de `reason` suivent les étapes du canari décrites plus haut :
`dlopen` et `symbol` pour un candidat qui ne se charge pas, `state_load` pour une
désérialisation d'état fautive, `signal` et `exit_code` pour une faute pendant les
appels au point d'entrée.

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

## Persistance et remapping de l'état

L'état de session survit au rechargement parce qu'il est **possédé par l'hôte**, qui
ne le déréférence jamais : il en détient un buffer opaque, dont seul le plugin
connaît le type.

Tant que le layout de cet état ne change pas, il n'y a rien à faire — on remplace le
code, on ne touche pas aux données. Le problème n'apparaît que quand la struct
change, et c'est là qu'intervient le snapshot.

### Deux chemins

```
version d'état inchangée  →  l'état n'est pas touché, on swap le code seul   (cas courant, ~0 ms)
version d'état changée    →  snapshot → reload → relecture par nom            (rare, on paie)
```

L'aiguillage est `plugin_state_version()`. La distinction n'est pas un détail
d'optimisation : sérialiser à chaque sauvegarde coûterait un temps proportionnel à
la taille de l'état, payé dans la boucle même que l'outil existe pour raccourcir.
Or la struct ne change pas à la grande majorité des sauvegardes.

### Le snapshot est auto-descriptif

Le format transporte **l'identité de chaque champ**, pas seulement ses octets. Le
remapping en découle sans que rien n'ait jamais à connaître l'ancien layout :

| Changement dans la struct | Comportement à la relecture |
|---|---|
| Champ ajouté | Absent du snapshot → valeur par défaut |
| Champ supprimé | Présent dans le snapshot → ignoré |
| Champs réordonnés | Aucun effet : lecture par nom, pas par offset |
| `int` → `float` | Conversion appliquée |

C'est ce qui permet de se passer entièrement d'une analyse des informations de
debug. Un format **positionnel** — l'encodage par défaut de bibliothèques comme
bitsery — est exclu pour cette raison : sans identité de champ, insérer un champ au
milieu de la struct décale toutes les lectures et produit des valeurs fausses sans
erreur ni crash, le pire mode de défaillance pour un outil de debug.

Les noms de champs viennent d'une macro de déclaration côté plugin — C++ n'ayant pas
de réflexion exploitable — du type `REFLECT(State, counter, speed)`.

### ABI du plugin

```c
extern "C" int    plugin_state_version(void);
extern "C" size_t plugin_state_size(void);
extern "C" size_t plugin_state_save(const void* state, char* out, size_t cap);
extern "C" bool   plugin_state_load(void* state, const char* in, size_t len);
```

Ordre des opérations, à ne pas inverser : le snapshot est produit par **l'ancien**
plugin *avant* le `dlclose` — lui seul connaît l'ancien layout — puis relu par le
**nouveau** après le `dlopen`.

### Ce qui ne se sérialise pas

Pointeurs bruts, descripteurs de fichiers, sockets, contextes GPU, `std::function`,
objets à vtable : rien de tout cela ne survit à un aller-retour. Les pointeurs
internes à l'état doivent devenir des identifiants ou des index. Cette contrainte
renforce le critère « code séparable de la donnée » de la section *Périmètre*.

Enfin, la correspondance par nom ne devine pas les changements **sémantiques** — un
champ qui passe de millisecondes en secondes, un champ scindé en deux. Un hook de
migration écrit à la main reste l'échappatoire pour ces cas.

### Place dans la machine à états

La migration n'a pas d'état dédié, volontairement : elle est atomique du point de
vue d'un lecteur du statut, et un état intermédiaire ne serait observable que
quelques microsecondes pour le coût d'une écriture fichier. Elle est tracée dans le
`detail` de `swapped` via `state_version_from` et `state_version_to`. Un échec de
relecture est déjà attrapé en amont par le canari (`sandbox_failed`,
`reason: "state_load"`), et converge donc vers `rolled_back` comme n'importe quel
autre échec.
