#pragma once

#include "Eq.h"

#include <cstdint>

namespace odvox::palette
{
    /** Identite visuelle OD Vox — « hardware analogique » (2026-09-22).

        Inspiree d'un panneau d'equipement analogique (panneau metal clair,
        details laiton/cuivre, encarts noirs enfonces) : la dominante est un
        aluminium brosse clair, le texte est grave au charbon, l'accent actif est
        un cuivre profond et les ecrans (courbe d'EQ, vumetres) sont des fenetres
        noires enfoncees dans le metal — le vocabulaire d'une piece d'equipement,
        pas d'un logiciel.

        Elle vit dans son propre en-tete parce qu'elle est partagee : l'editeur
        et la courbe d'EQ (F1.6c) la consomment. Les valeurs sont en ARGB
        (0xAARRGGBB), la convention de `juce::Colour`. Les helpers de rendu
        (brossage, biseaux, gravure) vivent dans `Metal.h`. */
    inline constexpr uint32_t background  = 0xffb3afa8;   // alu, fond du panneau
    inline constexpr uint32_t backgroundLo = 0xff98948d;  // alu, bas du degrade
    inline constexpr uint32_t card        = 0xffc9c5be;   // plaque de carte, un ton au-dessus
    inline constexpr uint32_t cardBorder  = 0xff7f7b73;   // contour / gravure
    inline constexpr uint32_t text        = 0xff26231e;   // charbon (texte grave)
    inline constexpr uint32_t textDim     = 0xff6d675e;   // gravure secondaire

    inline constexpr uint32_t accent      = 0xffb5763a;   // cuivre profond (actif)
    inline constexpr uint32_t accentHot   = 0xffe0994f;   // cuivre brillant (survol / LED allumee)
    inline constexpr uint32_t accentCold  = 0xff82878e;   // acier (inactif)

    /** L'accent VIOLET de la top bar (mockup tools/mockup_topbar.html) : les
        memes six constantes que les variables CSS du mockup — changer la
        teinte du plugin se fera en un seul endroit, comme dans le HTML. La
        sérigraphie est plate (PAS de glow : une encre ne brille pas), les LED
        sont saturées sans bloom — vocabulaire validé par l'utilisateur. */
    inline constexpr uint32_t inkAccent   = 0xffbd9bff;   // sérigraphie / valeurs
    inline constexpr uint32_t ledHi       = 0xffe8dcff;   // LED, centre (le plus clair)
    inline constexpr uint32_t ledMid      = 0xffa86ef5;   // LED, milieu du dégradé
    inline constexpr uint32_t ledLow      = 0xff6b3fc4;   // LED, bord du dégradé / veilleuse
    inline constexpr uint32_t ledRing     = 0xff4d3385;   // bague / bordure de couronne
    inline constexpr uint32_t ledGlow     = 0x9a5f5fff;   // liseré "allumé" (alpha déjà dans la valeur)

    inline constexpr uint32_t track       = 0xff26231f;   // fenetres enfoncees (ecrans, trous)

    /** Le vocabulaire metal : les biseaux et les corps de knob lisent ces
        quatre constantes, la texture de brossage vient de `Metal.h`. */
    inline constexpr uint32_t metalHi     = 0xffedeae2;   // lumiere du biseau (haut)
    inline constexpr uint32_t metalLo     = 0xff757068;   // ombre du biseau (bas)
    inline constexpr uint32_t knobBody    = 0xffbdb9b1;   // corps du knob
    inline constexpr uint32_t knobCap     = 0xffaba79f;   // chapeau central du knob

    /** Une couleur par bande d'EQ, du grave a l'aigu, dans la famille des
        metaux : cuivre, laiton, argent, acier. C'est le seul repere dont on ait
        besoin pour distinguer les quatre poignees d'un coup d'oeil. La taille
        suit `Eq::kNumBands` : retirer ou ajouter une bande ne compilerait plus
        sans lui donner sa couleur. */
    inline constexpr uint32_t band[Eq::kNumBands] =
    {
        0xffd98a4a,   // Low  : cuivre
        0xffe0b96a,   // Mid  : laiton
        0xffc9ccd1,   // High : argent
        0xff9db3c4,   // Air  : acier bleute
    };
}
