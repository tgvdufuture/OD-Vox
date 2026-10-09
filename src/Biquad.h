#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace odvox
{
    /** Biquad transposed-direct-form-I partage par les modules filtrants (extrait
        de LowCut au F1.8, pour le de-esser split-band).

        L'etat est double par canal : un filtre prepare pour `kMaxChannels`
        canaux doit toujours recevoir le nombre de canaux annonce — lire un
        pointeur de canal hors du tableau du signal est un comportement indefini
        (le segfault du F1.7 venait exactement de la). */
    struct Biquad
    {
        static constexpr int kMaxChannels = 2;

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<float, (size_t) kMaxChannels> x1 {}, x2 {}, y1 {}, y2 {};

        void set (float nb0, float nb1, float nb2, float na1, float na2) noexcept
        {
            b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
        }

        void identity() noexcept { set (1.0f, 0.0f, 0.0f, 0.0f, 0.0f); }

        void clearState() noexcept
        {
            x1.fill (0.0f); x2.fill (0.0f); y1.fill (0.0f); y2.fill (0.0f);
        }

        float process (float x, int channel) noexcept
        {
            const auto c = (size_t) channel;
            const float y = b0 * x + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c];

            x2[c] = x1[c]; x1[c] = x;
            y2[c] = y1[c]; y1[c] = y;

            return y;
        }
    };
}
