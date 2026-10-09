#pragma once

#include "Metal.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <ODVoxBinaryData.h>
#include <knob_atlas_meta.h>

namespace odvox::skin
{
    /** Decode un PNG du binaire DIRECTEMENT en image LOGICIELLE.

        Deux interdits ici, tous deux mortels au dechargement de la DLL :
        - ImageCache::getFromMemory : le cache garde l'image decodee (D2D)
          dans une statique du module — au FreeLibrary, sa destruction
          libere le device D3D11 partage pendant DllMain => l'hote se
          fige (dump FL du 29/09 : igd10um64xe <- d3d11!Release <-
          OD_Vox atexit <- LdrUnloadDll) ;
        - le constructeur Image par defaut : sur Windows JUCE 8 il est
          D2D (voir metal::softwareImage pour l'analyse).

        PNGImageFormat decode, SoftwareImageType convertit : l'image vit
        en RAM, sa destruction est un delete de HeapBlock. */
    inline juce::Image decodePng (const char* data, int size)
    {
        if (data == nullptr || size <= 0)
            return {};

        juce::MemoryInputStream stream (data, (size_t) size, false);
        return metal::softwareImage (juce::PNGImageFormat().decodeImage (stream));
    }

    /** Charge les sprites de l'interface (mode « assets individuels »,
        docs/FIGMA_KIT.md §9).

        Les images vivent dans le binaire via juce_add_binary_data(ODVoxAssets)
        (CMakeLists.txt) et sont chargees UNE FOIS au premier appel. Si un
        sprite manque (pas encore dessine), sprite() retourne une image nulle
        et l'appelant retombe sur le dessin vectoriel maison : on peut livrer
        un melange image/dessin a chaque etape.

        Convention : PNG fond transparent, exporte en 2x. La taille de dessin
        @1x est la moitie de la taille du fichier. L'encoche des knobs est
        dessinee en position midi (haut) dans le sprite ; la rotation selon la
        valeur est faite par l'appelant. */
    inline const juce::Image& knobSprite()
    {
        static const juce::Image knob = [] () -> juce::Image
        {
            int size = 0;
            if (auto* data = ODVoxBinary::getNamedResource ("knob_png", size))
            {
                auto original = decodePng (data, size);
                if (! original.isValid())
                    return {};

                // 1) Bounding box du contenu opaque (l'export Figma garde de
                //    larges marges transparentes : les dessiner ferait un knob
                //    plus petit que sa case et un pivot de rotation decale).
                juce::Image::BitmapData bits (original, juce::Image::BitmapData::readOnly);
                int x0 = original.getWidth(), y0 = original.getHeight();
                int x1 = -1, y1 = -1;
                for (int y = 0; y < original.getHeight(); ++y)
                    for (int x = 0; x < original.getWidth(); ++x)
                        if (bits.getPixelColour (x, y).getAlpha() > 8)
                        {
                            if (x < x0) x0 = x;
                            if (x > x1) x1 = x;
                            if (y < y0) y0 = y;
                            if (y > y1) y1 = y;
                        }
                if (x1 < 0)
                    return {};

                const int bw = x1 - x0 + 1;
                const int bh = y1 - y0 + 1;

                // 2) Pivot = centre du DISQUE, pas du bbox : le sprite porte
                //    souvent un onglet (repere de valeur) qui decale le bbox.
                //    Le disque, lui, est la rangee la plus large de pixels
                //    opaques — robuste meme avec l'onglet.
                int bestRow = y0, bestRowCount = -1;
                for (int y = y0; y <= y1; ++y)
                {
                    int count = 0;
                    for (int x = x0; x <= x1; ++x)
                        if (bits.getPixelColour (x, y).getAlpha() > 8) ++count;
                    if (count > bestRowCount) { bestRowCount = count; bestRow = y; }
                }
                int bestCol = x0, bestColCount = -1;
                for (int x = x0; x <= x1; ++x)
                {
                    int count = 0;
                    for (int y = y0; y <= y1; ++y)
                        if (bits.getPixelColour (x, y).getAlpha() > 8) ++count;
                    if (count > bestColCount) { bestColCount = count; bestCol = x; }
                }
                const int discCx = bestCol;
                const int discCy = bestRow;

                // 3) Carre centre sur le pivot du disque : la rotation tourne
                //    autour du centre de la case, donc le disque doit etre
                //    centre dans son canevas (l'onglet suit, c'est voulu).
                const int side = juce::jmax (bw, bh);
                juce::Image squared (juce::Image::ARGB, side, side, true,
                                     juce::SoftwareImageType {});
                {
                    juce::Graphics g (squared);
                    g.drawImage (original,
                                 (float) (side / 2 - discCx + x0),
                                 (float) (side / 2 - discCy + y0),
                                 (float) original.getWidth(), (float) original.getHeight(),
                                 0.0f, 0.0f,
                                 (float) original.getWidth(), (float) original.getHeight());
                }

                // 3) Copie de travail 256 px (qualite élevée, un seul
                //    reechantillonnage au demarrage : dessiner vingt knobs
                //    1254 px reechantilles a chaque frame couterait trop).
                constexpr int kWorkSize = 256;
                juce::Image work (juce::Image::ARGB, kWorkSize, kWorkSize, true,
                                  juce::SoftwareImageType {});
                {
                    juce::Graphics g (work);
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g.drawImage (squared, work.getBounds().toFloat());
                }
                return work;
            }
            return {};
        } ();
        return knob;
    }

