#pragma once

#include "Eq.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <array>

namespace odvox
{
    /** La geometrie de l'ECRAN LCD Figma (docs/assets/LCD_fige.png, 753x419),
        mesuree au pixel (tools/mockup_lcd.html porte les memes constantes).

        L'ecran est un objet physiquement fixe : son axe ne remplit pas la
        fenetre, il vit DANS le plot cadre par le bezel. Ces constantes sont la
        seule definition de cet axe — le mockup HTML et la vue C++ les lisent,
        donc le dessin et le geste ne peuvent pas diverger.

        Unites : pixels reels de l'image 753x419. L'unite LCD vaut 2 px (tous
        les traits de l'export font 2 px), et tout element dessine doit tomber
        sur la grille paire — sinon le rendu devient flou (sous-pixel). */
    namespace lcd
    {
        inline constexpr int   kWidth  = 753;
        inline constexpr int   kHeight = 419;

        /** L'interieur du plot (excluant le cadre de 3 px). */
        inline constexpr float kPlotX0 = 196.0f, kPlotX1 = 620.0f;
        inline constexpr float kPlotY0 = 117.0f, kPlotY1 = 272.0f;

        /** L'axe des frequences, mesure sur les labels : « 100 » centre a x244,
            « 1k » a x408, « 10k » a x574 — soit 165 px par DECADE, log. */
        inline constexpr float kXAt100Hz = 244.0f;
        inline constexpr float kDecadePx = 165.0f;

        /** L'axe des gains : bande pointillee 0 dB centree a y195 (mesuree
            193-196), labels +12 / -12 centres a y126 / y264 — soit 5.75 px par
            dB, exactement symetriques autour de 195. */
        inline constexpr float kZeroY   = 195.0f;
        inline constexpr float kPxPerDb = (264.0f - 126.0f) / 24.0f;

        float xForFrequency (float hz) noexcept;
        float frequencyForX (float x) noexcept;
        float yForGain (float db) noexcept;
        float gainForY (float y) noexcept;

        /** L'axe GENERIQUE (20 Hz..20 kHz, ±15 dB) projete dans l'ecran LCD, en
            FRACTIONS de l'image (0..1) : c'est le viewport que la vue passe au
            modele (qui le multiplie par sa taille reelle). */
        juce::Rectangle<float> axisViewport() noexcept;
    }

    /** Les mappages de la courbe d'EQ (lot F1.6c) : **purs**, sans interface.

        L'axe des frequences est **logarithmique** — c'est ce qui rend les octaves
        lisibles et ce qui fait qu'une courbe d'EQ se lit comme une oreille
        l'entend. L'axe des gains est lineaire, en dB. Ces quatre fonctions sont
        l'unique definition de la geometrie : la vue dessine avec elles, et le
        modele convertit les gestes avec elles. Comme elles sont pures, le test
        les exerce sans ouvrir de fenetre. */
    namespace eqcurve
    {
        inline constexpr float kMinHz = 20.0f;
        inline constexpr float kMaxHz = 20000.0f;

        /** Bornes verticales de l'affichage. Elles valent la plage des gains du
            catalogue (−15..+15 dB) : une poignee ne peut donc pas tomber hors du
            cadre, et tirer a fond donne exactement la butee du parametre. */
        inline constexpr float kMaxGainDb = 15.0f;

        /** Rayon de saisie d'une poignee, en pixels. Plus large que le dessin de
            la poignee : on vise une bande, pas un pixel. */
        inline constexpr float kHitRadius = 18.0f;

        float frequencyForX (float x, float width) noexcept;
        float xForFrequency (float hz, float width) noexcept;
        float gainForY (float y, float height) noexcept;
        float yForGain (float gainDb, float height) noexcept;

        /** Les quatre clefs de bande, dans l'ordre du catalogue : c'est le seul
            endroit qui compose `eq_<cle>_<reglage>`, donc le seul a corriger si
            un identifiant change. */
        extern const char* const kBandKeys[Eq::kNumBands];
    }

