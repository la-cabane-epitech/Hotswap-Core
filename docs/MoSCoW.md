# MoSCoW — Hotswap-Core

## Décision d'architecture préalable

Toutes les priorités ci-dessous découlent d'un choix qui doit être assumé
explicitement : **Hotswap-Core cible les projets qui adoptent son architecture
plugin.**

Le code rechargé vit dans une bibliothèque partagée derrière une frontière
`extern "C"`, et l'état persistant est possédé par le programme hôte, pas par le
plugin. C'est ce que fait le prototype (`src/host/`, `src/plugin/`).

C'est une contrainte pour l'utilisateur : l'outil n'est pas applicable tel quel à
une codebase existante sans restructuration. En contrepartie, elle rend le projet
réalisable et démontrable.

L'approche inverse — recharger du C++ arbitraire dans un process existant sans
toucher à son architecture (Live++, Unreal Live Coding) — est hors périmètre. Voir
*Won't Have*.

**Le différenciateur du projet n'est pas le hot reload lui-même** — il est connu et
déjà implémenté dans le prototype — **mais le filet de sécurité autour** : un
rechargement à chaud qui ne peut pas faire perdre l'état de la session, même quand
le code rechargé est faux.

---

## Must Have

| # | Item | État |
|---|---|---|
| 1 | Mécanisme de hot reloading | ✅ fait |
| 2 | Détection automatique (File Watcher) | ✅ fait |
| 3 | Build configurable | à faire |
| 4 | Canari de validation | à faire |
| 5 | Gestionnaire de transition | à faire |
| 6 | Reporting d'erreur | à faire |

**1. Mécanisme de hot reloading.** Compilation et rechargement dynamique d'une
bibliothèque partagée en cours d'exécution, sans redémarrer le processus hôte.
Implémenté dans `src/host/DLLoader.cpp` (`dlopen` / `dlsym` / `dlclose` piloté par
le mtime du `.so`).

**2. Détection automatique (File Watcher).** Déclenche le cycle
*Build → Canari → Swap* dès qu'une sauvegarde est détectée. Implémenté dans
`src/filewatcher/`. *Remonté de Should Have à Must Have : tout le pipeline est
déclenché par lui, il n'est optionnel pour rien.*

**3. Build configurable.** La commande de compilation et les chemins surveillés
doivent venir d'un fichier de configuration du projet, pas être codés en dur.
Aujourd'hui `src/filewatcher/Core.hpp` appelle un `g++ -shared -fPIC` littéral :
c'est acceptable pour un prototype, pas pour un produit installable.
*Nouvel item — sans lui, l'outil n'est utilisable que sur son propre dépôt.*

**4. Canari de validation.** Avant tout swap, le candidat est chargé et exécuté
dans un processus enfant issu d'un `fork()` **du Runtime**, sous timeout. L'enfant
`dlopen` le candidat, appelle le point d'entrée N fois sur la copie *copy-on-write*
de l'état, et sort ; le parent lit le code de sortie.

Le `fork()` doit venir du Runtime et pas d'un programme tiers : l'état vivant
n'existe que dans sa mémoire, et c'est contre cet état-là qu'il faut valider — un
candidat qui passe sur un état par défaut mais segfault sur l'état réel de la
session ne prouve rien. La Sandbox est donc une étape du pipeline, pas un
quatrième programme.

Cette étape attrape les trois classes de fautes qu'un compilateur ne peut pas voir
et qui tuent l'hôte : le **segfault**, la **boucle infinie** (couverte par le
timeout — l'ancien item *Time-out de Sandbox* est fusionné ici, un canari sans
timeout ne sert à rien), et les **symboles manquants ou incompatibles** à la
résolution.

*Remplace l'ancienne « Sandbox de Validation » qui exécutait les tests
unitaires/fonctionnels du projet. Motif : faire tourner une suite de tests à chaque
`Ctrl+S` ajoute des secondes au cycle que l'outil cherche justement à raccourcir,
et suppose des tests à jour sur le code qu'on est en train de casser. C'est une
exigence de CI transposée par erreur dans une boucle de développement. Le canari
coûte quelques millisecondes et ne demande aucun test à écrire.* La version avec
suite de tests devient un Could Have.

**5. Gestionnaire de transition.** Si le canari passe, on swappe ; sinon on reste
sur l'ancienne version et on émet un `rolled_back`. Le runtime ne swappe jamais un
candidat dont le statut n'est pas explicitement `sandbox_passed`. Machine à états
et protocole de statut inter-processus déjà spécifiés dans le `README.md`.

**6. Reporting d'erreur.** Affichage de l'erreur dans l'outil — stderr de
compilation, ou cause du rejet par le canari (signal, timeout, code de sortie) —
pour éviter au développeur d'aller la chercher dans les fichiers système.
*Note : les erreurs de compilation ne passent pas par le canari. Un échec de build
est détecté au code de retour du compilateur et court-circuite le pipeline ; il
n'y a pas de `.so` à valider. Le reporting est un consommateur du protocole de
statut, pas une étape de validation.*