    /** --- Atlas de frames « 3D » ---------------------------------------

        Le sprite unique tourne, mais sa lumiere tourne avec lui : c'est ce
        qui trahit l'image plate. L'atlas est rendu hors ligne
        (tools/generate_knob_frames.py) avec une lumiere FIXE A L'ECRAN : le
        molete et les stries tournent SOUS la lumiere, comme une vraie
        piece. 61 frames couvrent les 270 deg de course (pas de 4,5 deg).

        L'atlas est un fichier binaire de plus ; s'il manque (ressource non
        generee), la couche retombe sur le sprite unique, puis sur le dessin
        vectoriel : la chaine de secours reste intacte. */
    inline const juce::Image& knobAtlas()
    {
        static const juce::Image atlas = [] () -> juce::Image
        {
            int size = 0;
            if (auto* data = ODVoxBinary::getNamedResource ("knob_atlas_png", size))
                return decodePng (data, size);
            return {};
        } ();
        return atlas;
    }

    inline bool hasKnobAtlas() { return knobAtlas().isValid(); }

    /** --- Fond du plugin -----------------------------------------------

        La plaque d'alu noir brossé (Figma, docs/assets/Background_plugin.png)
        recouvre TOUTE la fenêtre : c'est elle la référence visuelle maintenant,
        les éléments existants se redessineront par-dessus au fur et à mesure.
        Dessinée AJUSTEe a la fenêtre (le ratio du PNG matche kDefault). */
    inline const juce::Image& background()
    {
        static const juce::Image bg = [] () -> juce::Image
        {
            int size = 0;
            if (auto* data = ODVoxBinary::getNamedResource ("background_png", size))
                return decodePng (data, size);
            return {};
        } ();
        return bg;
    }

    inline bool hasBackground() { return background().isValid(); }

    /** --- L'ecran LCD de l'EQ -------------------------------------------

        Le bezel Figma (docs/assets/LCD_bezel.png, genere par
        tools/clean_lcd_bezel.py depuis la maquette LCD_fige.png) : bezel, vis,
        vitre, cadre du plot et labels — SANS la courbe, les handles, les
        pointilles ni la ligne 0 dB, qui sont dessines en code par la vue
        (EqCurveView), sur la geometrie mesuree (odvox::lcd dans EqCurve.h). */
    inline const juce::Image& lcdBezel()
    {
        static const juce::Image bezel = [] () -> juce::Image
        {
            int size = 0;
            if (auto* data = ODVoxBinary::getNamedResource ("lcd_bezel_png", size))
                return decodePng (data, size);
            return {};
        } ();
        return bezel;
    }

    inline bool hasLcdBezel() { return lcdBezel().isValid(); }