    /** Le modele de la courbe d'EQ : ce que le geste FAIT.

        Le composant ne decide rien : il transmet des points, et c'est ici que la
        bande visee est trouvee, que les quatre parametres sont ecrits, et que les
        gestes d'automation de l'hote sont ouverts et fermes. Tout le lot F1.6c
        est donc verifiable **sans fenetre** — ce qui est la seule facon de
        verifier AC1 de US-04 autrement qu'en cliquant a la main.

        Ce que le modele ecrit, et sous quels gestes :
        - glisser-deposer  : frequence (horizontal) et gain (vertical),
        - molette          : largeur de bande (Q),
        - menu contextuel  : type de bande (cloche / shelf bas / shelf haut /
          passe-haut), ouvert par la vue et applique ici. */
    class EqCurveModel
    {
    public:
        struct Point { float x = 0.0f, y = 0.0f; };

        EqCurveModel (ODVoxAudioProcessor&, int width, int height);

        /** Taille de la zone de dessin, en pixels. */
        void setSize (int width, int height) noexcept;

        /** Le rectangle de la VUE (en pixels) dans lequel l'axe generique
            [20 Hz..20 kHz] x [±15 dB] est dessine. Par defaut (et pour tous les
            tests existants) il remplit la vue ; l'ecran LCD le retrecit a son
            plot mesure — les poignees et les gestes sont alors projetes dedans.

            Un seul appel au montage : la geometrie d'un ecran fixe ne change
            pas en cours de vie. */
        void setViewport (juce::Rectangle<float> axisFractions) noexcept;

        /** Bande saisie pour ce point, -1 si aucune. La plus proche dans le rayon
            de saisie. */
        int bandAt (Point) const;

        /** Position de la poignee d'une bande, en pixels (projetee dans le
            viewport — sur l'ecran LCD, les positions mesurees de la maquette). */
        Point handlePosition (int band) const;

        /** Ouvre un geste sur la bande visee. Faux si aucune bande n'est la. */
        bool beginGesture (Point);

        /** Ecrit frequence et gain de la bande saisie. Faux si aucun geste. */
        bool dragTo (Point);

        /** Ferme le geste aupres de l'hote (et seulement s'il en ouvre un). */
        void endGesture();

        bool isDragging() const noexcept { return dragging; }
        int  activeBand() const noexcept { return active; }

        /** Molette : une encoche double ou divise le Q (largeur de bande). */
        bool adjustQ (int band, float wheelDeltaY);

        /** Type de bande, indexe comme les libelles du catalogue
            (`Bell|Low Shelf|High Shelf|High Pass`). */
        bool setBandType (int band, int typeIndex);

        /** Double-clic : la bande revient aux defauts du CATALOGUE, comme les
            rotaries (AC2 de US-02). Le modele ne connait pas ces defauts par
            coeur, il les lit dans le catalogue — sinon deux verites.

            Le gain repart donc a 0 dB (et l'Air a +2,5, sa valeur voulue), la
            frequence et le Q a leur defaut mesure, et le type a son defaut. */
        bool resetBand (int band);

        /** Le gain d'une bande, tel que les parametres le portent (et non le
            reglage lissee du DSP : la courbe montre la CIBLE). */
        float gainOf (int band) const;
        float frequencyOf (int band) const;
        float qOf (int band) const;
        int   typeOf (int band) const;

        /** Les reglages des quatre bandes, tels que la courbe doit se tracer. */
        Eq::Settings settings() const;

    private:
        /** `eq_<cle>_<reglage>` : compose au meme endroit que les clefs de
            bande, pour qu'il n'existe qu'une facon de nommer un parametre. */
        static juce::String idFor (int band, const char* setting);

        /** Borne la valeur a la plage du CATALOGUE avant d'ecrire : le modele ne
            decide pas des plages, il les respecte. */
        void write (int band, const char* setting, float actual) const;

        /** La position d'une poignee : fraction generique -> pixel de la vue,
            via le viewport (en fractions de la vue). Privee : handlePosition
            en est la facade publique. */
        Point mappedHandle (int band) const;

        ODVoxAudioProcessor& processor;
        int width = 1, height = 1;

        /** Le viewport de l'axe, en FRACTIONS de la vue (0..1). Par defaut il
            remplit la vue : le comportement historique (et celui des tests).
            L'ecran LCD le retrecit via setViewport. */
        juce::Rectangle<float> viewport { 0.0f, 0.0f, 1.0f, 1.0f };

        int active = -1;
        bool dragging = false;

        JUCE_DECLARE_NON_COPYABLE (EqCurveModel)
    };
}
