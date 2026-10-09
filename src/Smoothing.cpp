#include "Smoothing.h"

#include <algorithm>

namespace odvox
{
    void Smoothing::prepare (double newSampleRate, int numParameters, double newRampSeconds)
    {
        sampleRate  = newSampleRate;
        rampSeconds = newRampSeconds;

        if (numParameters < 0)
            numParameters = 0;

        values.assign ((size_t) numParameters, {});

        for (auto& v : values)
            v.reset (sampleRate, rampSeconds);
    }

    void Smoothing::setTargets (const float* targets, int numTargets)
    {
        const int n = juce::jmin (numTargets, size());

        for (int i = 0; i < n; ++i)
            values[(size_t) i].setTargetValue (targets[i]);
    }

    void Smoothing::snapToTargets (const float* targets, int numTargets)
    {
        const int n = juce::jmin (numTargets, size());

        for (int i = 0; i < n; ++i)
            values[(size_t) i].setCurrentAndTargetValue (targets[i]);
    }

    void Smoothing::advanceBy (int numSamples)
    {
        if (numSamples <= 0)
            return;

        for (auto& v : values)
            v.skip (numSamples);
    }

    float Smoothing::getNextValue (int index) noexcept
    {
        if (index < 0 || index >= size())
            return 0.0f;

        return values[(size_t) index].getNextValue();
    }

    float Smoothing::getCurrent (int index) const noexcept
    {
        if (index < 0 || index >= size())
            return 0.0f;

        return values[(size_t) index].getCurrentValue();
    }

    bool Smoothing::isSmoothing() const noexcept
    {
        return std::any_of (values.begin(), values.end(),
                            [] (const auto& v) { return v.isSmoothing(); });
    }
}
