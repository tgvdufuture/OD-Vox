// Capture hors ecran de l'editeur : l'identite visuelle se juge a l'oeil.
// Ecrit des PNG dans verifications/ui/ pour relecture humaine (le detenteur
// du produit tranche la couleur, pas la machine).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../src/Parameters.h"
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include "../src/Skin.h"

#include <iostream>

namespace
{
    /** PPM (P6) : trois lignes d'en-tete puis les octets RGB. Sans
        dependance, sans canalpha — le format que tout lit. */
    void writePpm (const juce::Image& source, const char* relativePath)
    {
        const juce::Image image = source.convertedToFormat (juce::Image::RGB);
        juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (relativePath));
        file.getParentDirectory().createDirectory();

        // FileOutputStream AJOUTE a la fin d'un fichier existant (il ne
        // tronque pas) : sans ce delete, la capture fraiche s'empile derriere
        // l'ancienne et tout lecteur n'en decodant qu'une voit l'ANCIENNE.
        file.deleteFile();

        juce::FileOutputStream stream (file);

        if (! stream.openedOk())
        {
            std::cerr << "ecriture impossible : " << file.getFullPathName() << "\n";
            return;
        }

        const int w = image.getWidth(), h = image.getHeight();
        stream.writeText ("P6\n" + juce::String (w) + " " + juce::String (h) + "\n255\n",
                          false, false, "\n");

        juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                stream.writeByte (data.getPixelColour (x, y).getRed());
                stream.writeByte (data.getPixelColour (x, y).getGreen());
                stream.writeByte (data.getPixelColour (x, y).getBlue());
            }

        stream.flush();
        std::cout << "ecrit : " << file.getFullPathName() << "\n";
    }

    void capture (ODVoxAudioProcessorEditor& editor, const char* relativePath)
    {
        // La voie sanctionnee par JUCE pour capturer un composant hors ecran.
        juce::Image image = editor.createComponentSnapshot (
            editor.getLocalBounds(), true, 1.0f);

        const auto probe = image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2);
        std::cout << relativePath << " pixel centre : "
                  << (int) probe.getRed() << "," << (int) probe.getGreen() << ","
                  << (int) probe.getBlue() << " alpha " << (int) probe.getAlpha() << "\n";

        juce::String path (relativePath);
        writePpm (image, path.replace (".png", ".ppm").toRawUTF8());
    }

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        if (auto* param = p.state().getParameter (id))
            param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }
}

static void probeDrawKnob();

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // Diagnostic skin : sprite Figma + atlas de frames 3D charge-t-il ?
    std::cout << "skin knob present : " << (odvox::skin::hasKnobSprite() ? "OUI" : "NON")
              << "\n";
    std::cout << "skin knob atlas (3D) : " << (odvox::skin::hasKnobAtlas() ? "OUI" : "NON")
              << "\n";
    probeDrawKnob();

    ODVoxAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // 1. Defauts : le plug neuf, neutre — LED eteintes, courbe plate (Air +2,5).
    {
        ODVoxAudioProcessorEditor editor (processor);
        editor.setSize (ODVoxAudioProcessorEditor::kDefaultWidth,
                        ODVoxAudioProcessorEditor::kDefaultHeight);
        capture (editor, "verifications/ui/editeur_defauts.ppm");
    }

    // 2. Tout engaged : LED allumees, vumetres vivants, courbe sculptee —
    //    c'est l'image qui juge la dynamique cuivre/metal.
    {
        setActual (processor, "gate_amount", 40.0f);
        setActual (processor, "lowcut_amount", 1.0f);
        setActual (processor, "eq_low_db", 3.0f);
        setActual (processor, "eq_mid_db", -2.0f);
        setActual (processor, "eq_hi_db", 1.5f);
        setActual (processor, "comp_amount", 60.0f);
        setActual (processor, "deess_amount", 45.0f);
        setActual (processor, "drive_amount", 55.0f);
        setActual (processor, "doubler_amount", 30.0f);
        setActual (processor, "width_amount", 150.0f);
        setActual (processor, "delay_amount", 25.0f);
        setActual (processor, "reverb_small_pct", 35.0f);
        setActual (processor, "reverb_big_pct", 20.0f);
        setActual (processor, "hq_mode", 1.0f);
        setActual (processor, "output_gain_db", -3.0f);

        ODVoxAudioProcessorEditor editor (processor);
        editor.setSize (ODVoxAudioProcessorEditor::kDefaultWidth,
                        ODVoxAudioProcessorEditor::kDefaultHeight);

        // Un rendu de la chaine alimente les vumetres de reduction (Comp, Gate,
        // De-ess) : ils montrent leur etat amorti dans la capture.
        juce::AudioBuffer<float> probe (2, 512);

        for (int i = 0; i < 8; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int j = 0; j < 512; ++j)
                    probe.setSample (ch, j,
                                     0.25f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 620.0 * (double) (i * 512 + j) / 48000.0));

            juce::MidiBuffer midi;
            processor.processBlock (probe, midi);
        }

        capture (editor, "verifications/ui/editeur_engage.ppm");
    }

    std::cout << "captures ecrites dans verifications/ui/\n";
    return 0;
}

