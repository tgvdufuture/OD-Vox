// Harnais de stress : reproduire le crash FL a la RETRAITE du plugin.
//
// Scenario de l'incident : FL fait tourner le thread audio, l'utilisateur
// jette le plugin du channel rack => l'editeur est detruit PENDANT que le
// processeur continue de traiter. Ici : thread audio qui boucle sur
// processBlock, et l'editeur cree/detruit 40 fois avec gestes et pompage de
// messages entre les cycles. Tout crash est le bug cherche.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../src/Parameters.h"
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    ODVoxAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // --- Le thread audio : il ne s'arrete JAMAIS pendant les cycles d'editeur.
    std::atomic<bool> running { true };
    std::thread audioThread
    {
        [&]
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i,
                        0.25f * std::sin (2.0 * juce::MathConstants<double>::pi
                                            * 220.0 * (double) i / 48000.0));

            while (running)
                processor.processBlock (buffer, midi);
        }
    };

    const auto catalogue = odvox::params::declared();
    juce::Random rng (0x0DC0FFEE);
    int cycles = 0;

    for (int iteration = 0; iteration < 40; ++iteration)
    {
        // Un geste de parametre depuis le thread de messages (comme un knob
        // tourne par l'utilisateur, ou l'attachment d'un toggle).
        const auto& info = catalogue[(size_t) rng.nextInt ((int) catalogue.size())];
        if (auto* p = processor.state().getParameter (info.id))
            p->setValueNotifyingHost (rng.nextFloat());

        {
            ODVoxAudioProcessorEditor editor (processor);
            editor.setSize (ODVoxAudioProcessorEditor::kDefaultWidth,
                            ODVoxAudioProcessorEditor::kDefaultHeight);

            // Pomper des messages PENDANT la vie de l'editeur : timers (12 Hz),
            // repaints, capture continue A/B — le vrai rythme chez l'hote.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);

            auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            juce::ignoreUnused (image);
        } // <- LA destruction, le moment du crash FL

        // ...et le thread audio continue APRES la destruction de l'editeur.
        juce::Thread::sleep (25);
        ++cycles;

        if (iteration % 10 == 9)
            std::cout << "cycle " << cycles << " : ok\n" << std::flush;
    }

    running = false;
    audioThread.join();

    std::cout << "STRESS OK : " << cycles
              << " cycles editeur crees/detruits, audio continu, aucun crash\n";
    return 0;
}