---

## Should Have

**7. Persistance et migration de l'état à la frontière du plugin.** Transférer les
valeurs de l'ancienne version vers la nouvelle pour ne pas perdre le contexte de
debug, y compris quand la struct d'état change de layout.

L'état est **versionné**, et le plugin fournit un hook de migration :

```c
extern "C" size_t plugin_state_size(void);
extern "C" int    plugin_state_version(void);
extern "C" void   plugin_migrate(const void* old_state, int old_version, void* new_state);
```

Au rechargement, si la version a changé, l'hôte alloue un buffer à la nouvelle
taille, appelle `plugin_migrate`, puis libère l'ancien.

**Conséquence architecturale à ne pas manquer : l'hôte doit cesser de connaître le
type de l'état.** Aujourd'hui `src/host/main.cpp` déclare `State app_state = {0}`
sur la pile — le layout est gravé dans le binaire de l'hôte à la compilation, donc
modifier `State` impose de recompiler l'hôte, donc de le redémarrer, donc de perdre
l'état qu'on voulait préserver. L'hôte doit posséder un buffer opaque qu'il ne
déréférence jamais, et `State*` disparaît de la signature du point d'entrée au
profit d'un `void*`.

*Fusionne les anciens items « Structure Remapping » et « Persistance de l'état »,
qui sont le même problème. La partie automatique du remapping passe en Won't Have.*

Limites assumées : ne couvre que l'état à la frontière du plugin — les variables
globales et la mémoire allouée à l'intérieur du plugin sont perdues au
rechargement — et demande au développeur d'écrire la migration à chaque changement
de struct. En échange, c'est déterministe, débuggable, et ça traite les migrations
**sémantiques** (un champ qui change d'unité, un champ scindé en deux) qu'aucune
analyse de layout ne peut deviner.

**8. Support multi-modules.** Plusieurs plugins surveillés et rechargés
indépendamment, chacun avec son propre fichier de statut. Le protocole du
`README.md` l'anticipe déjà via le champ `module`, mais rien ne l'implémente.

---

## Could Have

**9. Sandbox avec suite de tests.** Exécution des tests unitaires/fonctionnels du
projet sur le candidat, en plus du canari. Activée explicitement par les projets
qui la veulent et qui acceptent la latence supplémentaire — jamais par défaut.
*Rétrogradé depuis Must Have, voir item 4.*

**10. Intégration IDE.** Extension VS Code pour souligner les erreurs de build et
les rejets de canari directement dans le code source.

**11. Multi-sandbox.** Tester plusieurs versions ou plusieurs sets de tests en
parallèle. Ne présente d'intérêt qu'une fois l'item 9 livré.

---

## Won't Have

**12. Remapping automatique inféré depuis les informations de debug.** Découvrir
seul qu'un type a changé de layout en lisant le DWARF, retrouver tous les objets
vivants de ce type sur le tas, les réallouer et corriger tous les pointeurs qui les
visaient — vtables, pointeurs vers l'intérieur d'un objet et conteneurs dont le
layout dépend de `T` compris.

*Motif : c'est un projet à part entière, pas une feature. La référence du domaine
(Live++) représente environ dix ans de travail d'un ingénieur spécialisé. Le
remplaçant tractable est l'item 7.*

**13. Hot reload sur une codebase non instrumentée.** Recharger du C++ arbitraire
dans un process qui tourne sans imposer l'architecture plugin. Conséquence directe
de la décision d'architecture en tête de document.

**14. Compatibilité multi-OS.** Le projet cible Linux. Le chargeur repose sur
`dlfcn.h` et le build sur un appel direct au compilateur ; Windows demanderait une
réécriture du chargeur (`LoadLibrary`, verrouillage des PDB) et non un portage.

**15. Gestion du déploiement final.** L'outil sert la boucle de développement, pas
la mise en production.

---

## Décisions ouvertes

À trancher avant d'ajouter la moindre fonctionnalité — la forme du livrable
conditionne les items 3, 6 et 10 :

1. **Sous quelle forme l'outil est-il livré ?** CLI, démon, bibliothèque à linker
   dans l'hôte, extension d'IDE ? Aujourd'hui le « produit » est un jeu de binaires
   posés à la racine du dépôt avec un chemin de plugin codé en dur.
2. **Comment un utilisateur l'installe-t-il et l'intègre-t-il à son projet ?**
3. **Quelle est la démo de soutenance ?** Proposition : insérer un déréférencement
   nul dans le plugin, sauvegarder, et montrer l'hôte qui encaisse, signale, et
   continue sur la version précédente sans perdre son état.
