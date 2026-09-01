# Architecture et périmètre

Ce document décrit sur quoi repose Hotswap-Core, à qui il s'adresse, et ce qu'il
demande à un projet qui veut l'adopter.

Pour installer et lancer le projet, voir le [README](../README.md). Pour le
pipeline de validation, voir [protocole.md](protocole.md). Pour la survie de
l'état à travers un rechargement, voir [etat.md](etat.md).


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
Voir [MoSCoW.md](MoSCoW.md) pour les motifs.

Conséquence à assumer : **la valeur de l'outil est proportionnelle à la part des
itérations qui tombent dans la frontière.** Tout ce qui est modifié hors du plugin
impose toujours un redémarrage complet.
