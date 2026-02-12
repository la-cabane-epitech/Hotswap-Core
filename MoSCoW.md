Must:

    Mécanisme de Hot Reloading : Compilation et rechargement dynamique de DLL/Shared Libs en cours d'exécution.
    Sandbox de Validation : Un environnement isolé (une instance séparée ou un thread protégé) qui exécute les tests unitaires/fonctionnels sur la nouvelle version du code.
    Gestionnaire de Transition : Si les tests passent, on switch sur la nouvelle version ; si ils échouent, on reste sur l'ancienne.
    Reporting d'Erreur : Affichage précis de l'erreur (logs de compilation ou crash sandbox) dans l'outil pour éviter que le développeur n'ait à chercher dans les fichiers systèmes.

Should Have:

    Structure Remapping : Gérer intelligemment le cas où une classe change de taille ou de layout (ajout d'un membre au milieu d'une struct) sans corrompre la mémoire.
    Persistance de l'état (Memory Persistence) : Transférer les valeurs des variables de l'ancienne version vers la nouvelle pour ne pas perdre le contexte de debug.
    Détection automatique (File Watcher) : Déclencher le cycle "Build -> Sandbox -> Swap" dès qu'une sauvegarde est détectée dans l'IDE.
    Time-out de Sandbox : Si le nouveau code contient une boucle infinie, la sandbox doit pouvoir le tuer pour ne pas bloquer l'outil.

Could Have:
    Multi-sandbox : Pouvoir tester plusieurs versions ou plusieurs sets de tests en parallèle.
    Intégration IDE : Plugin pour VS Code ou Visual Studio pour souligner les erreurs directement dans le code source.
    Compatibilité multi-OS : On peut se concentrer uniquement sur Windows ou Linux pour commencer.

Wont Have:
    Gestion du déploiement final.

