#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace odvox
{
    /** Gate / expander (F1.4, US-03).

        L'attaque et le maintien ne sont **pas** exposes : PRD.md §3.4 ne leur
        alloue aucun emplacement, comme pour `attack` et `knee` du compresseur.
        Ce sont donc des constantes de conception, choisies pour tenir AC2 de
        US-03 — la premiere consonne d'une plosive ne doit pas etre tronquee —.

        Le maintien sert au cas limite de F1.4 : une plosive breve (occlusion)
        laisse retomber l'enveloppe sous le seuil pendant quelques dizaines de ms,
        et sans maintien le gate se refermerait au milieu du mot. */
    class Gate
    {
    public:
        static constexpr float kAttackMs           = 0.5f;
        static constexpr float kHoldMs             = 50.0f;
        static constexpr float kHysteresisDb       = 3.0f;   // fermeture <= ouverture (contrainte de F1.4)
        static constexpr float kDetectorAttackMs   = 0.5f;
        static constexpr float kDetectorReleaseMs  = 20.0f;

        struct Settings
        {
            float thresholdDb = -45.0f;
            float releaseMs   = 150.0f;
            float rangeDb     = 80.0f;
            float amountPct   = 0.0f;   // Mode Essentiel : 0 % = strictement transparent
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        /** A appeler une fois par bloc : c'est ici que sont calcules les pas de
            rampe, une seule fois pour tout le bloc. */
        void setSettings (const Settings&);

        /** Traite un echantillon, sur place. Renvoie la reduction appliquee, en dB
            (valeur positive). */
        float processSample (float* const* channels, int sampleIndex);

        /** Reduction maximale atteinte depuis le dernier `reset()`, en dB. */
        float maxReductionDb() const noexcept { return worstReductionDb; }

    private:
        float detectorCoefficient (float milliseconds) const noexcept;

        double sampleRate = 48000.0;
        int numChannels = 2;

        Settings settings;
        bool inert = true;
        float effectiveRangeDb = 0.0f;

        float attackStepDb = 1.0f;
        float releaseStepDb = 1.0f;
        float detectorAttackCoeff = 1.0f;
        float detectorReleaseCoeff = 1.0f;
        int holdSamplesMax = 0;

        float detector = 0.0f;
        float gainDb = 0.0f;
        int holdSamples = 0;
        bool open = true;
        float worstReductionDb = 0.0f;
    };
}
