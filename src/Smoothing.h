#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace odvox
{
    /** Porte la valeur lissee de chaque parametre continu.

        PRD.md §3.4 : `modulation` transporte les valeurs lissee (une par
        parametre, periode de lissage >= 10 ms) pour eviter les clics. Le defaut
        de 20 ms satisfait cette borne avec de la marge.

        C'est ce lissage qui satisfait AC2 de US-11 : un basculement A/B change
        les cibles, jamais les valeurs courantes, donc aucun saut ne peut
        apparaitre dans le signal. */
    class Smoothing
    {
    public:
        static constexpr double kDefaultRampSeconds = 0.02;

        void prepare (double sampleRate, int numParameters,
                      double rampSeconds = kDefaultRampSeconds);

        /** Fixe les cibles pour le prochain bloc, sans changer les valeurs
            courantes : elles seront atteintes progressivement. */
        void setTargets (const float* targets, int numTargets);

        /** Amorce sur les cibles sans rampe. A utiliser a la preparation, jamais
            sur un changement d'etat pendant que l'audio tourne. */
        void snapToTargets (const float* targets, int numTargets);

        /** Avance le lissage de `numSamples` sans rien lire. Sert aux parametres
            consommes une fois par bloc — les coefficients de filtre — alors que
            les gains sont eux lus echantillon par echantillon. */
        void advanceBy (int numSamples);

        float getNextValue (int index) noexcept;
        float getCurrent (int index) const noexcept;

        bool isSmoothing() const noexcept;
        int size() const noexcept { return (int) values.size(); }

    private:
        std::vector<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>> values;
        double sampleRate = 0.0;
        double rampSeconds = kDefaultRampSeconds;
    };
}
