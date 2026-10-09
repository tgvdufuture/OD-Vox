#include "Image.h"

#include <cmath>

namespace odvox
{
    namespace
    {
        constexpr double kPi = juce::MathConstants<double>::pi;

        /** Vitesse de derive des voix, en Hz. Assez lente pour ne pas faire
            « chorus de synthé », assez rapide pour que deux voix ne se
            recollent jamais : 0,4 et 0,53 Hz, le rapport irrationnel qui evite
            les coïncidences periodiques. */
        constexpr float kDriftRates[Image::kVoicesPerSide * 2] = { 0.40f, 0.53f, 0.47f, 0.36f };

        /** Profondeur de la derive, en cents. Le detune regle est le CENTRE ;
            la derive l'agite de plus ou moins ça autour. Un doubler dont les
            voix sont figees produit un battement constant — audible comme un
            défaut, jamais comme deux chanteurs. */
        constexpr float kDriftDepthCents = 0.6f;
    }

    void Image::prepare (double sampleRate, int channels)
    {
        sampleRateD = sampleRate;
        numChannels = juce::jmax (1, channels);

        const auto hp = juce::IIRCoefficients::makeHighPass (sampleRate, 120.0, 0.7071067811865476);
        sideHigh.set (hp.coefficients[0], hp.coefficients[1], hp.coefficients[2],
                      hp.coefficients[3], hp.coefficients[4]);
        sideHighCross.set (hp.coefficients[0], hp.coefficients[1], hp.coefficients[2],
                           hp.coefficients[3], hp.coefficients[4]);

        builtVoicesValid = false;   // les voix seront construites au premier setSettings

        reset();
    }

    void Image::reset()
    {
        for (auto& v : voices)
        {
            v.phase = 0.0f;
            v.line.fill (0.0f);
            v.write = 0;
        }

        sideHigh.clearState();
        sideHighCross.clearState();
        widthSmoothed = widthTarget;
    }

    void Image::refreshVoices()
    {
        builtDetune = settings.detuneCents;
        builtDelayMs = settings.delayMs;

        const double sr = sampleRateD > 0.0 ? sampleRateD : 48000.0;

        // Les quatre voix : 0 et 1 a gauche, 2 et 3 a droite. Les delais sont
        // DECALEES entre voix d'un meme cote (sinon elles fusionnent) et
        // SYMETRIQUES entre cotes (sinon l'image penche).
        const float base = (float) (settings.delayMs * 0.001 * sr);

        for (int i = 0; i < kVoicesPerSide * 2; ++i)
        {
            auto& v = voices[(size_t) i];

            const int side = i / kVoicesPerSide;             // 0 : L, 1 : R
            const int slot = i % kVoicesPerSide;             // 0 : premiere voix, 1 : seconde

            // Decalage intra-cote : la moitie du delai de base, ce qui garde les
            // deux voix dans la plage 5-60 ms du catalogue.
            v.delaySamples = base * (1.0f + 0.5f * (float) slot);

            // Desaccordage oppose entre cotes : la voix a de gauche et celle de
            // droite ne partagent jamais leur frequence instantanee.
            const float sign = (side == 0 ? 1.0f : -1.0f) * (slot == 0 ? 1.0f : -0.8f);
            v.detuneCents = sign * settings.detuneCents;
            v.rateHz = kDriftRates[i];
            v.phase = 0.25f * (float) i;   // phases reparties : pas de coincidences
        }

        // Normalisation d'energie : quatre voix non correlees a poids egal
        // portent 4x l'energie, soit +6 dB. Le curseur pilote leur niveau, et la
        // somme d'energie (voix + sec) reste celle de l'entree a 100 % : c'est la
        // « sans effet de niveau » voulu.
        const float amount = juce::jlimit (0.0f, 1.0f, settings.amount01);
        gainPerVoice = amount * std::sqrt (0.25f);
    }

    void Image::setSettings (const Settings& s)
    {
        settings = s;

        if (settings.detuneCents != builtDetune || settings.delayMs != builtDelayMs
            || ! builtVoicesValid)
        {
            refreshVoices();
            builtVoicesValid = true;
        }

        // Le pas de lissage de la largeur : 20 ms pour parcourir toute la plage,
        // sans zipper audible ni retard perceptible.
        widthTarget = juce::jlimit (0.0f, 2.0f, settings.width);

        if (sampleRateD > 0.0)
        {
            const float span = 2.0f;   // plage complete du parametre
            const float timeSeconds = 0.02f;
            widthStep = span / (float) (timeSeconds * sampleRateD);
        }

        // La coupure du mono bass est-elle dans la plage utile ? En dessous de
        // 20 Hz elle ne fait rien de mesurable (AC2 ne porte que sur ce qui
        // s'entend) ; au-dessus de Nyquist non plus.
        monoBassActive = settings.monoBassHz >= 20.0f
                         && settings.monoBassHz <= sampleRateD * 0.45;

        if (monoBassActive && sampleRateD > 0.0)
        {
            const auto hp = juce::IIRCoefficients::makeHighPass (
                sampleRateD, (double) settings.monoBassHz, 0.7071067811865476);
            sideHigh.set (hp.coefficients[0], hp.coefficients[1], hp.coefficients[2],
                          hp.coefficients[3], hp.coefficients[4]);
            sideHighCross.set (hp.coefficients[0], hp.coefficients[1], hp.coefficients[2],
                               hp.coefficients[3], hp.coefficients[4]);
        }
    }

