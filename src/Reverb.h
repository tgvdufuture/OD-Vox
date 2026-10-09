#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

namespace odvox
{
    /** Reverb a reseau propre (F1.12, US-02) : **4 moteurs CUMULES**, chacun
        avec SON PROPRE CURSEUR public (retour utilisateur du 2026-09-21 :
        "je veux toujours mes 4 knobs pour modifier les differentes reverb
        (short, small, big, lush)").

        **Structure retenue** : les quatre moteurs tournent TOUJOURS en
        parallele, chacun avec son gain independant, enables separes — notre
        preset d'usine « Clean Vocals Reverb » pose 0,181 / 0,200 / 0,152 / 0 :
        TROIS moteurs simultanes. La couleur vient de la SUPERPOSITION (attaque
        dense + corps + traine), que l'utilisateur dose moteur par moteur. Ce
        repere de calage sert aussi a nos autres presets d'usine (Presets.cpp).

        **Famille d'algorithme** : l'architecture est de type Freeverb (lags
        d'autocorrelation a 377/1107/1426 echantillons, tres dense). Nous n'en
        copions PAS les constantes : l'AC4 du lot interdit toute derivation d'un
        produit tiers, chaque longueur est notre choix (nombres premiers, sans
        multiple commun, identiques L/R — la decorrelation stereo vient des
        allpass et du cumul, et est verifiee au signal).

        ### Quatre curseurs publics (AC1)

        Les 4 sous-reseaux (Short 0,4 s / Small 1,1 s / Big 2,2 s / Lush 4,2 s)
        tournent toujours ; chaque curseur (0-100 %) fixe le gain de son moteur
        par la loi cubique kGainCurve (le debut de course reste utile, comme
        le bouton). Le damping un-pole de boucle assure une
        decroissance monotone (aucun emballement possible : le gain de boucle
        de CHAQUE comb est strictement < 1 par construction, et le staging
        plafonne la somme des moteurs).

        ### Pre-delay (AC2)

        Fixe a 20 ms, lisse (glide
        10 ms) pour un changement sans clic, sur le chemin humide seul, PARTAGE
        par les 4 moteurs (une seule ligne).

        ### Transparence (AC3)

        Les 4 curseurs a 0 % : court-circuit complet du module : BIT-EXACT.
        A 100 %, mix humide/secs 30/70 (le portage d'une voix au premier plan),
        le sec traverse sans traitement.        */
    class Reverb
    {
    public:
        static constexpr int kNumEngines = 4;

        // RT60 nominaux (s) des 4 sous-reseaux. Lush porte la nappe la plus
        // longue (4,2 s) : avec des reseaux aux peignes partages, c'est
        // l'ecart de vitesse de decroissance qui distingue les queues au
        //-dela des couleurs tonales (damping par moteur).
        static constexpr float kEngineRt60[kNumEngines] = { 0.4f, 1.1f, 2.2f, 4.2f };

        // Loi des 4 curseurs (0-100 % -> gain du moteur) : cubique douce, le
        // debut de course reste utile.
        static float gainCurve (float pct) noexcept
        {
            const float x = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
            return x * x * x;
        }

        // Pre-delay PREREGLÉ a 20 ms, lisse pour un changement sans clic.
        static constexpr float kFixedPredelayMs  = 20.0f;

        // Mix humide/secs : le sec reste au premier plan, la somme des 4
        // moteurs passe le staging doux. La ligne pre-delay est dimensionnee
        // au plafond de son reglage interne.
        static constexpr float kWetMix    = 0.30f;
        static constexpr float kDryMix    = 0.70f;
        static constexpr float kPredelayGlideMs = 10.0f;
        static constexpr float kPredelayMaxMs   = 200.0f;

        struct Settings
        {
            // Les 4 gains de moteur, 0..1 — la structure retenue : quatre
            // moteurs TOUJOURS cumules, doses moteur par moteur.
            float gains[kNumEngines] = { 0.0f, 0.0f, 0.0f, 0.0f };
            bool  inert = false;   // court-circuit du module ENTIER
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        void setSettings (const Settings&);

        /** Recalcule la cible de pre-delay du bloc. A appeler avant la boucle
            d'echantillons. */
        void beginBlock();

        /** Traite un echantillon de chaque canal sur place, et retourne la
            valeur du canal 0 (convention de la chaine). */
        float processSample (float* const* channels, int sampleIndex);

        /** Niveau de la queue courante, en dBFS (vumetre). */
        float tailLevelDb() const noexcept;

    private:
        static constexpr int kNumCombs   = 8;
        static constexpr int kNumAllpass = 4;

        // Damping de boucle PAR MOTEUR : le caractere tonal de chaque bouton —
        // Short sombre (les HF meurent vite, l'attaque reste dense), Lush
        // clair (la nappe brille et persiste). Cette difference structurelle
        // d'assombrissement distingue aussi les PERSISTANCES au-dela des
        // seuls RT60.
        static constexpr float kEngineLoopDamp[kNumEngines] = { 0.55f, 0.45f, 0.40f, 0.28f };

        // Longueurs en ECHANTILLONS a 48 kHz — nombres premiers, sans multiple
        // commun. PARTAGEES par les 4 sous-reseaux (la difference de couleur
        // vient des gains de boucle = RT60).
        static constexpr int kCombL[kNumCombs] = { 1423, 1571, 1733, 1907, 2089, 2269, 2441, 2617 };
        static constexpr int kCombR[kNumCombs] = { 1439, 1583, 1747, 1933, 2113, 2281, 2467, 2633 };

        static constexpr int kAllpassL[kNumAllpass] = { 347, 397, 443, 487 };
        static constexpr int kAllpassR[kNumAllpass] = { 349, 401, 457, 491 };

        // Chaque comb porte son damping un-pole DANS la boucle : le gain de
        // boucle est < 1 par construction (aucun emballement possible).
        struct Comb
        {
            std::vector<float> buffer;
            int   length = 0;
            int   index  = 0;
            float feedback = 0.0f;
            float damp1    = 1.0f;   // historique du passe-bas un-pole
            float damp2    = 0.0f;
            float store    = 0.0f;
        };

        struct Allpass
        {
            std::vector<float> buffer;
            int   length = 0;
            int   index  = 0;
            float feedback = 0.5f;
        };

        // Un sous-reseau = un moteur complet (combs + allpass par canal).
        struct Engine
        {
            std::array<Comb, kNumCombs>      combsL, combsR;
            std::array<Allpass, kNumAllpass> allpassL, allpassR;
        };

        void prepareEngine (Engine& e, int engineIndex);
        void resetEngine (Engine& e);

        double sampleRateD = 0.0;
        int numChannels = 2;

        Settings settings;
        bool inert = false;
        // Gains de moteur courants (0 = moteur muet, pas de calcul).
        float engineGains[kNumEngines] = { 0.0f, 0.0f, 0.0f, 0.0f };

        std::array<Engine, kNumEngines> engines;

        // Pre-delay : ligne courte partagée, glide sans clic.
        std::vector<float> preL, preR;
        int   preLength = 0;
        int   preIndex  = 0;
        float preSamples = 0.0f;
        float preTarget  = 0.0f;

        float tailLevel = 0.0f;
    };
}
