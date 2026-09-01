# Persistance et remapping de l'état

Ce document décrit comment l'état de session survit à un rechargement, y compris
lorsque la structure de cet état change entre deux versions.

Prérequis de lecture : [architecture.md](architecture.md) pour la frontière
plugin, [protocole.md](protocole.md) pour le pipeline de validation.


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
