# ABI — la frontière plugin/hôte

Ce document décrit le contrat binaire entre le Runtime et le `.so`/`.dylib` du
plugin : les symboles que le plugin doit exporter, leur signature, qui les
appelle, quand, et ce qui casse silencieusement si le contrat n'est pas
respecté.

Prérequis de lecture : [architecture.md](architecture.md) pour le pattern de
frontière, [protocole.md](protocole.md) pour le pipeline qui déclenche ces
appels.

À distinguer de la communication inter-processus décrite dans
[protocole.md](protocole.md) : cette ABI ne traverse pas de processus, elle
traverse `dlsym()` — une frontière *intra-processus*, entre le binaire du
Runtime et le `.so` chargé en mémoire.

## Convention

Tous les symboles sont `extern "C"` : pas de name mangling C++, pas de
surcharge, pas de template. Le Runtime les résout par nom via `dlsym()`
(`DLLoader.cpp`, dans `load_active()`) — renommer une fonction côté plugin
sans mettre à jour l'appelant ne produit pas une erreur de compilation, mais
un `dlsym(plugin_update): symbol not found` au runtime. Sur le candidat, ça
fait échouer la promotion et le Runtime reste sur l'ancienne version (voir
*Qui appelle `plugin_update`* ci-dessous) ; il n'y a pas d'étape de
validation séparée qui l'attraperait plus tôt.

## Surface actuelle — implémentée

Définie entièrement dans [`src/plugin/plugin.hpp`](../src/plugin/plugin.hpp).

```c
struct State {
    int counter;
};

extern "C" void plugin_update(State* state);
```

| Élément | Propriété |
|---|---|
| Propriétaire de `State` | Le Runtime (`main.cpp:27`, alloué sur la pile de `main`). Le plugin ne fait jamais que le recevoir par pointeur. |
| Durée de vie de `State` | Celle du processus hôte. Elle ne dépend d'aucune version du plugin chargée. |
| Qui appelle `plugin_update` | Le Runtime, une fois par itération de sa boucle principale (`main.cpp:41`) — c'est le seul appelant. Aucune exécution de validation séparée n'a lieu avant : le premier appel sur un candidat promu est le même que tous les suivants. |
| Contrat de layout | **Implicite et non vérifié.** `State` doit avoir le même layout dans l'ancien et le nouveau `.so` — rien dans le code ne le garantit ni ne le détecte. Un changement de layout aujourd'hui n'est pas une erreur signalée, c'est une corruption silencieuse. |
| Effets de bord | Réels, pour de vrai, dès le premier appel — pas de duplication à anticiper, mais pas de filet non plus si le code plante. |
| Ce que le plugin ne doit pas faire | Conserver le pointeur `state` au-delà de l'appel ; dépendre d'un état global interne au plugin, qui ne survit à aucun rechargement ; segfaulter ou boucler sans fin — plus aucune étape n'absorbe ce cas, voir *Historique* dans [protocole.md](protocole.md). |

## Surface prévue — spécifiée, pas codée

Quatre symboles supplémentaires, décrits dans [etat.md](etat.md), nécessaires
pour que l'état survive à un changement de layout de `State` (MoSCoW item 7,
cœur du projet). **Aucun n'existe dans le code aujourd'hui.**

```c
extern "C" int    plugin_state_version(void);
extern "C" size_t plugin_state_size(void);
extern "C" size_t plugin_state_save(const void* state, char* out, size_t cap);
extern "C" bool   plugin_state_load(void* state, const char* in, size_t len);
```

| Symbole | Appelé sur | Quand | Rôle |
|---|---|---|---|
| `plugin_state_version` | ancienne **et** nouvelle version | avant toute décision de swap | Si les deux versions rendent la même valeur, le Runtime swap le code seul — aucune sérialisation, chemin à ~0 ms. |
| `plugin_state_size` | nouvelle version | après `dlopen`, avant allocation | Donne au Runtime la taille du buffer opaque à allouer pour la nouvelle struct. |
| `plugin_state_save` | **ancienne** version | juste avant `dlclose` | Sérialise l'état courant dans un format auto-descriptif (nom de champ + valeur) — l'ancienne version est la seule à connaître son propre layout. |
| `plugin_state_load` | **nouvelle** version | juste après `dlopen`, avant la reprise de la boucle | Relit le snapshot par nom de champ et remplit le nouveau buffer. Un retour `false` doit être traité comme un échec de promotion ordinaire (rollback) ; un crash dans cette fonction, en revanche, fait tomber le process — voir *Historique* dans [protocole.md](protocole.md). |

Séquencement strict à respecter à l'implémentation :

```
versions identiques     → dlclose(ancien) → dlopen(nouveau) → swap, rien sérialisé
versions différentes    → plugin_state_save(ancien)   [avant dlclose]
                        → dlclose(ancien) → dlopen(nouveau)
                        → plugin_state_load(nouveau)   [avant tout plugin_update]
```

**Non résolu à ce jour :** la source de la liste {nom, type, offset} que
`plugin_state_save`/`load` consomment — macro `REFLECT` déclarée à la main,
ou parsing du DWARF du binaire compilé. Les deux produisent la même liste,
voir [etat.md](etat.md) et [MoSCoW.md](MoSCoW.md) *Décisions ouvertes #3*. Ce
document décrit le contrat d'appel, pas la façon dont la liste de champs est
obtenue — ce choix ne change rien aux quatre signatures ci-dessus.

## Ce qui casse le contrat silencieusement

| Erreur | Détectée comment |
|---|---|
| Renommer `plugin_update` sans recompiler l'appelant | `dlsym` échoue au runtime, jamais à la compilation. Sur un candidat, ça fait échouer la promotion (rollback automatique) ; il n'y a plus d'étape de validation séparée qui l'attraperait avant. |
| Changer le layout de `State` sans l'ABI de version (aujourd'hui) | **Rien ne le détecte.** Lecture/écriture à de mauvais offsets, silencieux. |
| Oublier un champ dans `REFLECT` (une fois codé) | Prévu : un `static_assert` doit comparer le nombre de champs listés au nombre réel de membres — erreur de compilation plutôt que champ perdu en silence. Pas encore implémenté. |
| Garder un pointeur vers un objet polymorphe défini dans le plugin, à travers un `dlclose` | Le vtable pointe dans le `.so` déchargé ; le crash arrive plus tard, ailleurs, sans rapport apparent — voir *Le pattern de frontière* dans [architecture.md](architecture.md). |

## État d'implémentation de ce document

| Section | Statut |
|---|---|
| Surface actuelle | Reflète le code tel qu'il est, vérifié ligne à ligne au moment de la rédaction. |
| Surface prévue | Spécification reprise de [etat.md](etat.md) ; aucun symbole n'existe dans `src/`. |
