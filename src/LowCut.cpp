#include "LowCut.h"

#include <cmath>

namespace odvox
{
    namespace
    {
        /** Q de Butterworth d'ordre 4. Ces deux valeurs sont celles du gabarit :
            c'est ce qui donne −3 dB exactement au coin ET une reponse
            maximalement plate. Choisir des Q arbitraires ferait apparaitre une
            bosse au coin, que la mesure verrait immediatement. */
        constexpr float kButterworthQ4[] = { 0.54119610f, 1.30656296f };
    }

    void LowCut::prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        numChannels = juce::jlimit (1, kMaxChannels, newNumChannels);

        reset();
    }

    void LowCut::reset()
    {
        for (auto& s : stages)
            s.clearState();
    }

    void LowCut::updateHighPass (Biquad& f, float freqHz, float q) noexcept
    {
        const float w0    = juce::MathConstants<float>::twoPi * freqHz / (float) sampleRate;
        const float cosW  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);

        const float a0 = 1.0f + alpha;

        f.set ( (1.0f + cosW) * 0.5f / a0,
               -(1.0f + cosW)        / a0,
                (1.0f + cosW) * 0.5f / a0,
               -2.0f * cosW           / a0,
                (1.0f - alpha)        / a0);
    }

    void LowCut::setSettings (const Settings& s)
    {
        inert = ! s.enabled;

        // AC de transparence : interrupteur ferme = identite BIT A BIT, pas un
        // melange a coefficient nul. On remet aussi les etats a zero plutot que
        // de laisser des restes : un coupe-bas qu'on rallume doit repartir propre.
        if (inert)
        {
            stagesUsed = 0;

            for (auto& stage : stages)
                stage.identity();

            reset();
            return;
        }

        const float nyquistLimit = (float) (sampleRate * 0.45);
        activeHz   = juce::jlimit (10.0f, nyquistLimit, kFixedHz);
        stagesUsed = kNumStages;

        for (int i = 0; i < stagesUsed; ++i)
            updateHighPass (stages[(size_t) i], activeHz, kButterworthQ4[i]);
    }

    void LowCut::processSample (float* const* channels, int sampleIndex)
    {
        if (inert)
            return;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float wet = channels[ch][sampleIndex];

            for (int i = 0; i < stagesUsed; ++i)
                wet = stages[(size_t) i].process (wet, ch);

            channels[ch][sampleIndex] = wet;
        }
    }
}
