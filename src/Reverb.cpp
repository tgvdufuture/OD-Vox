#include "Reverb.h"

#include <algorithm>
#include <cmath>

namespace odvox
{
    namespace
    {
        // Somme des combs remise a l'echelle, staging humide doux, et damping
        // fixe de boucle (etat du passe-bas un-pole) : le gain de boucle de
        // CHAQUE comb vaut feedback * gain(LP) < 1 pour tout reglage, donc le
        // cumul de 4 moteurs ne peut pas diverger non plus (chaque sous-reseau
        // est stable independamment, le staging plafonne la somme).
        constexpr float kCombScale = 1.0f / 8.0f;
        constexpr float kWetStage  = 0.75f;
        constexpr float kTanhOut   = 0.80f;
        // Damping de SECOURS si un moteur n'avait pas sa valeur (jamais le cas
        // en pratique : prepareEngine fixe toujours kEngineLoopDamp).
        constexpr float kLoopDamp  = 0.40f;

        float tanhApprox (float x) noexcept
        {
            // tanh polynomial (6.4) : suffisamment precis pour un staging de
            // gain, evite le cout de std::tanh par echantillon.
            const float x2 = x * x;
            return x * (27.0f + x2) / (27.0f + 9.0f * x2);
        }
    }

    void Reverb::prepareEngine (Engine& e, int engineIndex)
    {
        const float srScale = (float) (sampleRateD / 48000.0);
        const float loopDamp = kEngineLoopDamp[engineIndex];

        for (int i = 0; i < kNumCombs; ++i)
        {
            e.combsL[i].length = juce::jmax (4, (int) std::lround (kCombL[i] * srScale));
            e.combsR[i].length = juce::jmax (4, (int) std::lround (kCombR[i] * srScale));
            e.combsL[i].buffer.assign ((size_t) e.combsL[i].length, 0.0f);
            e.combsR[i].buffer.assign ((size_t) e.combsR[i].length, 0.0f);
            e.combsL[i].index = e.combsR[i].index = 0;
            e.combsL[i].store = e.combsR[i].store = 0.0f;
            e.combsL[i].damp1 = 1.0f - loopDamp;
            e.combsR[i].damp1 = 1.0f - loopDamp;
            e.combsL[i].damp2 = loopDamp;
            e.combsR[i].damp2 = loopDamp;
        }

        for (int i = 0; i < kNumAllpass; ++i)
        {
            e.allpassL[i].length = juce::jmax (2, (int) std::lround (kAllpassL[i] * srScale));
            e.allpassR[i].length = juce::jmax (2, (int) std::lround (kAllpassR[i] * srScale));
            e.allpassL[i].buffer.assign ((size_t) e.allpassL[i].length, 0.0f);
            e.allpassR[i].buffer.assign ((size_t) e.allpassR[i].length, 0.0f);
            e.allpassL[i].index = e.allpassR[i].index = 0;
            e.allpassL[i].feedback = 0.5f;
            e.allpassR[i].feedback = 0.5f;
        }
    }

    void Reverb::resetEngine (Engine& e)
    {
        for (int i = 0; i < kNumCombs; ++i)
        {
            std::fill (e.combsL[i].buffer.begin(), e.combsL[i].buffer.end(), 0.0f);
            std::fill (e.combsR[i].buffer.begin(), e.combsR[i].buffer.end(), 0.0f);
            e.combsL[i].index = e.combsR[i].index = 0;
            e.combsL[i].store = e.combsR[i].store = 0.0f;
        }
        for (int i = 0; i < kNumAllpass; ++i)
        {
            std::fill (e.allpassL[i].buffer.begin(), e.allpassL[i].buffer.end(), 0.0f);
            std::fill (e.allpassR[i].buffer.begin(), e.allpassR[i].buffer.end(), 0.0f);
            e.allpassL[i].index = e.allpassR[i].index = 0;
        }
    }

    void Reverb::prepare (double sampleRate, int numChannelsToProcess)
    {
        sampleRateD = sampleRate;
        numChannels = juce::jlimit (1, 2, numChannelsToProcess);

        for (int m = 0; m < kNumEngines; ++m)
            prepareEngine (engines[(size_t) m], m);

        // Pre-delay : ligne au plafond du reglage interne (+2 echantillons de
        // marge pour l'interpolation sans wrap fantome).
        preLength = (int) std::lround (kPredelayMaxMs * 0.001 * sampleRate) + 2;
        preL.assign ((size_t) preLength, 0.0f);
        preR.assign ((size_t) preLength, 0.0f);
        preIndex = 0;
        preTarget = juce::jlimit (0.0f, (float) preLength - 2.0f,
                                  kFixedPredelayMs * 0.001f * (float) sampleRate);
        preSamples = preTarget;

        reset();
    }

    void Reverb::reset()
    {
        for (auto& e : engines)
            resetEngine (e);
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        preIndex = 0;
        preSamples = preTarget;
        tailLevel = 0.0f;
    }

    void Reverb::setSettings (const Settings& s)
    {
        settings = s;

        bool toutMuet = true;
        for (int e = 0; e < kNumEngines; ++e)
        {
            engineGains[e] = juce::jlimit (0.0f, 1.0f, s.gains[e]);
            if (engineGains[e] > 0.0f)
                toutMuet = false;
        }

        inert = s.inert || toutMuet;

        if (sampleRateD <= 0.0)
            return;

        // RT60 NOMINAUX de chaque moteur : c'est LA couleur d'un knob — tourner
        // Short donne une queue courte, tourner Lush une longue nappe (chaque
        // bouton porte son propre RT60).
        for (int m = 0; m < kNumEngines; ++m)
        {
            const float rt60 = kEngineRt60[m];
            auto& eng = engines[(size_t) m];

            for (int i = 0; i < kNumCombs; ++i)
            {
                const float tL = (float) eng.combsL[i].length / (float) sampleRateD;
                const float tR = (float) eng.combsR[i].length / (float) sampleRateD;
                eng.combsL[i].feedback = std::pow (10.0f, -3.0f * tL / rt60);
                eng.combsR[i].feedback = std::pow (10.0f, -3.0f * tR / rt60);
            }
        }
    }

