#include "Compressor.h"

#include <cmath>

namespace odvox
{
    // ======================================================================
    // La table macro retenue — cible de conception, mesuree au signal
    // ======================================================================
    // Modele etabli par mesure au signal : seuil FIXE ≈ -50 dBFS, ratio et
    // make-up croissent avec le curseur. Verification sur les cinq positions
    // mesurees (entree -48 dBFS, seuil -50 -> 2 dB au-dessus) :
    //
    //   0 %   : -48 - 2x(1-1/1.50) +  1.7 = -46.97 dB  (mesure -47.0)
    //   25 %  : -48 - 2x(1-1/1.63) +  7.3 = -41.47 dB  (mesure -41.5)
    //   50 %  : -48 - 2x(1-1/1.79) + 13.1 = -35.79 dB  (mesure -35.8)
    //   75 %  : -48 - 2x(1-1/2.05) + 18.2 = -30.81 dB  (mesure -30.8)
    //   100 % : -48 - 2x(1-1/4.37) + 29.2 = -19.69 dB  (mesure -20.3)
    //
    // Le make-up est un NIVELLEMENT : il remonte fortement les signaux faibles
    // (+29,2 dB a 100 %). Le bruit de fond remonte avec lui — c'est
    // l'architecture de la table, ou le gate en amont n'est pas un
    // luxe. Notre reproduction est fidele a ±0,8 dB sur les cinq points.
    // ======================================================================
    Compressor::Preset Compressor::tableFor (float amount01)
    {
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);

        struct Key { float amount; float ratio; float makeUpDb; };

        static constexpr Key keys[] =
        {
            { 0.00f, 1.50f,  1.7f },   // mesure : jamais desactive
            { 0.25f, 1.63f,  7.3f },
            { 0.50f, 1.79f, 13.1f },
            { 0.75f, 2.05f, 18.2f },
            { 1.00f, 4.37f, 29.2f },
        };

        for (size_t i = 0; i + 1 < std::size (keys); ++i)
        {
            const auto& a = keys[i];
            const auto& b = keys[i + 1];

            if (amount01 <= b.amount)
            {
                const float t = (amount01 - a.amount) / (b.amount - a.amount);
                return { kFixedThresholdDb,
                         juce::jmap (t, a.ratio, b.ratio),
                         kDefaultReleaseMs,
                         juce::jmap (t, a.makeUpDb, b.makeUpDb) };
            }
        }

        const auto& last = keys[std::size (keys) - 1];
        return { kFixedThresholdDb, last.ratio, kDefaultReleaseMs, last.makeUpDb };
    }

    void Compressor::prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        numChannels = juce::jmax (1, newNumChannels);

        reset();
    }

    void Compressor::reset()
    {
        detector         = 0.0f;
        gainDb           = 0.0f;
        worstReductionDb = 0.0f;
    }

    float Compressor::detectorCoefficient (double sr, float milliseconds) noexcept
    {
        const double seconds = juce::jmax (0.01, (double) milliseconds) * 0.001;
        return (float) (1.0 - std::exp (-1.0 / (seconds * sr)));
    }

    void Compressor::setSettings (const Settings& s)
    {
        settings = s;

        // Seul le PROCESSEUR (court-circuit a 0 %) rend le module inerte. La
        // table ne le fait JAMAIS : a 0 % le module compresse deja
        // a 1,5:1 quand il est engage.
        inert = ! s.enabled;

        if (inert)
        {
            reset();
            makeUpGainDb = 0.0f;
            return;
        }

        const auto preset = tableFor (s.amountPct * 0.01f);

        // La TABLE MESUREE pilote tout, sans exception (rev du 2026-09-21 :
        // le module n'expose aucun reglage de comp — un curseur, une idee).
        effectiveThresholdDb = preset.thresholdDb;
        effectiveRatio       = preset.ratio;
        effectiveMakeUpDb    = preset.makeUpDb;

        const float releaseMs = kDefaultReleaseMs;

        detectorAttackCoeff  = detectorCoefficient (sampleRate, kAttackMs);
        // Le coude « doux » : un detecteur qui retombe deux fois plus vite que la
        // rampe de gain adoucit la reprise, sans parametre `knee` expose.
        detectorReleaseCoeff = detectorCoefficient (sampleRate,
                                                    juce::jmax (1.0f, releaseMs * 0.5f));

        // Rampe de release : lineaire en dB, la plage de detection se parcourt
        // en exactement releaseMs (meme convention que le gate, qui divise sa
        // plage par le nombre d'echantillons du release).
        const float releaseSamples = juce::jmax (1.0f, releaseMs) * 0.001f * (float) sampleRate;
        releaseStepDb = kDetectorSpanDb / releaseSamples;
        attackStepDb  = 0.25f;   // dB par echantillon : ~12 dB a 48 kHz, transitoires passes
    }

    float Compressor::processSample (float* const* channels, int sampleIndex)
    {
        if (inert)
            return 0.0f;

        // Detecteur : melange RMS (energie) et crete (transitoires), comme un
        // detecteur de niveau vocal.
        float sumSquares = 0.0f;
        float peak       = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = channels[ch][sampleIndex];
            sumSquares += x * x;
            peak = juce::jmax (peak, std::abs (x));
        }

        const float rms   = std::sqrt (sumSquares / juce::jmax (1, numChannels));
        const float level = 0.6f * rms + 0.4f * peak;

        const bool rising = level > detector;
        detector += (level - detector)
                    * (rising ? detectorAttackCoeff : detectorReleaseCoeff);

        const float overDb   = juce::Decibels::gainToDecibels (detector, -100.0f)
                               - effectiveThresholdDb;
        const float targetDb = overDb > 0.0f
                                   ? -overDb * (1.0f - 1.0f / juce::jmax (1.0f, effectiveRatio))
                                   : 0.0f;

        // Rampe lineaire en dB : on descend vite (attaque fixe), on remonte au
        // rythme du release. Meme convention mesurable que le gate.
        if (targetDb < gainDb)
            gainDb = juce::jmax (targetDb, gainDb - attackStepDb);
        else
            gainDb = juce::jmin (targetDb, gainDb + releaseStepDb);

        // Make-up : la VALEUR DE TABLE, sans suiveur. Le suiveur de reduction
        // moyenne (l'AutoGain de l'ancien mode Custom) aplatissait le
        // nivellement de la cible — c'est la table qui porte la
        // montee de niveau (1,7 dB a 0 % -> 29,2 dB a 100 %), pas un compenseur
        // dynamique. Retire avec le mode Custom (rev du 2026-09-21).
        //
        // MAIS la valeur de table est AMORCEE en douceur (constante d'attaque,
        // 3 ms) : appliquee instantanement au premier echantillon, la montee de
        // +29 dB devance la GR (qui met ~3 ms a s'engager) et produit un
        // transitoire de +20 dBFS (mesure du 2026-09-21 : crete 9.985). Le
        // regime etabli — seule la valeur de table — reste exact.
        makeUpGainDb += (effectiveMakeUpDb - makeUpGainDb) * detectorAttackCoeff;

        const float gain = juce::Decibels::decibelsToGain (gainDb + makeUpGainDb);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][sampleIndex] *= gain;

        worstReductionDb = juce::jmax (worstReductionDb, juce::jmax (0.0f, -gainDb));

        return juce::jmax (0.0f, -gainDb);
    }
}