// --- Test isole de drawKnob : dessine sur canvas TRANSPARENT, sonde les
//     alphas et exporte la couche composee pour inspection. ---
static void probeDrawKnob()
{
    juce::Image canvas (juce::Image::ARGB, 80, 80, true);
    {
        juce::Graphics g (canvas);
        odvox::skin::drawKnob (g, juce::Rectangle<float> (0, 0, 80, 80), 0.5f);
    }
    juce::Image::BitmapData bits (canvas, juce::Image::BitmapData::readOnly);
    int x0 = 80, y0 = 80, x1 = -1, y1 = -1;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 80; ++x)
            if (bits.getPixelColour (x, y).getAlpha() > 8)
            {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    std::cout << "probe drawKnob 80x80 : bbox contenu = "
              << (x1 >= 0 ? x1 - x0 + 1 : 0) << "px, centre ("
              << (x1 >= 0 ? (x0 + x1) / 2 : -1) << "," << (y1 >= 0 ? (y0 + y1) / 2 : -1)
              << ")\n";
    std::cout << "  alpha coin(2,2)=" << (int) bits.getPixelColour (2, 2).getAlpha()
              << " bord(40,4)=" << (int) bits.getPixelColour (40, 4).getAlpha()
              << " centre(40,40)=" << (int) bits.getPixelColour (40, 40).getAlpha()
              << "\n";

    // La couche composee (ce que drawKnob envoie a l'ecran), exportee en PNG.
    if (odvox::skin::hasKnobAtlas())
    {
        auto layer = odvox::skin::knobLayer (0.5f);
        juce::File out ("verifications/ui/knob_layer.png");
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (layer, stream);
            std::cout << "  couche knobLayer(0.5) exportee : verifications/ui/knob_layer.png\n";
        }
    }

    // drawKnob sur fond reproduit (brushed + sheen), taille d'une cellule :
    // le disque doit y etre VISIBLE — sinon il se fond dans le panneau.
    {
        juce::Image cell (juce::Image::ARGB, 94, 110, true);
        {
            juce::Graphics g (cell);
            odvox::metal::fillBrushed (g, cell.getBounds().toFloat());
            juce::ColourGradient sheen (juce::Colour (0x26ffffff), 0.0f, 0.0f,
                                        juce::Colour (0x2a000000), 0.0f, 110.0f, false);
            g.setGradientFill (sheen);
            g.fillAll();
            odvox::skin::drawKnob (g, juce::Rectangle<float> (6, 6, 82, 88), 0.5f);
        }
        juce::File out ("verifications/ui/knob_on_panel.png");
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (stream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (cell, stream);
            std::cout << "  cellule knob sur panneau : verifications/ui/knob_on_panel.png\n";
        }
    }

    const auto& img = odvox::skin::knobSprite();
    std::cout << "sprite : " << img.getWidth() << "x" << img.getHeight() << "\n";
}