    void Reverb::beginBlock()
    {
        if (sampleRateD <= 0.0)
            return;

        // Cible de pre-delay du bloc (le glide vers cette cible se fait dans
        // processSample, par echantillon : changement sans clic).
        preTarget = juce::jlimit (0.0f, (float) preLength - 2.0f,
                                  kFixedPredelayMs * 0.001f * (float) sampleRateD);
    }

    float Reverb::processSample (float* const* channels, int sampleIndex)
    {
        if (inert)
            return channels[0][sampleIndex];

        const int n = juce::jlimit (1, 2, numChannels);
        const float dryL = channels[0][sampleIndex];
        const float dryR = n > 1 ? channels[1][sampleIndex] : dryL;

        // --- Pre-delay : ecriture puis lecture interpolee -------------------
        preL[(size_t) preIndex] = dryL;
        preR[(size_t) preIndex] = dryR;

        // Position de lecture en DOUBLE puis wrapping en ENTIER (bug corrige
        // le 2026-09-21 : le wrapping flottant `while (readPos < 0) readPos +=
        // preLength` arrondissait a exactement preLength quand la fraction
        // negative etait sous le demi-ulp de ~9600 (0,0005) — preIndex passant
        // par la valeur exacte de la cible convergee, readPos = -1,2e-6 tombait
        // dans ce piege et (int) renvoyait preLength : lecture hors bornes).
        const double readPosD = (double) preIndex - (double) preSamples;
        int   i0   = (int) std::floor (readPosD);
        const float frac = (float) (readPosD - (double) i0);
        while (i0 < 0)
            i0 += preLength;
        i0 %= preLength;
        const int   i1 = (i0 + 1) % preLength;
        const float inL = preL[(size_t) i0] * (1.0f - frac) + preL[(size_t) i1] * frac;
        const float inR = preR[(size_t) i0] * (1.0f - frac) + preR[(size_t) i1] * frac;

        preIndex = (preIndex + 1) % preLength;
        // Glide exponentiel 10 ms vers la cible.
        const float glide = 1.0f - std::exp (-1.0f / (kPredelayGlideMs * 0.001f * (float) sampleRateD));
        preSamples += (preTarget - preSamples) * glide;

        // --- Les 4 sous-reseaux CUMULES --------------------------------------
        // Chaque moteur traite l'entree pre-delayee avec ses propres combs et
        // allpass, a son PROPRE RT60 (couleur du bouton), et son curseur
        // (loi cubique) pondere sa contribution.
        float wetL = 0.0f, wetR = 0.0f;

        for (int m = 0; m < kNumEngines; ++m)
        {
            const float gain = engineGains[m];
            if (gain <= 0.0f)
                continue;   // moteur a 0 % : pas de calcul

            auto& eng = engines[(size_t) m];
            float eL = 0.0f, eR = 0.0f;

            for (int i = 0; i < kNumCombs; ++i)
            {
                {
                    auto& c = eng.combsL[i];
                    const float read = c.buffer[(size_t) c.index];
                    c.store = c.damp2 * read + c.damp1 * c.store;   // passe-bas de boucle
                    c.buffer[(size_t) c.index] = inL + c.store * c.feedback;
                    eL += read;
                    c.index = (c.index + 1) % c.length;
                }
                {
                    auto& c = eng.combsR[i];
                    const float read = c.buffer[(size_t) c.index];
                    c.store = c.damp2 * read + c.damp1 * c.store;
                    c.buffer[(size_t) c.index] = inR + c.store * c.feedback;
                    eR += read;
                    c.index = (c.index + 1) % c.length;
                }
            }
            eL *= kCombScale;
            eR *= kCombScale;

            for (int i = 0; i < kNumAllpass; ++i)
            {
                {
                    auto& a = eng.allpassL[i];
                    const float bufout = a.buffer[(size_t) a.index];
                    const float out = -eL + bufout;
                    a.buffer[(size_t) a.index] = eL + bufout * a.feedback;
                    eL = out;
                    a.index = (a.index + 1) % a.length;
                }
                {
                    auto& a = eng.allpassR[i];
                    const float bufout = a.buffer[(size_t) a.index];
                    const float out = -eR + bufout;
                    a.buffer[(size_t) a.index] = eR + bufout * a.feedback;
                    eR = out;
                    a.index = (a.index + 1) % a.length;
                }
            }

            wetL += gain * eL;
            wetR += gain * eR;
        }

        // --- Staging : lineaire aux petits signaux, plafond doux a 0,8 -----
        wetL = tanhApprox (wetL * kWetStage) * kTanhOut;
        wetR = tanhApprox (wetR * kWetStage) * kTanhOut;

        // Vumetre de queue (enveloppe lente du chemin humide).
        const float wetAbs = juce::jmax (std::abs (wetL), std::abs (wetR));
        tailLevel += (wetAbs - tailLevel) * 0.0005f;

        channels[0][sampleIndex] = dryL * kDryMix + wetL * kWetMix;
        if (n > 1)
            channels[1][sampleIndex] = dryR * kDryMix + wetR * kWetMix;

        return channels[0][sampleIndex];
    }

    float Reverb::tailLevelDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (tailLevel, -90.0f);
    }
}
