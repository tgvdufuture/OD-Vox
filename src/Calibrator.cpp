#include "Calibrator.h"

namespace odvox
{
    void Calibrator::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        cancel();
    }

    void Calibrator::start()
    {
        state             = State::waiting;
        measuredPeak      = 0.0f;
        measuredSeconds   = 0.0;
        waitedSeconds     = 0.0;
        resultPending     = false;
        pendingGainDeltaDb = 0.0f;
    }

    void Calibrator::cancel()
    {
        // Une annulation n'efface un resultat deja obtenu : AC2 de US-06 porte sur
        // l'annulation EN COURS de mesure.
        if (isRunning())
            state = State::idle;
    }

    void Calibrator::finish()
    {
        const float peakDb = juce::Decibels::gainToDecibels (measuredPeak, -100.0f);

        // Peut etre negatif : le cas limite de F1.3 demande d'attenuer quand le
        // signal est deja tres fort.
        pendingGainDeltaDb = kTargetPeakDb - peakDb;
        resultPending      = true;
        state              = State::done;
    }

    void Calibrator::abandon()
    {
        state              = State::abandoned;
        pendingGainDeltaDb = 0.0f;
    }

    void Calibrator::process (const float* const* channels, int numChannels, int numSamples)
    {
        if (! isRunning() || numSamples <= 0)
            return;

        const double blockSeconds = (double) numSamples / sampleRate;

        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (channels[ch][i]));

        const bool hasSignal = juce::Decibels::gainToDecibels (peak, -100.0f) > kSignalFloorDb;

        if (state == State::waiting)
        {
            if (hasSignal)
            {
                state           = State::measuring;
                measuredPeak    = peak;
                measuredSeconds = blockSeconds;
                return;
            }

            // Cas limite de F1.3 : aucun son pendant 10 s, on abandonne et le gain
            // reste inchange.
            waitedSeconds += blockSeconds;

            if (waitedSeconds >= kGiveUpSeconds)
                abandon();

            return;
        }

        measuredPeak = juce::jmax (measuredPeak, peak);
        measuredSeconds += blockSeconds;

        if (measuredSeconds >= kMeasureSeconds)
            finish();
    }

    bool Calibrator::consumeResult (float& gainDeltaDb)
    {
        if (! resultPending)
            return false;

        resultPending = false;
        gainDeltaDb   = pendingGainDeltaDb;
        return true;
    }
}
