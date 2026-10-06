# ABI — la frontière plugin/hôte

Ce document décrit le contrat binaire entre le Runtime et la bibliothèque du
plugin (`.so` sur Linux, `.dylib` sur macOS) : les symboles que le plugin doit
exporter, leur signature, qui les appelle, quand, et ce qui casse si le contrat
n'est pas respecté.

Prérequis de lecture : [architecture.md](architecture.md) pour le pattern de
frontière, [protocole.md](protocole.md) pour le pipeline qui déclenche ces
appels.

À distinguer de la communication inter-processus décrite dans
[protocole.md](protocole.md) : cette ABI ne traverse pas de processus, elle
traverse `dlsym()` — une frontière *intra-processus*, entre le binaire du
Runtime et la bibliothèque chargée en mémoire. Le Watcher n'y participe pas.

## Contrat figé

La source de vérité est [`include/hotswap/abi.hpp`](../include/hotswap/abi.hpp).
**Le contrat est figé** : toute l'équipe code contre lui. Le modifier demande
l'accord de toute l'équipe et l'incrémentation de `HOTSWAP_ABI_VERSION` — voir
[chantiers.md](chantiers.md).

```c
extern "C" int      hotswap_abi_version(void);
extern "C" uint64_t plugin_state_version(void);
extern "C" void*    plugin_state_create(void);
extern "C" void     plugin_state_destroy(void* state);
extern "C" size_t   plugin_state_save(const void* state, char* out, size_t cap);
extern "C" bool     plugin_state_load(void* state, const char* in, size_t len);
extern "C" void     plugin_update(void* state);
```

| Symbole | Rôle | Appelé sur | Quand |
|---|---|---|---|
| `hotswap_abi_version` | Version du contrat. Un plugin compilé contre un autre header est refusé. | toute version chargée | juste après `dlopen` |
| `plugin_state_version` | Identité du layout de l'état. Égalité entre deux versions → l'état est conservé tel quel. | toute version chargée | juste après `dlopen` |
| `plugin_state_create` | Construit un état neuf, avec ses valeurs par défaut. | la nouvelle version | premier chargement, ou layout changé |
| `plugin_state_destroy` | Détruit un état. | la version qui l'a créé, ou une de même layout | avant le `dlclose` de cette version |
| `plugin_state_save` | Écrit un snapshot auto-descriptif (nom de champ + valeur). | l'**ancienne** version | layout changé, avant tout `dlclose` |
| `plugin_state_load` | Relit un snapshot par nom de champ dans un état neuf. | la **nouvelle** version | layout changé, après `plugin_state_create` |
| `plugin_update` | Le code rechargé. | la version active | à chaque tour de la boucle de l'hôte |

### Propriété de l'état

L'état est **créé et détruit par le plugin**, **détenu par le Runtime** sous
forme d'un `void*` qu'il ne déréférence jamais. Le type `State` n'existe que
dans le plugin ([`src/plugin/plugin.hpp`](../src/plugin/plugin.hpp)) : l'hôte
n'inclut pas ce header, et CMake ne lui en donne pas l'accès.

C'est ce qui permet à la struct de changer de layout sans recompiler ni
redémarrer l'hôte. La mémoire de l'état vit sur le tas du processus : elle
survit au `dlclose`, seules les variables globales et statiques du plugin sont
perdues.

### `plugin_state_save` : convention de taille

La fonction renvoie toujours le nombre d'octets nécessaires, et n'écrit dans
`out` que si `cap` est suffisant — la convention de `snprintf`. Le Runtime
l'appelle une première fois avec `cap = 0`, réserve la taille renvoyée, puis
l'appelle à nouveau.

## Séquence d'un swap

Le candidat est ouvert **avant** la fermeture de la version active, sous un
chemin propre à ce swap (`libplugin.dylib.gen<N>`). Les deux versions sont en
mémoire en même temps : l'état peut passer de l'une à l'autre, et un échec ne
touche jamais la version qui tourne.

```
dlopen(candidat) + dlsym de tous les symboles + contrôle de hotswap_abi_version
│
├─ aucun état encore       → create(nouveau)
├─ même state_version      → l'état est conservé tel quel, rien n'est sérialisé (cas courant)
└─ state_version différent → save(ancien) → create(nouveau) → load(nouveau, snapshot)
                              puis destroy(ancien, ancien état)
│
dlclose(ancien), suppression de son fichier gen
le candidat devient la version active ; sa copie remplace libplugin.dylib
```

Si une étape échoue avant le dernier bloc — `dlopen`, symbole manquant, ABI
différente, `create` qui renvoie `nullptr`, `load` qui renvoie `false` — le
candidat est jeté et la version active continue avec son état, intact.

Le chemin unique par swap n'est pas un détail : `dlopen()` reconnaît une
bibliothèque déjà chargée à son chemin, et rouvrir un chemin déjà utilisé
pourrait rendre l'ancien code. Le fichier chargé n'est jamais renommé ni
réécrit ensuite, pour que les débogueurs le retrouvent.

Cette séquence est implémentée dans
[`src/host/DLLoader.cpp`](../src/host/DLLoader.cpp) et couverte par
[`tests/test_runtime.cpp`](../tests/test_runtime.cpp).

## Règles pour écrire un plugin

| Règle | Pourquoi |
|---|---|
| Seuls des types C traversent la frontière : pointeurs, entiers, `bool`. | Un `std::string` dans une signature lie l'hôte et le plugin à la même version de la bibliothèque standard. |
| Aucune exception ne sort d'une fonction exportée. | Une exception qui traverse la frontière n'a pas de comportement garanti. Attraper à l'intérieur, renvoyer un code d'erreur. |
| `plugin_state_version` change dès qu'un champ est ajouté, supprimé, déplacé ou change de type. | Sinon le Runtime garde l'ancien état avec le nouveau layout : corruption mémoire silencieuse. |
| `plugin_state_load` ne lit que par nom de champ. | Un format positionnel donne des valeurs fausses sans erreur dès qu'un champ est inséré. Voir [etat.md](etat.md). |
| Ne pas garder le pointeur d'état, ni de pointeur vers du code du plugin, au-delà d'un appel. | Le code est démappé au `dlclose`. |
| Pas d'état utile dans des variables globales ou statiques du plugin. | Elles repartent à leur valeur initiale à chaque rechargement. |

## Ce qui reste à faire

| Élément | État |
|---|---|
| Contrat, Runtime et séquence de swap | Fait, testé sur Linux et macOS. |
| `plugin_state_version` calculé automatiquement depuis la liste des champs | À faire. Le plugin de démo renvoie une constante. |
| `plugin_state_save` / `plugin_state_load` du plugin de démo | Bouchons : un changement de layout remet l'état à ses valeurs par défaut. C'est le chantier sérialisation, voir [chantiers.md](chantiers.md). |
| Source de la liste des champs : macro maison ou bibliothèque | Décision ouverte, voir [MoSCoW.md](MoSCoW.md). |
