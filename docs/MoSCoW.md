# MoSCoW — Hotswap-Core

## Décision d'architecture préalable

Toutes les priorités ci-dessous découlent d'un choix qui doit être assumé
explicitement : **le code rechargé vit dans une bibliothèque partagée derrière une
frontière `extern "C"`, et l'état persistant est possédé par le programme hôte, pas
par le plugin.** C'est ce que fait le prototype (`src/host/`, `src/plugin/`).

L'outil ne demande pas de restructurer une application entière : il demande **une
couture**, à un endroit où l'on itère souvent et où le code est déjà séparable de
la donnée — une boucle d'update, un handler, un système de règles. Le critère de
coût du portage n'est pas la taille de la codebase, mais la quantité d'état qu'il
faut déloger du code rechargé pour la remonter dans l'hôte. Voir la section
*Périmètre* du `README.md`.

La cible est donc : **projets ayant déjà une frontière de rechargement, ou pouvant
en isoler une rapidement, et dont l'état de session est cher à reconstruire** — jeu
vidéo, simulation, robotique, trading. Ces domaines ont déjà adopté ce pattern
volontairement et sans outil ; le coût d'adoption y est proche de zéro.

L'approche inverse — recharger du C++ arbitraire dans un process sans toucher à son
architecture (Live++, Unreal Live Coding) — est hors périmètre. Voir *Won't Have*.

**Le différenciateur du projet n'est pas le hot reload lui-même** — il est connu et
déjà implémenté dans le prototype — **mais la conservation de l'état de session à
travers le rechargement, y compris quand la structure de cet état change entre
deux versions.** Un outil qui ne perd jamais l'état mais ne recharge que du code au
layout inchangé n'a résolu que la moitié du problème.

Il n'y a pas de filet de sécurité autour de cette promesse. Un canari existait —
un `fork()` du Runtime validant chaque candidat avant adoption — et a été retiré
pour concentrer tout l'effort sur la promesse centrale plutôt que sur ce qui la
protégerait. Voir *Won't Have*, item 4, pour ce qui existait et pourquoi.

---

## Must Have

C'est ici que vit la promesse centrale : le code peut changer de layout de donnée
sans faire perdre l'état de session. Aucune étape de validation ne protège cette
promesse — voir item 4 en *Won't Have*.

| # | Item | État |
|---|---|---|
| 1 | Mécanisme de hot reloading | ✅ fait |
| 2 | Détection automatique (File Watcher) | ✅ fait |
| 3 | Build configurable | à faire |
| 7 | Sérialisation de l'état et remapping par nom de champ | à faire — **priorité 1** |
| 5 | Gestionnaire de transition | à faire |
| 6 | Reporting d'erreur | à faire |

**1. Mécanisme de hot reloading.** Compilation et rechargement dynamique d'une
bibliothèque partagée en cours d'exécution, sans redémarrer le processus hôte.
Implémenté dans `src/host/DLLoader.cpp` (`dlopen` / `dlsym` / `dlclose` piloté par
le mtime du `.so`).

**2. Détection automatique (File Watcher).** Déclenche le cycle
*Build → Swap* dès qu'une sauvegarde est détectée. Implémenté dans
`src/filewatcher/`. *Remonté de Should Have à Must Have : tout le pipeline est
déclenché par lui, il n'est optionnel pour rien.*

**3. Build configurable.** La commande de compilation et les chemins surveillés
doivent venir d'un fichier de configuration du projet, pas être codés en dur.
Aujourd'hui `src/filewatcher/Core.hpp` appelle un `g++ -shared -fPIC` littéral :
c'est acceptable pour un prototype, pas pour un produit installable.
*Nouvel item — sans lui, l'outil n'est utilisable que sur son propre dépôt.*

Inclut la forme du livrable : un `hotswap run ./mon_app` qui lit la configuration
et supervise les processus. Tant que le livrable est un jeu de binaires posés à la
racine, il n'y a pas d'usage quotidien possible.

