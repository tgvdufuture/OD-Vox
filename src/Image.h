#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"

#include <array>

namespace odvox
{
    /** Image : doubler et largeur stereo (F1.10, US-08).

        Deux etages en serie, l'ordre du pipeline §3.1 : **Doubler d'abord** (il
        cree la matiere stereo — deux voix desaccordees), puis **Width** (elle
        etiquette la largeur de ce qui existe deja, y compris de ce que le
        doubler vient de produire). Les inverser rendrait le reglage de largeur
        aveugle a son propre compagnon de module.

        **Doubler — cible de conception** : le doubler est « quasi transparent en magnitude, a
        part le gain fixe → copie desaccordee, coherent avec un doubler (delai +
        desaccordage), sans effet de niveau ». Trois consequences prises au
        pied de la lettre :
        - le niveau ne bouge pas : chaque voix copiee est ponderee pour que la
          somme d'energie reste celle de l'entree (voix a −3 dB chacune pour
          deux voix non correlees) ;
        - la copie est retardee ET desaccordee : deux voix par cote, une a
          `+detune` cents et l'autre a `−detune`, retardees de `delay_ms` ±
          une difference (sinon les deux voix d'un meme cote seraient identiques
          a un facteur pres et ne feraient qu'un chorus) ;
        - L et R ne partagent AUCUNE voix : c'est ce qui decorele les canaux, et
          ce que la correlation < 0,5 d'AC4 exige. Un chorus classique (meme LFO
          des deux cotes) ne le satisfait pas.

        **Transparence** : a `doubler_amount` 0 % ET `width_amount` 100 %, le
        module entier est court-circuite — sortie BIT-EXACTE. Le doubler seul a
        0 % est court-circuite aussi (AC4 : difference ≤ −120 dBFS ; ici : nulle).

        **Width — mid/side avec mono bass (AC2)** : M = (L+R)/2, S = (L−R)/2,
        S pondere par la largeur demandee, puis recomposition. La ponderation
        s'applique au-dessus de `monoBassHz` ; en dessous, S passe par un
        passe-haut Linkwitz-Riley 4, donc le grave redevient mono (S ≈ 0) alors
        que le haut porte la largeur. AC1 est structurel : a 100 % le poids vaut
        1, le module est court-circuite, le signal est inchange au bit pres ; a
        0 % S est nulle, L = R = M : strictement mono (correlation 1,0).

        **AC3 (pas de subsonique)** : les differences L−R et les recompositions
        M/S ne creent aucune composante qui n'existait pas — pas d'offset, pas
        de modulation. C'est verifie au signal (bande 0-20 Hz plancher). */
    class Image
    {
    public:
        static constexpr int kVoicesPerSide = 2;

        struct Settings
        {
            // Doubler. Defauts du struct : actifs ; le PROCESSEUR pose `inert`,
            // et le defaut du catalogue est amount 0 % (AC2 de US-02).
            float amount01     = 1.0f;
            float detuneCents  = 12.0f;
            float delayMs      = 22.0f;

            // Width. 1,0 = neutre (defaut du catalogue), 0 = mono, 2 = double.
            float width        = 1.0f;
            float monoBassHz   = 120.0f;

            bool  inert        = false;   // court-circuit du module ENTIER
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        void setSettings (const Settings&);

        /** Traite un echantillon sur place (convention de la chaine). Sur un
            signal mono, le module est traversé sans effet : il n'y a rien a
            elargir ni a decorreler, et inventer un canal n'est pas son travail
            (l'hote fournit le stereo). */
        float processSample (float* const* channels, int sampleIndex);

    private:
        void refreshVoices();

        double sampleRateD = 0.0;
        int numChannels = 2;

        Settings settings;

        // --- Doubler : LFO de desaccordage + delais par voix -----------------
        // Le desaccordage n'est PAS statique : un detune fixe produit deux
        // sinus a frequence fixe (battement constant), un doubler doit DERIVER
        // — chaque voix suit sa propre phase, comme deux chanteurs qui ne
        // restent jamais exactement d'accord.
        struct Voice
        {
            float delaySamples = 0.0f;      // delai de base, en echantillons
            float detuneCents  = 0.0f;      // desaccordage de cette voix
            float phase        = 0.0f;      // phase du LFO de derive
            float rateHz       = 0.0f;      // vitesse de derive
            std::array<float, 960> line {}; // ligne a delai (~20 ms a 48 kHz)
            int   write        = 0;
        };

        std::array<Voice, kVoicesPerSide * 2> voices;   // 2 par cote

        float gainPerVoice = 0.0f;   // normalisation d'energie, recalculee

        // --- Width : mono bass par LR4 ---------------------------------------
        // Le side grave est COUPE (mono strict sous la coupure), le side aigu
        // porte la largeur. Le filtre est un Linkwitz-Riley 4 (deux etages du
        // 2e ordre au meme coin) : sa pente de −24 dB/octave fait tomber le side
        // a moins de 1 % une octave et demie sous la coupure.
        //
        // C'est un PASSE-HAUT, et non « le side moins son passe-bas ». Cette
        // derniere forme, retenue d'abord, n'en est PAS un : au coin, le retard
        // de phase du passe-bas rend la soustraction presque perpendiculaire,
        // si bien que le grave ressortait ELARGI (mesure du 2026-09-19 : side a
        // 60 Hz a 2,67x l'entree, la ou il fallait 0,12x).
        Biquad sideHigh;       // LR4 du side : la moitie aigue, qui porte la largeur
        Biquad sideHighCross;  // second etage du LR4 (2 x 2 ordres = 4)

        float widthSmoothed = 1.0f;   // pas de zipper noise sur S
        float widthStep     = 0.0f;
        float widthTarget   = 1.0f;

        bool monoBassActive = false;  // coupure dans la plage utile ?

        bool builtVoicesValid = false;
        float builtDetune = -1.0f;
        float builtDelayMs = -1.0f;
    };
}