    /** Image du knob (fonction de la valeur) : 61 couches de la meme piece,
        chacune avec son propre eclairage. */
    inline juce::Image knobLayer (float normalised)
    {
        // Le knob doit rester CENTRE dans sa frame : la moindre erreur de
        // sous-pixel se verrait comme un tremblement de tout le plug.
        const auto& atlas = knobAtlas();

        const float v = juce::jlimit (0.0f, 1.0f, normalised);
        const float pos = v * (float) (knobatlas::kFrames - 1);
        const int frame = (int) pos;

        // Crossfade 50/50 entre la frame inferieure et la frame superieure :
        // 61 angles sur 270 deg laissent 4,5 deg entre deux couches, le fondu
        // rend le drag continu. Melange ARGB en memoire (61 drawImage avec
        // opacites variables par repaint couterait plus cher).
        // Image LOGICIELLE : la couche est produite a chaque geste du knob ;
        // meme si elle ne vivait qu'une frame, la creator D2D toucherait au
        // device partage. Regle du fichier : aucune image GPU nait ici.
        juce::Image layer (juce::Image::ARGB, knobatlas::kFrameSize, knobatlas::kFrameSize, true,
                           juce::SoftwareImageType {});
        {
            juce::Graphics g (layer);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

            const int col = frame % knobatlas::kColumns;
            const int row = frame / knobatlas::kColumns;
            g.setOpacity (1.0f);
            g.drawImage (atlas,
                         0, 0, knobatlas::kFrameSize, knobatlas::kFrameSize,
                         col * knobatlas::kFrameSize, row * knobatlas::kFrameSize,
                         knobatlas::kFrameSize, knobatlas::kFrameSize,
                         false);

            if (const float t = pos - (float) frame; t > 0.0001f && frame + 1 < knobatlas::kFrames)
            {
                const int ncol = (frame + 1) % knobatlas::kColumns;
                const int nrow = (frame + 1) / knobatlas::kColumns;
                g.setOpacity (t);
                g.drawImage (atlas,
                             0, 0, knobatlas::kFrameSize, knobatlas::kFrameSize,
                             ncol * knobatlas::kFrameSize, nrow * knobatlas::kFrameSize,
                             knobatlas::kFrameSize, knobatlas::kFrameSize,
                             false);
            }
        }
        return layer;
    }

    /** Le knob a-t-il ses couches 3D (sinon : sprite unique, puis dessin) ? */
    inline bool hasKnobSprite() { return knobSprite().isValid(); }

    /** Dessine le knob pour `normalised` (0..1).

        Trois couches possibles, de la plus riche a la plus sure :
        1. atlas de frames (lumiere fixe, le « semblant de 3D ») ;
        2. sprite unique tourne (lumiere cuite, l'ancien rendu) ;
        3. rien : l'appelant (PluginEditor) retombe sur le dessin vectoriel. */
    inline void drawKnob (juce::Graphics& g, const juce::Rectangle<float>& bounds,
                          float normalised)
    {
        // 1. Atlas : chaque valeur a SA couche, avec son propre eclairage.
        if (hasKnobAtlas())
        {
            const auto layer = knobLayer (normalised);

            // La frame porte le disque (86 % de la frame) + son ombre
            // portee : on dessine la frame entiere, ce qui met le disque a
            // ~80 % de la case — la meme taille que le sprite unique.
            const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.93f;
            const auto centre = bounds.getCentre();
            g.drawImage (layer,
                         centre.x - side * 0.5f, centre.y - side * 0.5f, side, side,
                         0.0f, 0.0f, (float) layer.getWidth(), (float) layer.getHeight(),
                         false);
            return;
        }

        // 2. Sprite unique : le rendu d'origine, lumiere cuite dans l'image.
        const auto& img = knobSprite();
        if (! img.isValid())
            return;

        const float angle = (juce::jlimit (0.0f, 1.0f, normalised) - 0.5f) * 270.0f;

        juce::Graphics::ScopedSaveState state (g);
        const auto centre = bounds.getCentre();
        g.addTransform (juce::AffineTransform::rotation (
            angle * juce::MathConstants<float>::pi / 180.0f, centre.x, centre.y));

        // Le sprite AJUSTE a la case, avec de l'air : le disque occupe ~80 %
        // du plus petit cote (le sprite contient disque + onglet, le disque
        // seul fait ~92 % du canevas carre). Rien ne touche les voisins.
        const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.87f;
        g.drawImage (img,
                     centre.x - side * 0.5f, centre.y - side * 0.5f, side, side,
                     0.0f, 0.0f, (float) img.getWidth(), (float) img.getHeight(),
                     false);
    }
}