### 7. Sérialisation de l'état et remapping par nom de champ

**C'est le cœur du projet.** Sans lui, l'outil ne recharge que du code dont la
struct d'état n'a pas bougé — exactement le cas le moins intéressant, puisque dans
ce cas le simple `dlopen`/`dlclose` suffit déjà.

Transférer les valeurs de l'ancienne version vers la nouvelle pour ne pas perdre le
contexte de debug, **y compris quand la struct d'état change de layout**.

Le mécanisme est un **snapshot auto-descriptif** : l'état est sérialisé dans un
format qui transporte l'identité de chaque champ, pas seulement ses octets. Le
remapping tombe alors sans jamais avoir à connaître l'ancien layout :

| Changement dans la struct | Comportement à la relecture |
|---|---|
| Champ ajouté | Absent du snapshot → valeur par défaut |
| Champ supprimé | Présent dans le snapshot → ignoré |
| Champs réordonnés | Aucun effet : lecture par nom, pas par offset |
| `int` → `float` | Conversion appliquée |

*C'est ce point qui rend inutile l'analyse des informations de debug : le problème
du remapping n'est pas résolu, il est contourné. Cet item remplace les anciens
« Structure Remapping » et « Persistance de l'état », qui sont le même problème.*

**Deux chemins, et c'est essentiel.** Sérialiser à chaque sauvegarde coûterait un
temps proportionnel à la taille de l'état, payé dans la boucle que l'outil existe
pour raccourcir. Or la struct ne change pas à la grande majorité des sauvegardes :

```
version d'état inchangée  →  l'état n'est pas touché, on swap le code seul   (cas courant, ~0 ms)
version d'état changée    →  snapshot → reload → relecture par nom            (rare, on paie)
```

`plugin_state_version()` est l'aiguillage entre les deux.

**ABI du plugin :**

```c
extern "C" int    plugin_state_version(void);
extern "C" size_t plugin_state_size(void);
extern "C" size_t plugin_state_save(const void* state, char* out, size_t cap);
extern "C" bool   plugin_state_load(void* state, const char* in, size_t len);
```

Contrainte de séquencement à ne pas rater : le snapshot est produit par **l'ancien**
plugin, avant le `dlclose` — lui seul connaît l'ancien layout — et relu par le
**nouveau**, après le `dlopen`.

**Conséquence architecturale.** L'hôte doit cesser de connaître le type de l'état.
Aujourd'hui `src/host/main.cpp` déclare `State app_state = {0}` sur la pile : le
layout est gravé dans le binaire de l'hôte à la compilation, donc modifier `State`
impose de recompiler l'hôte, donc de le redémarrer, donc de perdre l'état qu'on
voulait préserver. L'hôte possède un buffer opaque qu'il ne déréférence jamais.

**Source des noms de champs.** C++ n'a pas de réflexion exploitable. Trois options,
par ordre de sûreté : une **macro de déclaration** (`REFLECT(State, counter, speed)`)
qui génère la liste des champs — zéro dépendance, marche partout, c'est le choix
retenu pour livrer ; un **générateur libclang** qui parse les headers, sans
boilerplate côté utilisateur mais avec une étape de build et une dépendance LLVM ;
la **réflexion statique C++26**, à vérifier sur la chaîne de compilation avant d'y
compter et sur laquelle aucun lot ne doit reposer.

**Format.** Noms de champs en clair d'abord : une migration fautive doit pouvoir
être diagnostiquée en ouvrant le snapshot. Le passage à des tags numériques
stables (un `uint16` par champ, à la Protobuf) est une substitution locale, à faire
une fois le format stabilisé et **seulement si la mesure le justifie**. Un format
positionnel — l'encodage par défaut de bibliothèques comme bitsery — est exclu :
sans identité de champ, l'insertion d'un champ au milieu de la struct produit des
valeurs fausses sans erreur ni crash, le pire mode de défaillance pour un outil de
debug.

