#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace odvox
{
    /** Compresseur (F1.7, US-05) — un curseur, cible de conception (F1.7b).

        Le compresseur a ete **mesure au signal** a la conception
        et son modele est etabli :

        - seuil FIXE ≈ -50 dBFS (jamais expose, jamais bouge) ;
        - ratio croissant avec le curseur : 1,50 / 1,63 / 1,79 / 2,05 / 4,37 ;
        - make-up croissant : +1,7 / +7,3 / +13,1 / +18,2 / +29,2 dB — c'est un
          **nivellement automatique** qui remonte fortement les signaux faibles ;
        - il n'est **jamais desactive** : a 0 % il compresse deja a 1,5:1.

        Consequences assumees et ecrites au PRD :
        - REV du 2026-09-21 : `comp_on` est supprime — le curseur ENGAGE le
          module (0 % = court-circuit par le processeur), comme chez la
          la cible, dont le compresseur est interne et toujours actif ;
        - le bruit de fond remonte avec le make-up : le gate en amont n'est pas
          un luxe, c'est l'architecture de la table elle-meme.

        La regle du macro (US-05) tient : UN SEUL curseur pilote la table
        (rev du 2026-09-21 : le module n'expose AUCUN reglage de comp —
        les parametres avances et le mode Custom sont supprimes ; le make-up
        suiveur de la reduction moyenne, notre amelioration, reste actif). */
    class Compressor
    {
    public:
        static constexpr float kAttackMs = 3.0f;    // v1 : fixe (Phase 2)
        // Le seuil FIXE de la cible : jamais expose, jamais bouge. La
        // table mesuree ne fait varier que le ratio et le make-up.
        static constexpr float kFixedThresholdDb  = -50.0f;
        static constexpr float kDefaultReleaseMs  = 150.0f;
        static constexpr float kDetectorSpanDb    = 50.0f;   // plage rampe release
        static constexpr float kMakeUpMaxDb       = 30.0f;  // la table monte a +29,2 dB

        /** La table macro retenue : un reglage pour une position
            du curseur. C'est cette table que AC4 de US-05 veut reproduite. */
        struct Preset
        {
            float thresholdDb;
            float ratio;
            float releaseMs;
            float makeUpDb;
        };

        /** Reglages internes pour une position 0..1 du curseur. */
        static Preset tableFor (float amount01);

        struct Settings
        {
            // UN SEUL reglage public : le curseur. La table fait le reste.
            float amountPct = 70.0f;
            // Defaut du struct : actif (reglage direct). Le PROCESSEUR l'assigne
            // toujours explicitement depuis le curseur (0 % = inerte).
            bool  enabled   = true;
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        /** Appele a CHAQUE BLOC par le processeur : ne doit jamais reinitialiser
            l'etat (detecteur) — seulement `reset()` le fait. */
        void setSettings (const Settings&);

        /** Traite un echantillon, sur place. Renvoie la reduction appliquee, en
            dB (valeur positive). */
        float processSample (float* const* channels, int sampleIndex);

        /** Reduction instantanee courante, en dB (valeur positive) — vumetre. */
        float currentReductionDb() const noexcept { return -gainDb; }

        /** Reduction maximale depuis le dernier `reset()`, en dB. */
        float maxReductionDb() const noexcept { return worstReductionDb; }

    private:
        static float detectorCoefficient (double sampleRate, float milliseconds) noexcept;

        double sampleRate = 48000.0;
        int numChannels = 2;

        Settings settings;
        bool inert = false;   // pose par le processeur (curseur a 0 %) : le
                              // comp engage n'est jamais desactive par la table

        float effectiveThresholdDb = -50.0f;
        float effectiveRatio       = 1.5f;

        float detectorAttackCoeff  = 1.0f;
        float detectorReleaseCoeff = 1.0f;
        float attackStepDb         = 0.1f;   // dB par echantillon
        float releaseStepDb        = 0.1f;   // dB par echantillon
        float effectiveMakeUpDb    = 0.0f;   // valeur de table en mode macro
        float makeUpGainDb         = 0.0f;   // rampe vers la valeur de table
                                               // (amorce ~3 ms : transitoire
                                               // d'attaque elimine, mesure du
                                               // 2026-09-21)

        float detector = 0.0f;
        float gainDb = 0.0f;
        float worstReductionDb = 0.0f;
    };
}
