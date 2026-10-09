// Reproduction REELLE du crash FL : le plugin est heberge comme chez un DAW,
// en tant que VST3 (DLL), pas lie statiquement. Cycles :
//   scan -> instance -> editeur (fenetre native reelle, addToDesktop) ->
//   thread audio continu + pompage des messages -> detruire l'editeur ->
//   detruire l'instance -> DECHARGEMENT de la DLL.
// C'est la difference avec editor_stress (statique, jamais decharge) : tout
// ce qui vit dans le module (statiques JUCE, images de skin) meurt au
// dechargement, comme quand FL jette le plug du channel rack.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    // Le format VST3, cherche explicitement.
    juce::AudioPluginFormat* vst3 = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat (i)->getName().containsIgnoreCase ("VST3"))
            vst3 = formatManager.getFormat (i);

    if (vst3 == nullptr)
    {
        std::cerr << "VST3 introuvable dans les formats\n";
        return 2;
    }

    // Le binaire INSTALLE (celui que FL charge) — argument 1, sinon le chemin standard.
    juce::File pluginFile ("C:/Program Files/Common Files/VST3/OD Vox.vst3");
    if (argc > 1)
        pluginFile = juce::File (juce::String::fromUTF8 (argv[1]));

    std::cout << "heberge : " << pluginFile.getFullPathName() << "\n";

    for (int cycle = 1; cycle <= 8; ++cycle)
    {
        std::cout << "--- cycle " << cycle << "\n" << std::flush;

        juce::KnownPluginList knownPlugins;
        juce::OwnedArray<juce::PluginDescription> types;
        knownPlugins.scanAndAddFile (pluginFile.getFullPathName(), false, types, *vst3);

        if (types.isEmpty())
        {
            std::cerr << "    scan : aucun type trouve\n";
            return 3;
        }

        juce::String error;
        auto plugin = formatManager.createPluginInstance (*types.getFirst(), 48000.0, 512, error);

        if (plugin == nullptr)
        {
            std::cerr << "    echec : " << error << "\n";
            return 3;
        }

        std::cout << "    instance : " << plugin->getName() << "\n" << std::flush;
        plugin->prepareToPlay (48000.0, 512);

        // Thread audio CONTINU (comme FL pendant qu'il ferme la fenetre).
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
                    plugin->processBlock (buffer, midi);
            }
        };

        {
            // Fenetre native REELLE : chez FL, l'editeur vit dans une fenetre
            // hote (HWND managé), pas hors ecran.
            auto* rawEditor = plugin->createEditorAndMakeActive();

            if (rawEditor == nullptr)
            {
                running = false;
                audioThread.join();
                std::cerr << "    editeur : nullptr\n";
                return 3;
            }

            std::unique_ptr<juce::AudioProcessorEditor> editor (rawEditor);
            editor->setSize (1000, 600);
            editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
            editor->setVisible (true);

            // Pomper les messages avec l'editeur VIVANT (timers 12 Hz,
            // repaints, capture A/B continue) et l'audio qui tourne.
            for (int frame = 0; frame < 45; ++frame)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (16);

            std::cout << "    editeur vivant : ok\n" << std::flush;

            // Le moment du crash FL : la fenetre native part d'abord (comme
            // quand l'utilisateur retire le plugin), puis l'editeur meurt.
            editor->removeFromDesktop();
            plugin->editorBeingDeleted (editor.get());
            editor.reset();
            std::cout << "    editeur detruit\n" << std::flush;
        }

        running = false;
        audioThread.join();

        plugin->releaseResources();
        plugin = nullptr;          // <- destruction de l'instance, DECHARGEMENT de la DLL
        std::cout << "    instance detruite, module decharge\n" << std::flush;
        juce::Thread::sleep (120);
    }

    std::cout << "STRESS VST3 OK : 8 cycles, aucun crash\n";
    return 0;
}