**Limites assumées.** Ne couvre que l'état à la frontière du plugin ; les variables
globales et la mémoire allouée à l'intérieur du plugin sont perdues au
rechargement. Et tout n'est pas sérialisable : pointeurs bruts, descripteurs de
fichiers, sockets, contextes GPU, `std::function`, objets à vtable. Les pointeurs
internes à l'état doivent devenir des identifiants ou des index. Un hook de
migration écrit à la main reste l'échappatoire pour les changements **sémantiques**
— un champ qui change d'unité, un champ scindé en deux — qu'aucune correspondance
par nom ne peut deviner.

**5. Gestionnaire de transition.** Un candidat détecté est promu directement : son
`.so` remplace l'actif, `dlopen`/`dlsym` sont retentés sur la nouvelle version. Si
l'un des deux échoue — symbole manquant, fichier corrompu — on reste sur l'ancienne
version et on émet un `rolled_back`. C'est le seul filet qui reste : il attrape un
candidat qui ne charge pas, pas un candidat qui charge puis plante à l'exécution.
Machine à états et protocole de statut inter-processus spécifiés dans
[protocole.md](protocole.md).

**6. Reporting d'erreur.** Affichage de l'erreur dans l'outil — stderr de
compilation, échec de relecture d'un snapshot, échec de `dlopen`/`dlsym` sur un
candidat promu — pour éviter au développeur d'aller la chercher dans les fichiers
système. Le reporting est un consommateur du protocole de statut, pas une étape de
validation.

---

## Should Have

### 8. Snapshots nommés et restauration

Sauvegarder l'état sous un nom et y revenir à la demande : *« recharge le code et
remets-moi dans l'état d'il y a 30 secondes »*. Presque gratuit une fois l'item 7
livré, et c'est une capacité de debug que ne donne aucun outil de hot reload
existant.

### 9. Survie au crash de l'hôte

Snapshots périodiques en tâche de fond, et restauration au redémarrage. Étend la
promesse au-delà de la frontière du plugin : aujourd'hui, si l'hôte plante — pour
une raison liée au plugin rechargé ou non — la session est perdue.

### 10. Support multi-modules

Plusieurs plugins surveillés et rechargés indépendamment, chacun avec son propre
fichier de statut. Le protocole du `README.md` l'anticipe déjà via le champ
`module`, mais rien ne l'implémente. *Première variable d'ajustement si le
calendrier se tend : l'ajouter plus tard ne coûtera pas de refonte.*

---

## Could Have

**11. Sandbox avec suite de tests.** Exécution des tests unitaires/fonctionnels du
projet sur le candidat, avant adoption. Activée explicitement par les projets qui
la veulent et qui acceptent la latence supplémentaire — jamais par défaut.
*Seule forme de validation qui resterait envisageable sans réintroduire de
canari, voir item 4 en Won't Have.*

**12. Intégration IDE.** Extension VS Code pour souligner les erreurs de build
directement dans le code source.

**13. Partage de session.** Exporter un snapshot dans un fichier qu'un collègue
recharge chez lui pour reproduire un bug. Dépend de l'item 7.

**14. Multi-sandbox.** Tester plusieurs versions ou plusieurs sets de tests en
parallèle. Ne présente d'intérêt qu'une fois l'item 11 livré.

---

## Won't Have

**4. Canari de validation.** Avant tout swap, le candidat était chargé et exécuté
dans un processus enfant issu d'un `fork()` **du Runtime**, sous timeout. L'enfant
`dlopen` le candidat, appelait le point d'entrée N fois sur une copie
*copy-on-write* de l'état réel, et sortait ; le parent lisait le code de sortie
pour attraper trois classes de fautes qu'un compilateur ne peut pas voir —
segfault, boucle infinie, symbole manquant — plus une désérialisation d'état
fautive.

