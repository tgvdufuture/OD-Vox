#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace odvox
{
    /** Calibration automatique du gain d'entree (F1.3, US-06).

        Mesure le niveau crete du signal **apres** le gain d'entree, et rend le
        changement de gain qui place ce niveau au milieu de la fourchette
        -12..-6 dBFS exigee par AC1 de US-06.

        Elle ne touche jamais au gain elle-meme : elle rend un resultat que
        l'appelant applique. C'est ce qui rend AC2 de US-06 verifiable — une
        annulation pendant la mesure laisse le gain **inchange**. */
    class Calibrator
    {
    public:
        enum class State { idle, waiting, measuring, done, abandoned };

        static constexpr float  kTargetPeakDb   = -9.0f;    // milieu de -12..-6
        static constexpr double kMeasureSeconds = 5.0;      // « 5 secondes d'entree vocale »
        static constexpr double kGiveUpSeconds  = 10.0;     // cas limite de F1.3
        static constexpr float  kSignalFloorDb  = -60.0f;   // ce qui compte comme du son

        void prepare (double sampleRate);
        void start();
        void cancel();

        State getState() const noexcept { return state; }

        bool isRunning() const noexcept
        {
            return state == State::waiting || state == State::measuring;
        }

        /** Mesure un bloc. A appeler sur le signal post-gain d'entree. */
        void process (const float* const* channels, int numChannels, int numSamples);

        /** Vrai une seule fois, quand la mesure vient d'aboutir. `gainDeltaDb` est
            le changement a appliquer au gain d'entree. */
        bool consumeResult (float& gainDeltaDb);

        float measuredPeakDb() const noexcept
        {
            return juce::Decibels::gainToDecibels (measuredPeak, -100.0f);
        }

    private:
        void finish();
        void abandon();

        double sampleRate = 48000.0;
        State state = State::idle;

        float measuredPeak = 0.0f;
        double measuredSeconds = 0.0;
        double waitedSeconds = 0.0;

        bool resultPending = false;
        float pendingGainDeltaDb = 0.0f;
    };
}
