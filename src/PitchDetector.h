#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace odvox
{
    /** Detecteur de fondamentale (F1.6b).

        **Composant PARTAGE, jamais duplique** : le module EQ s'en sert, et rien
        d'autre ne doit l'instancier — un seul detecteur, pas deux, ce qui rend
        l'augmentation de charge CPU mesurable.

        Methode : difference moyenne normalisee cumulee (l'algorithme YIN), avec
        interpolation parabolique du minimum. Elle est robuste aux harmoniques —
        ce qui compte ici : sur un signal dont la fondamentale est faible mais dont
        les harmoniques sont fortes (une voix, ou un spectre de voyelle), une
        detection par maximum de spectre se trompe d'une octave, pas celle-ci.

        Un signal dont les partiels sont des harmoniques d'une meme fondamentale
        est periodique a cette fondamentale : c'est ce qui permet de poser une
        sonde a 300 Hz sur une fondamentale a 150 Hz sans que le detecteur ne
        bascule sur 300 Hz (son minimum est au PREMIER tau, donc a 150 Hz).

        **Sous-echantillonnage par 4** (12 kHz) : la plage utile s'arrete a 500 Hz,
        et le cout de l'autocorrelation varie comme le CARRE du nombre de retards.
        Un filtre anti-repliement precede la decimation, sans quoi les harmoniques
        aigues se replieraient dans la plage de recherche. */
    class PitchDetector
    {
    public:
        static constexpr float kMinHz = 70.0f;
        static constexpr float kMaxHz = 500.0f;
        static constexpr float kTargetRate = 12000.0f;   // taux d'analyse
        static constexpr int   kRingSize = 1024;         // historique d'analyse
        static constexpr int   kWindow = 256;            // fenetre de comparaison
        static constexpr float kYinThreshold = 0.20f;    // seuil d'acceptation

        void prepare (double sampleRate);
        void reset();

        /** Pousse un bloc de signal. L'analyse est lancee quand assez de signal
            decime est arrive — jamais a chaque echantillon. */
        void pushBlock (const float* const* channels, int numChannels, int numSamples);

        /** Frequence detectee en Hz, 0 si aucune. */
        float frequencyHz() const noexcept { return detectedHz; }

        /** Confiance 0..1 (1 = signal parfaitement periodique a cette periode). */
        float confidence() const noexcept { return detectedConfidence; }

        /** Nombre d'analyses lancees depuis `reset()` — instrumentation de cout. */
        juce::int64 analysisCount() const noexcept { return analyses; }

    private:
        void analyse();

        double sampleRateD = 0.0;
        int decimation = 1;
        double analysisRate = 0.0;

        // Anti-repliement : deux Butterworth 2 en cascade (45 % du taux d'analyse).
        juce::IIRFilter antiAlias[2];
        int decimateCounter = 0;

        std::array<float, (size_t) kRingSize> ring {};
        int writeIndex = 0;
        int filled = 0;
        int sinceAnalysis = 0;
        int hop = 128;

        float detectedHz = 0.0f;
        float detectedConfidence = 0.0f;
        juce::int64 analyses = 0;
    };
}