**Retiré du projet.** Implémenté puis retiré pour concentrer tout l'effort sur la
promesse centrale — la conservation de l'état à travers un changement de struct —
plutôt que sur le filet qui l'entourait. Conséquence directe et assumée :
un plugin qui compile mais segfault ou boucle une fois réellement appelé fait
tomber le process hôte, pour de vrai. À revisiter si un besoin réel se présente ;
voir l'implémentation passée dans l'historique git (`DLLoader.cpp` avant son
retrait) et dans *Historique*, [protocole.md](protocole.md).

**16. Hot reload sur une codebase non instrumentée.** Recharger du C++ arbitraire
dans un process qui tourne sans imposer l'architecture plugin. Conséquence directe
de la décision d'architecture en tête de document.

**17. Compatibilité multi-OS.** Le projet cible Linux. Le chargeur repose sur
`dlfcn.h` ; Windows demanderait une réécriture du chargeur (`LoadLibrary`,
verrouillage des PDB) et non un portage. Le retrait du canari enlève la seule
autre dépendance plateforme (`fork()`) : le blocage restant tient entièrement au
chargeur, rien d'autre à traiter côté canari.

**18. Gestion du déploiement final.** L'outil sert la boucle de développement, pas
la mise en production.

---

## Décisions ouvertes

1. **Comment un utilisateur installe-t-il l'outil et l'intègre-t-il à son projet ?**
   (L'item 3 fixe la forme du livrable, pas la distribution.)
2. **Quelle est la démo de soutenance ?** Proposition : modifier le plugin et
   montrer le compteur qui continue sans redémarrer — puis ajouter un champ au
   milieu de la struct pour montrer le remapping. *Mise à jour : l'ancienne
   proposition montrait un déréférencement nul absorbé par le canari ; sans lui,
   ce même scénario ferait planter l'hôte pour de vrai — `demo.sh` ne couvre plus
   que le changement valide et l'échec de compilation.*
3. **Remapping automatique inféré depuis les informations de debug (ex-item 15).**
   Découvrir seul qu'un type a changé de layout en lisant le DWARF, retrouver tous
   les objets vivants de ce type sur le tas, les réallouer et corriger tous les
   pointeurs qui les visaient — vtables, pointeurs vers l'intérieur d'un objet et
   conteneurs dont le layout dépend de `T` compris.

   Pas classé en Won't Have : à rediscuter en équipe avant de le fermer
   définitivement. Éléments à mettre dans la balance — le remapping par l'item 7
   couvre déjà le besoin fonctionnel sans ça ; la version complète (parcours du
   tas, réparation de pointeurs, vtables) est du même ordre de grandeur qu'un
   ramasse-miettes à compaction, ce que Live++ lui-même ne résout pas dans sa
   forme la plus générale. Mais rien n'empêche une version plus étroite plus
   tard — lire le DWARF seulement pour la liste des champs d'un struct, en
   alternative à la macro `REFLECT` (voir item 7) — sans aller jusqu'au parcours
   du tas.

## Jalons de dérisquage

Deux mesures à faire **tôt**, parce qu'elles peuvent invalider des choix de
conception pendant qu'il est encore temps d'en changer :

1. **Coût réel du portage.** Porter un projet open source du domaine cible sur
   l'architecture plugin, et chronométrer — en mesurant surtout *combien d'état il
   a fallu déloger*. Ce chiffre est la réponse à la question de l'adoption, et la
   meilleure slide de la soutenance. S'il sort à plusieurs jours, la frontière est
   trop exigeante.
2. **Comportement du débogueur.** Que deviennent les breakpoints posés dans le
   plugin après un `dlclose` / `dlopen` ? GDB sait en principe repositionner les
   breakpoints `fichier:ligne` au rechargement d'une bibliothèque, mais c'est à
   vérifier, pas à supposer. Si le développeur perd ses breakpoints à chaque
   sauvegarde, l'outil est inutilisable pour du debug — donc inutilisable.
