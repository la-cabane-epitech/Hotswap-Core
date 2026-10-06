# Protocole de statut

Ce document décrit le pipeline **Build → Swap / Rollback**, la machine à états
qui le pilote, et le format d'échange entre les composants.

Prérequis de lecture : [architecture.md](architecture.md) pour la frontière
plugin et le vocabulaire.


Le pipeline (**Watcher/Build → Runtime**, plus **Reporting** en observateur)
tourne dans des processus qui ne partagent pas de mémoire et ne communiquent
pas par IPC directe. Ils communiquent par un fichier de statut par module —
même logique que le polling déjà utilisé par `DLLoader::poll` pour détecter un
candidat.

**Il n'y a pas d'étape de validation avant adoption.** Un candidat qui
compile et dont `dlopen`/`dlsym` réussissent est promu directement. Un plugin
qui compile mais plante ou boucle une fois réellement appelé fait tomber le
process hôte pour de vrai — il n'y a plus de canari pour absorber ce cas.
C'est un compromis assumé, pas un oubli : voir *Historique* en bas de page.

### Machine à états

```
building → build_ok → swapped
              │
        promote_failed      (dlopen/dlsym, ABI ou relecture de l'état échoue)
              │
         rolled_back
```

Un `build_failed` ne produit jamais de candidat, donc rien à promouvoir. Un
`promote_failed` vient d'un symbole manquant ou renommé, d'une version d'ABI
différente, ou d'un snapshot que `plugin_state_load` refuse. Le candidat est
alors jeté et la version active continue, état intact : c'est le filet déjà
intégré à `DLLoader::promote()`, pas une étape à part.

### Fichiers

```
.hotswap/
├── libplugin.so              # dernière version promue, chargée au démarrage du Runtime
├── libplugin.so.candidate    # nouvelle version en attente de promotion
├── libplugin.so.gen<N>       # copie chargée par le Runtime pour le swap N
├── plugin.status.json        # source de vérité du pipeline pour ce module (à faire)
└── build.log                 # stderr de compilation
```

Le suffixe est `.so` sur Linux et `.dylib` sur macOS. Le détail des fichiers
`gen<N>` est dans [abi.md](abi.md), *Séquence d'un swap*.

`plugin.status.json` s'écrit comme le `.so` : sur un `.tmp`, puis `rename()` —
jamais en place, pour qu'aucun lecteur ne tombe sur un JSON à moitié écrit.

### Format de `plugin.status.json`

```json
{
  "schema_version": 1,
  "module": "plugin",
  "state": "swapped",
  "producer": "runtime",
  "timestamp": "2026-08-25T14:32:10Z",
  "candidate_path": ".hotswap/libplugin.so.candidate",
  "active_path": ".hotswap/libplugin.so",
  "detail": { "state_version_from": "1", "state_version_to": "2" },
  "log_path": ".hotswap/build.log"
}
```

| Champ | Type | Rôle |
|---|---|---|
| `schema_version` | int | Fait évoluer le format sans casser les lecteurs existants |
| `module` | string | Nom du plugin concerné (utile dès le multi-modules) |
| `state` | enum | Le champ pivot — un des états de la machine ci-dessus |
| `producer` | enum | `watcher` / `runtime` — qui a écrit ce statut |
| `timestamp` | ISO 8601 UTC | Horodatage de la dernière transition |
| `candidate_path` / `active_path` | string | Chemins des deux `.so` du module |
| `detail` | objet libre | Forme différente selon `state`, voir ci-dessous |
| `log_path` | string | Chemin du log complet |

`detail` selon l'état :

| État | Champs de `detail` |
|---|---|
| `build_failed` | `compiler_exit_code`, `stderr_excerpt` |
| `swapped` | `state_version_from`, `state_version_to` |
| `rolled_back` | `cause_state` — l'état qui a déclenché le rollback |

### Qui écrit, qui lit

| Étape | Processus | Écrit | Lit |
|---|---|---|---|
| Watcher / Build | Watcher | `building` → `build_ok` / `build_failed` | — |
| Runtime / DLLoader | Runtime | `swapped` / `rolled_back` | `build_ok` |
| Reporting | Reporting | rien (pur observateur) | tout |

Deux programmes, trois étapes de la machine — pas de troisième processus.

### Règles

1. **Écriture atomique toujours** — `.tmp` + `rename()`, jamais de write direct sur `*.status.json`.
2. **Détection par polling** du mtime du fichier de statut, comme `DLLoader::poll` le fait déjà pour le candidat.
3. **Un seul statut à la fois** — le fichier contient le dernier état connu, pas un historique (l'historique complet vit dans `build.log`).

### Historique

Une étape *Sandbox* existait ici, exécutant un **canari** — un `fork()` du
Runtime, jetable, qui validait chaque candidat contre une copie *copy-on-write*
de l'état réel avant adoption. Elle attrapait trois classes de fautes qu'un
compilateur ne peut pas voir : segfault, boucle infinie, symbole manquant.

Elle a été retirée du projet pour concentrer l'effort sur la promesse centrale
— la conservation de l'état à travers un changement de structure — plutôt que
sur le filet de sécurité qui l'entoure. À revisiter si un besoin réel se
présente ; voir [MoSCoW.md](MoSCoW.md).
