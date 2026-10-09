#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

/** Lanceur des tests d'interface (F1.6c).

    Il est SEPARÉ de `TestMain.cpp` pour une raison précise : l'initialisation
    GUI de JUCE doit être **démarrée et arrêtée explicitement** ici, dans
    `main`. La variante par variable statique (`ScopedJuceInitialiser_GUI` au
    niveau fichier) plante au démarrage — le gestionnaire de messages n'existe
    pas encore — et, placée dans une fonction, elle laisse le processus suspendu
    à la fin : le destructeur de l'initialiseur GUI attend une boucle de
    messages qui n'a plus personne pour la faire tourner.

    Ici : démarrage avant les tests, arrêt explicite APRÈS la sortie du rapport,
    puis `return` immédiat — le destructeur n'a plus rien à attendre. */
int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    int passes = 0;
    juce::String currentSuite;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* result = runner.getResult (i);

        if (result == nullptr)
            continue;

        failures += result->failures;
        passes += result->passes;

        if (result->unitTestName != currentSuite)
        {
            currentSuite = result->unitTestName;
            std::cout << "\n" << currentSuite << "\n";
        }

        std::cout << "  [" << (result->failures == 0 ? "ok " : "KO ") << "] "
                  << result->subcategoryName
                  << "  (" << result->passes << " assertions)\n";

        if (result->failures > 0)
            for (const auto& message : result->messages)
                std::cout << "        " << message << "\n";
    }

    std::cout << "\n" << (failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ONT ECHOUE")
              << " : " << passes << " assertions reussies, " << failures << " echouees\n"
              << std::flush;

    // Le destructeur de `juceInitialiser` s'exécute au `return`, sur une pile
    // encore intacte : c'est exactement ce que la variante statique ne peut pas
    // garantir, et c'est pourquoi ce lanceur existe.
    return failures == 0 ? 0 : 1;
}
