#include "Gate.h"

#include <cmath>

namespace odvox
{
    void Gate::prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        numChannels = juce::jmax (1, newNumChannels);

        reset();
    }

    void Gate::reset()
    {
        detector         = 0.0f;
        gainDb           = 0.0f;
        holdSamples      = 0;
        open             = true;
        worstReductionDb = 0.0f;
    }

    float Gate::detectorCoefficient (float milliseconds) const noexcept
    {
        const double seconds = juce::jmax (0.01, (double) milliseconds) * 0.001;
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    void Gate::setSettings (const Settings& s)
    {
        settings = s;

        // Le curseur d'intensite regle la profondeur du gate : a 0 % il n'y a
        // aucune reduction possible, donc le module est transparent.
        effectiveRangeDb = juce::jlimit (0.0f, 100.0f, s.amountPct) * 0.01f
                             * juce::jmax (0.0f, s.rangeDb);

        inert = effectiveRangeDb <= 0.0f;

        if (inert)
        {
            reset();
            return;
        }

        const float sr = (float) sampleRate;

        // Pas de rampe lineaire en dB, et non exponentielle : c'est ce qui garantit
        // que la reduction atteint EXACTEMENT la valeur reglee au bout du temps de
        // release (AC3 de US-03 demande ±1 dB). Une rampe exponentielle n'y
        // arriverait asymptotiquement jamais.
        attackStepDb  = effectiveRangeDb
                          / juce::jmax (1.0f, kAttackMs * 0.001f * sr);
        releaseStepDb = effectiveRangeDb
                          / juce::jmax (1.0f, juce::jmax (1.0f, s.releaseMs) * 0.001f * sr);

        detectorAttackCoeff  = detectorCoefficient (kDetectorAttackMs);
        detectorReleaseCoeff = detectorCoefficient (kDetectorReleaseMs);

        holdSamplesMax = (int) juce::jmax (1.0f, kHoldMs * 0.001f * sr);
    }

    float Gate::processSample (float* const* channels, int sampleIndex)
    {
        if (inert)
            return 0.0f;

        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (channels[ch][sampleIndex]));

        const bool rising = peak > detector;
        detector += (peak - detector) * (rising ? detectorAttackCoeff : detectorReleaseCoeff);

        const float envelopeDb = juce::Decibels::gainToDecibels (detector, -100.0f);

        if (open)
        {
            // Hysterese : on ne referme qu'en dessous du seuil abaisse, pour ne pas
            // hachurer un signal qui flotte autour du seuil.
            if (envelopeDb < settings.thresholdDb - kHysteresisDb)
            {
                if (holdSamples > 0)
                    --holdSamples;
                else
                    open = false;
            }
            else
            {
                holdSamples = holdSamplesMax;
            }
        }
        else if (envelopeDb > settings.thresholdDb)
        {
            open        = true;
            holdSamples = holdSamplesMax;
        }

        const float targetDb = open ? 0.0f : -effectiveRangeDb;

        if (targetDb > gainDb)
            gainDb = juce::jmin (targetDb, gainDb + attackStepDb);
        else
            gainDb = juce::jmax (targetDb, gainDb - releaseStepDb);

        const float gain = juce::Decibels::decibelsToGain (gainDb);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][sampleIndex] *= gain;

        const float reduction = -gainDb;
        worstReductionDb = juce::jmax (worstReductionDb, reduction);

        return reduction;
    }
}
