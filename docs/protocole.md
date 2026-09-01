# Protocole de statut et validation

Ce document décrit le pipeline **Build → Canari → Swap / Rollback**, la machine à
états qui le pilote, et le format d'échange entre les composants.

Prérequis de lecture : [architecture.md](architecture.md) pour la frontière
plugin et le vocabulaire.


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
