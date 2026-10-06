# Chantiers — travailler à quatre en parallèle

Ce document découpe le travail restant en quatre chantiers qui se touchent le
moins possible, et fixe les règles qui permettent de les mener en même temps.

Prérequis de lecture : [abi.md](abi.md) pour le contrat entre le Runtime et le
plugin, [MoSCoW.md](MoSCoW.md) pour les priorités.

## Ce qui est en place

| Élément | Où | État |
|---|---|---|
| Contrat plugin/hôte, figé | `include/hotswap/abi.hpp` | Fait |
| Runtime : état opaque, swap, transfert d'état, rejet d'un candidat | `src/host/DLLoader.cpp` | Fait, testé |
| Plugin de démo conforme au contrat | `src/plugin/` | Fait, sérialisation en bouchons |
| Tests, sans dépendance externe | `tests/` | 11 tests du Runtime |
| CI : build et tests sur Linux et macOS | `.github/workflows/build.yml` | Fait |

## Règles communes

1. **Le contrat est figé.** `include/hotswap/abi.hpp` ne change qu'avec l'accord
   des quatre, dans une pull request dédiée qui incrémente
   `HOTSWAP_ABI_VERSION` et met à jour [abi.md](abi.md). Si un chantier a besoin
   d'un changement, il le propose ; il ne le fait pas en passant.
2. **Une branche par chantier**, fusionnée dans `main` par pull request, avec
   une CI verte sur Linux **et** macOS.
3. **Chaque chantier possède ses fichiers** (tableau ci-dessous). Modifier un
   fichier d'un autre chantier se fait après l'avoir prévenu.
4. **Tout comportement nouveau arrive avec un test.** Un test est un fichier
   `tests/test_*.cpp` écrit avec `tests/check.hpp`, enregistré par une ligne
   `hotswap_add_test()` dans `tests/CMakeLists.txt`.
5. **La doc suit le code dans la même pull request.** Quatre personnes lisent
   ces documents : une erreur dedans coûte quatre fois.

Pour démarrer :

```bash
./build.sh
ctest --test-dir build --output-on-failure
```

## Les quatre chantiers

| # | Chantier | Possède | Dépend de |
|---|---|---|---|
| 1 | Sérialisation et remapping | `include/hotswap/state.hpp` (à créer), `tests/test_state.cpp`, `src/plugin/` | Une décision d'équipe, voir ci-dessous |
| 2 | Statut JSON et reporting | `src/status/` (à créer), les appels de statut dans `Core.hpp` et `DLLoader.cpp` | Rien |
| 3 | Build configurable et `hotswap run` | `src/filewatcher/main.cpp`, la commande de build dans `Core.hpp`, `src/cli/` (à créer) | Rien |
| 4 | Dérisquage et démo | `examples/` (à créer), `docs/` pour les mesures | Rien pour démarrer |

### 1. Sérialisation et remapping — priorité 1

C'est le cœur du projet : remplacer les bouchons du plugin de démo par une vraie
sérialisation par nom de champ, comme décrit dans [etat.md](etat.md).

- **Première étape, en équipe :** choisir entre la macro maison et une
  bibliothèque comme nlohmann/json. Voir *Source des noms de champs* dans
  [MoSCoW.md](MoSCoW.md).
- **Livrable :** un header côté plugin qui génère les cinq fonctions d'état
  (`plugin_state_version`, `_create`, `_destroy`, `_save`, `_load`) à partir
  d'une liste de champs. Le développeur d'un plugin écrit une ligne, pas cinq
  fonctions.
- **`plugin_state_version` calculé**, jamais saisi : un hash des noms et des
  types des champs.
- **Terminé quand** les tests couvrent les quatre lignes du tableau de remapping
  (champ ajouté, supprimé, déplacé, `int` → `float`), et qu'ajouter un champ au
  milieu de `State` dans le plugin de démo garde la valeur du compteur.

Ce chantier ne touche pas au Runtime : il travaille entièrement derrière le
contrat. Le Runtime appelle déjà `save` et `load` au bon moment, c'est testé.

### 2. Statut JSON et reporting

Implémenter [protocole.md](protocole.md) : le fichier `plugin.status.json`,
écrit par le Watcher (`building`, `build_ok`, `build_failed`) et par le Runtime
(`swapped`, `promote_failed`), puis un lecteur qui affiche les erreurs.

- **Livrable :** un petit module d'écriture atomique du statut dans
  `src/status/`, utilisé par les deux processus.
- **Dans le code des autres :** quelques appels seulement. Côté Runtime, aux
  points de `DLLoader::promote()` qui affichent déjà « Swap done » ou
  « Candidate rejected ». Côté Watcher, dans `Core::build()`.

### 3. Build configurable et `hotswap run`

Item 3 du MoSCoW : la commande de compilation, les dossiers surveillés et les
includes viennent d'un fichier de configuration du projet, et une commande
unique lance et supervise les deux processus.

- **Point de contact avec le chantier 2 :** les deux touchent
  `src/filewatcher/Core.hpp`. Pour éviter les conflits, ce chantier commence par
  extraire la construction de la commande de compilation dans sa propre
  fonction, et fusionne ce découpage tôt.

### 4. Dérisquage et démo

Les deux jalons de [MoSCoW.md](MoSCoW.md), *Jalons de dérisquage*, puis la démo
de soutenance.

- **Coût du portage :** porter un projet open source du domaine cible sur la
  frontière plugin, et mesurer combien d'état il a fallu déloger.
- **Débogueur :** vérifier que les breakpoints posés dans le plugin survivent à
  un swap, avec GDB sur Linux **et** LLDB sur macOS.
- **Démo :** le scénario « ajouter un champ au milieu de la struct » dépend du
  chantier 1 ; tout le reste peut être préparé avant.

Ces deux mesures peuvent invalider des choix de conception : elles passent
avant le confort.
