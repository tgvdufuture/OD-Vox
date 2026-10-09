#include "PitchDetector.h"

#include <cmath>
#include <vector>

namespace odvox
{
    void PitchDetector::prepare (double sampleRate)
    {
        sampleRateD = sampleRate > 0.0 ? sampleRate : 48000.0;

        decimation = juce::jmax (1, (int) std::lround (sampleRateD / (double) kTargetRate));
        analysisRate = sampleRateD / (double) decimation;

        // Anti-repliement a 45 % du taux d'analyse.
        const double cutoff = juce::jmin (analysisRate * 0.45, sampleRateD * 0.45);
        const auto coeffs = juce::IIRCoefficients::makeLowPass (sampleRateD, cutoff);
        antiAlias[0].setCoefficients (coeffs);
        antiAlias[1].setCoefficients (coeffs);

        // Une analyse toutes les ~10 ms de signal, au plus.
        hop = juce::jmax (32, (int) (analysisRate / 100.0));

        reset();
    }

    void PitchDetector::reset()
    {
        antiAlias[0].reset();
        antiAlias[1].reset();
        ring.fill (0.0f);
        writeIndex = 0;
        filled = 0;
        sinceAnalysis = 0;
        detectedHz = 0.0f;
        detectedConfidence = 0.0f;
    }

    void PitchDetector::pushBlock (const float* const* channels, int numChannels, int numSamples)
    {
        if (sampleRateD <= 0.0 || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Somme des canaux : le detecteur suit la voix, pas un cote.
            float x = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                x += channels[ch][i];

            if (numChannels > 1)
                x /= (float) numChannels;

            x = antiAlias[0].processSingleSampleRaw (x);
            x = antiAlias[1].processSingleSampleRaw (x);

            if (++decimateCounter >= decimation)
            {
                decimateCounter = 0;

                ring[(size_t) writeIndex] = x;
                writeIndex = (writeIndex + 1) % kRingSize;
                filled = juce::jmin (filled + 1, kRingSize);

                if (++sinceAnalysis >= hop)
                {
                    sinceAnalysis = 0;

                    if (filled >= kWindow + (int) (analysisRate / (double) kMinHz))
                        analyse();
                }
            }
        }
    }

    void PitchDetector::analyse()
    {
        const int minLag = juce::jmax (2, (int) (analysisRate / (double) kMaxHz));
        const int maxLag = juce::jmin (kRingSize - kWindow - 1,
                                       (int) (analysisRate / (double) kMinHz));

        if (maxLag <= minLag)
            return;

        ++analyses;

        // Difference moyenne : d(tau) = somme (x[i] - x[i+tau])^2 sur kWindow.
        // Lecture circulaire depuis le point d'ecriture le plus ancien.
        const int start = (writeIndex + kRingSize - kWindow - maxLag) % kRingSize;

        std::vector<float> diff ((size_t) maxLag + 1, 0.0f);

        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < kWindow; ++i)
            {
                const float a = ring[(size_t) ((start + i) % kRingSize)];
                const float b = ring[(size_t) ((start + i + tau) % kRingSize)];
                const float d = a - b;
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        // Difference normalisee cumulee (YIN) : le biais des petits retards
        // disparait, sinon le minimum est toujours au retard le plus court.
        std::vector<float> cumulative ((size_t) maxLag + 1, 0.0f);
        cumulative[(size_t) minLag] = 1.0f;

        float running = 0.0f;
        for (int tau = minLag + 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            cumulative[(size_t) tau] = running > 0.0f
                ? diff[(size_t) tau] * (float) (tau - minLag) / running
                : 1.0f;
        }

        // Premier minimum local sous le seuil, dans la moitie gauche de la plage
        // (le minimum global d'une fonction quasi periodique se repete a chaque
        // multiple de la periode : le premier est la fondamentale, les suivants
        // sont ses multiples — d'ou l'ordre de recherche).
        int bestTau = -1;
        for (int tau = minLag + 1; tau < maxLag; ++tau)
        {
            if (cumulative[(size_t) tau] < kYinThreshold
                && cumulative[(size_t) tau] <= cumulative[(size_t) tau - 1]
                && cumulative[(size_t) tau] <= cumulative[(size_t) tau + 1])
            {
                bestTau = tau;
                break;
            }
        }

        if (bestTau < 0)
        {
            // Aucun minimum franc : on garde la derniere detection mais la
            // confiance tombe a zero — c'est ce que lit la descente en frequences
            // fixes du cas limite.
            detectedConfidence = 0.0f;
            detectedHz = 0.0f;
            return;
        }

        // Interpolation parabolique du minimum : sans elle, la quantification du
        // retard plafonne la precision a ~1 % (soit ~17 cents a 100 Hz).
        float refined = (float) bestTau;
        if (bestTau > minLag && bestTau < maxLag)
        {
            const float y0 = cumulative[(size_t) bestTau - 1];
            const float y1 = cumulative[(size_t) bestTau];
            const float y2 = cumulative[(size_t) bestTau + 1];
            const float denom = 2.0f * (2.0f * y1 - y0 - y2);

            if (std::abs (denom) > 1.0e-9f)
                refined = (float) bestTau + (y2 - y0) / denom;
        }

        detectedHz = (float) (analysisRate / (double) refined);
        detectedConfidence = juce::jlimit (0.0f, 1.0f, 1.0f - cumulative[(size_t) bestTau]);

        if (detectedHz < kMinHz || detectedHz > kMaxHz)
        {
            detectedHz = 0.0f;
            detectedConfidence = 0.0f;
        }
    }
}
