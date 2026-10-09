#pragma once

#include "Palette.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace odvox::metal
{
    inline juce::Colour colour (uint32_t argb) noexcept { return juce::Colour (argb); }

    /** Force une image en RAM, jamais sur le GPU.

        Pourquoi ce vestibule existe : sur Windows, JUCE 8 cree les images
        par defaut en Direct2D (NativeImageType -> Direct2DPixelData, un
        texte D3D11 — voir juce_Direct2DImage_windows.cpp, qui le dit
        lui-meme : "the caller may be trying to create an Image from a
        static variable; if this is a DLL, then this is probably called
        from DllMain"). Nos textures vivent dans des statiques ; au
        dechargement de la DLL, leur destructeur libere le device D3D11
        PARTAGE pendant DllMain(PROCESS_DETACH), donc sous loader lock —
        le pilote attend un thread qui ne peut plus s'executer, et l'hote
        se fige pour toujours. Pile mesuree dans le dump FL du 29/09 :
        igd10um64xe (pilote Intel) <- d3d11!CDevice::Release <- OD_Vox
        (atexit) <- LdrUnloadDll.

        SoftwareImageType rend la destruction banale (un HeapBlock se
        libere sans le GPU) : meme contenu, meme rendu, plus aucun
        capitaux graphique retenu au-dela de la vie de l'editeur. */
    inline juce::Image softwareImage (juce::Image image)
    {
        if (! image.isValid())
            return image;

        return juce::SoftwareImageType().convert (image);
    }

    /** Texture de metal brosse, generee UNE FOIS (static local) et tuilee.

        Le brossage est horizontal : chaque ligne de la tuile porte une
        luminance aleatoire constante sur sa largeur — c'est ce qui fait les
        stries du metal. Le tirage est semence (graine fixe) pour que deux
        ouvertures de fenetre rendent le meme panneau : une texture qui change
        a chaque repaint clignote. La tuile fait 8 px de haut : au-dela, les
        stries se voient comme un motif ; en dessous, elles se moyennent. */
    inline const juce::Image& brushedTile()
    {
        static const juce::Image tile = []
        {
            juce::Image image (juce::Image::ARGB, 64, 8, true, juce::SoftwareImageType {});
            juce::Random rng (0x0D507EED);

            for (int y = 0; y < 8; ++y)
            {
                // Luminance de la ligne : ±6 autour du neutre, plus une variation
                // lente (le rng est partage) pour casser l'egalisage des stries.
                const int jitter = (int) (rng.nextFloat() * 12.0f) - 6;

                for (int x = 0; x < 64; ++x)
                {
                    const int base = 0xb8 + jitter;
                    image.setPixelAt (x, y, juce::Colour ((uint8_t) base, (uint8_t) (base - 1),
                                                          (uint8_t) (base - 5)));
                }
            }

            return image;
        }();

        return tile;
    }

    /** Remplit `bounds` avec l'alu brosse (tuile horizontalement, lignes
        re-echantillonnees verticalement par le tiling standard de JUCE). */
    inline void fillBrushed (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const auto& tile = brushedTile();

        for (float y = bounds.getY(); y < bounds.getBottom(); y += (float) tile.getHeight())
        {
            const float height = juce::jmin ((float) tile.getHeight(), bounds.getBottom() - y);

            for (float x = bounds.getX(); x < bounds.getRight(); x += (float) tile.getWidth())
            {
                const float width = juce::jmin ((float) tile.getWidth(), bounds.getRight() - x);
                g.drawImage (tile, x, y, width, height,
                             0.0f, 0.0f, width, height);
            }
        }
    }

    /** Panneau complet : alu brosse + degrade de lumiere (plus clair en haut,
        comme une plaque eclairee du plafond) + biseau. `radius` 0 = plaque
        carree (le fond du plugin), arrondi = carte. */
    inline void panel (juce::Graphics& g, const juce::Rectangle<float>& bounds, float radius)
    {
        // Le brosse d'abord, puis un voile vertical par-dessus : la lumiere
        // vient du haut, c'est ce qui donne la lecture "plaque metallique".
        fillBrushed (g, bounds);

        auto path = juce::Path();
        path.addRoundedRectangle (bounds, radius);

        juce::ColourGradient sheen (colour (0x22ffffff), 0.0f, bounds.getY(),
                                    colour (0x1a000000), 0.0f, bounds.getBottom(), false);

        g.saveState();
        g.reduceClipRegion (path);
        g.setGradientFill (sheen);
        g.fillRect (bounds);
        g.restoreState();

        // Biseau : lumiere au-dessus, ombre en dessous — c'est l'epaisseur du
        // metal. Le contour final defint la plaque.
        g.setColour (colour (palette::metalHi).withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.translated (0.0f, 0.5f).reduced (0.5f), radius, 1.0f);
        g.setColour (colour (palette::metalLo).withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.translated (0.0f, -0.5f).reduced (0.5f), radius, 1.0f);
        g.setColour (colour (palette::cardBorder).withAlpha (0.8f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    /** Trou enfonce dans le metal (ecrans, piste des vumetres) : fond noir,
        ombre en haut, lumiere en bas — la lumiere ne peut pas venir du trou. */
    inline void recessed (juce::Graphics& g, const juce::Rectangle<float>& bounds, float radius)
    {
        g.setColour (colour (palette::track));
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (colour (0x54000000));
        g.drawRoundedRectangle (bounds.translated (0.0f, -0.5f).reduced (0.5f), radius, 1.0f);
        g.setColour (colour (0x38ffffff));
        g.drawRoundedRectangle (bounds.translated (0.0f, 0.5f).reduced (0.5f), radius, 1.0f);
    }

    /** Corps de knob : disque metal avec biseau (lumiere en haut-gauche,
        ombre en bas-droite) et chapeau central plus sombre. Le texte de valeur
        est dessine par l'appelant, au-dessus. */
    inline void knob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01)
    {
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 5.0f;

        // Ombre portee : le knob flotte au-dessus de la plaque.
        g.setColour (colour (0x59000000));
        g.fillEllipse (centre.x - radius + 1.5f, centre.y - radius + 2.5f,
                       radius * 2.0f, radius * 2.0f);

        // Corps : degrade radial clair -> sombre, comme une piece tournee.
        juce::ColourGradient body (colour (palette::metalHi), centre.x - radius * 0.4f,
                                   centre.y - radius * 0.5f,
                                   colour (palette::metalLo), centre.x + radius * 0.3f,
                                   centre.y + radius * 0.6f, true);
        g.setColour (colour (palette::knobBody));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

        {
            juce::Path bodyClip;
            bodyClip.addEllipse (centre.x - radius, centre.y - radius,
                                 radius * 2.0f, radius * 2.0f);
            g.saveState();
            g.reduceClipRegion (bodyClip);
            g.setGradientFill (body);
            g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            g.restoreState();
        }

        // Contour de definition.
        g.setColour (colour (palette::cardBorder));
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        // Chapeau central : la piece qui se visse, un ton plus sombre.
        const float capRadius = radius * 0.62f;
        g.setColour (colour (palette::knobCap));
        g.fillEllipse (centre.x - capRadius, centre.y - capRadius,
                       capRadius * 2.0f, capRadius * 2.0f);
        g.setColour (colour (palette::metalLo).withAlpha (0.5f));
        g.drawEllipse (centre.x - capRadius, centre.y - capRadius,
                       capRadius * 2.0f, capRadius * 2.0f, 1.0f);

        // Repere : une encoche gravee, du chapeau vers le bord, a l'angle de la
        // valeur. C'est elle qui lit la position, plus qu'un arc de couleur.
        constexpr float startAngle = -juce::MathConstants<float>::pi * 0.75f;
        constexpr float span       =  juce::MathConstants<float>::pi * 1.5f;
        const float angle = startAngle + span * juce::jlimit (0.0f, 1.0f, value01);

        const float inner = capRadius * 0.25f;
        const float outer = capRadius * 0.9f;

        g.setColour (colour (0x66000000));
        g.drawLine (juce::Line<float> (
                        centre.x + std::sin (angle) * inner + 0.5f,
                        centre.y - std::cos (angle) * inner + 1.0f,
                        centre.x + std::sin (angle) * outer + 0.5f,
                        centre.y - std::cos (angle) * outer + 1.0f),
                    2.0f);
        g.setColour (colour (palette::text));
        g.drawLine (juce::Line<float> (
                        centre.x + std::sin (angle) * inner,
                        centre.y - std::cos (angle) * inner,
                        centre.x + std::sin (angle) * outer,
                        centre.y - std::cos (angle) * outer),
                    2.0f);
    }

    /** Texte grave : ombre claire en bas (le metal rebondit la lumiere), texte
        charbon au-dessus. Un decalage d'un demi-pixel suffit. */
    inline void engraved (juce::Graphics& g, const juce::String& text,
                          const juce::Rectangle<float>& area,
                          juce::Justification justification, float height, bool bold)
    {
        g.setFont (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));

        g.setColour (colour (0x30ffffff));
        g.drawText (text, area.translated (0.0f, 0.5f), justification, true);

        g.setColour (colour (palette::text));
        g.drawText (text, area, justification, true);
    }

    /** Look-and-feel maison : les boutons de la barre superieure et les menus
        deviennent des plaques de metal boulonnees au panneau. Applique UNE fois
        par editeur (le membre `look` de la classe), jamais globalement — deux
        fenetres de plugin ne doivent pas se battre pour le L&F global. */
    class HardwareLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HardwareLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, colour (palette::card));
            setColour (juce::PopupMenu::highlightedBackgroundColourId, colour (palette::accent));
            setColour (juce::PopupMenu::textColourId, colour (palette::text));
            setColour (juce::PopupMenu::headerTextColourId, colour (palette::text));
            setColour (juce::ComboBox::backgroundColourId, colour (palette::track));
            setColour (juce::ComboBox::outlineColourId, colour (palette::cardBorder));
            setColour (juce::ComboBox::textColourId, colour (palette::text));
            setColour (juce::ComboBox::arrowColourId, colour (palette::textDim));
            setColour (juce::ComboBox::buttonColourId, colour (palette::accent));
            setColour (juce::Label::textColourId, colour (palette::text));
            setColour (juce::TooltipWindow::backgroundColourId, colour (palette::track));
            setColour (juce::TooltipWindow::textColourId, colour (0xffe8e4dc));
            setColour (juce::TooltipWindow::outlineColourId, colour (palette::cardBorder));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            juce::ignoreUnused (backgroundColour);

            const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
            const float radius = juce::jmin (6.0f, bounds.getHeight() * 0.3f);

            // La plaque : plus sombre au clic (elle s'enfonce), plus claire au
            // survol. Le bouton est une piece, pas une couleur.
            if (down)
            {
                metal::recessed (g, bounds, radius);
                g.setColour (colour (palette::knobCap));
                g.fillRoundedRectangle (bounds.reduced (2.0f), juce::jmax (2.0f, radius - 2.0f));
            }
            else
            {
                g.setColour (colour (0x28000000));
                g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), radius);
                metal::panel (g, bounds, radius);
            }

            if (highlighted && ! down)
            {
                g.setColour (colour (palette::accent).withAlpha (0.45f));
                g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 1.5f);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int height) override
        {
            return juce::Font (juce::FontOptions ((float) juce::jmax (11, height / 2), juce::Font::bold));
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                             bool /*highlighted*/, bool /*down*/) override
        {
            metal::engraved (g, button.getButtonText(), button.getLocalBounds().toFloat(),
                             juce::Justification::centred,
                             (float) button.getHeight() * 0.42f, true);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool highlighted, bool down) override
        {
            juce::ignoreUnused (highlighted, down);

            const auto bounds = button.getLocalBounds().toFloat();
            const bool on = button.getToggleState();

            // Interrupteur a droite (la position EST l'etat, pas la couleur),
            // libelle grave a gauche — comme un selecteur de panel.
            const float trackH = juce::jmin (18.0f, bounds.getHeight() - 4.0f);
            const float trackW = trackH * 1.6f;
            const juce::Rectangle<float> track (bounds.getRight() - trackW - 2.0f,
                                                bounds.getCentreY() - trackH * 0.5f,
                                                trackW, trackH);

            metal::recessed (g, track, trackH * 0.5f);

            const float knobSide = trackH - 2.0f;
            const auto knob = on ? juce::Rectangle<float> (track.getRight() - knobSide - 1.0f,
                                                           track.getY() + 1.0f,
                                                           knobSide, knobSide)
                                 : juce::Rectangle<float> (track.getX() + 1.0f,
                                                           track.getY() + 1.0f,
                                                           knobSide, knobSide);

            g.setColour (colour (on ? palette::accent : palette::accentCold).withAlpha (on ? 0.95f : 0.55f));
            g.fillRoundedRectangle (knob, knobSide * 0.5f);
            g.setColour (colour (palette::metalHi).withAlpha (0.5f));
            g.drawRoundedRectangle (knob.reduced (0.5f), knobSide * 0.5f, 1.0f);

            const float labelW = juce::jmax (0.0f, track.getX() - bounds.getX() - 4.0f);
            metal::engraved (g, button.getButtonText(),
                             juce::Rectangle<float> (bounds.getX(), bounds.getY(),
                                                     labelW, bounds.getHeight()),
                             juce::Justification::centredLeft, 11.0f, false);
        }
    };
}
