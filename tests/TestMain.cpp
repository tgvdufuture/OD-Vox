#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

/** Lanceur commun a tous les fichiers de tests.

    Variante de debogage : execute chaque suite SEULE dans son runner et logue
    AVANT de la lancer (std::cout en `unitbuf` : flush a chaque insertion). Un
    crash en pleine suite ne perd donc plus le nom de la suite fautive. */
int main()
{
    std::cout.setf (std::ios::unitbuf); // flush immediate a chaque <<
    std::cout << "=== ODVoxTests : lance ===\n";

    auto& all = juce::UnitTest::getAllTests();

    int failures = 0;
    int passes   = 0;

    for (auto* suite : all)
    {
        if (suite == nullptr)
            continue;

        std::cout << "\n--- suite : " << suite->getName() << " ---\n";

        juce::UnitTestRunner runner;
        runner.setAssertOnFailure (false);
        runner.runTests (suite);            // suite isolee

        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            auto* result = runner.getResult (i);
            if (result == nullptr)
                continue;

            failures += result->failures;
            passes   += result->passes;

            std::cout << "  [" << (result->failures == 0 ? "ok " : "KO ") << "] "
                      << result->subcategoryName
                      << "  (" << result->passes << " assertions)\n";

            if (result->failures > 0)
                for (const auto& message : result->messages)
                    std::cout << "        " << message << "\n";
        }
    }

    std::cout << "\n" << (failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ONT ECHOUE")
              << " : " << passes << " assertions reussies, " << failures << " echouees\n";

    return failures == 0 ? 0 : 1;
}