    float Image::processSample (float* const* channels, int sampleIndex)
    {
        // --- Court-circuit du module ENTIER (AC1 de US-08, AC4) ---------------
        // A width 100 % ET doubler 0 %, il n'y a litteralement rien a faire :
        // la sortie est identique au bit pres, tres en dessous des −120 dBFS.
        if (settings.inert)
            return channels[0][sampleIndex];

        const int n = numChannels;

        // --- Doubler : deux voix par cote, delai + detune derives -------------
        const float amount = juce::jlimit (0.0f, 1.0f, settings.amount01);

        if (amount > 0.0f && n >= 2)
        {
            const float dryL = channels[0][sampleIndex];
            const float dryR = channels[1][sampleIndex];

            float wetL = 0.0f;
            float wetR = 0.0f;

            for (int i = 0; i < kVoicesPerSide * 2; ++i)
            {
                auto& v = voices[(size_t) i];
                const int side = i / kVoicesPerSide;

                // La derive : le detune regle, agite par le LFO de la voix.
                // La phase avance d'UN pas par echantillon traité, quel que soit
                // le cote — les voix restent independantes sans condition.
                v.phase += (float) (2.0 * kPi * (double) v.rateHz / sampleRateD);

                if (v.phase > (float) (2.0 * kPi))
                    v.phase -= (float) (2.0 * kPi);

                const float drift = kDriftDepthCents * std::sin (v.phase);
                const float cents = v.detuneCents + drift;

                // Le delai varie avec le detune : c'est PHYSIQUE — une voix qui
                // chante plus haut derive lentement en phase, donc son delai
                // effectif glisse. D'ou la modulation du delai lui-meme.
                const float ratio = std::pow (2.0f, cents / 1200.0f);
                const float readFloat = (float) v.write - v.delaySamples * ratio;

                float readIndex = readFloat;
                while (readIndex < 0.0f)
                    readIndex += (float) v.line.size();

                const int i0 = (int) readIndex;
                const int i1 = (i0 + 1) % (int) v.line.size();
                const float frac = readIndex - (float) i0;

                const float delayed = v.line[(size_t) i0]
                                      + frac * (v.line[(size_t) i1] - v.line[(size_t) i0]);

                // L'entree de la ligne : le signal SEC du cote, pas le wet —
                // sinon les voix se recursivent.
                v.line[(size_t) v.write] = (side == 0 ? dryL : dryR);
                v.write = (v.write + 1) % (int) v.line.size();

                if (side == 0)
                    wetL += delayed;
                else
                    wetR += delayed;
            }

            wetL *= gainPerVoice;
            wetR *= gainPerVoice;

            // « Sans effet de niveau » : le sec reste entier, les voix s'ajoutent
            // a poids normalise. A 100 %, la somme d'energie sec + 4 voix
            // normalisees reste celle de l'entree a un chouia pres.
            channels[0][sampleIndex] = dryL + wetL;
            channels[1][sampleIndex] = dryR + wetR;
        }

        // --- Width : mid/side + mono bass -------------------------------------
        if (n >= 2)
        {
            // Lissage par pas constant : le pas est calcule une fois par bloc
            // (setSettings), ici on avance d'un pas par echantillon.
            if (widthSmoothed < widthTarget)
                widthSmoothed = juce::jmin (widthTarget, widthSmoothed + widthStep);
            else if (widthSmoothed > widthTarget)
                widthSmoothed = juce::jmax (widthTarget, widthSmoothed - widthStep);

            const float w = widthSmoothed;

            if (std::abs (w - 1.0f) > 1.0e-6f)
            {
                const float l = channels[0][sampleIndex];
                const float r = channels[1][sampleIndex];

                const float m = 0.5f * (l + r);
                float s = 0.5f * (l - r);

                if (monoBassActive)
                {
                    // Un Linkwitz-Riley 4 du side : le bas est coupe (mono
                    // strict sous la coupure, AC2), le haut porte la largeur.
                    float high = sideHigh.process (s, 0);
                    high = sideHighCross.process (high, 0);

                    s = w * high;
                }
                else
                {
                    s *= w;
                }

                channels[0][sampleIndex] = m + s;
                channels[1][sampleIndex] = m - s;
            }
        }

        return channels[0][sampleIndex];
    }
}
