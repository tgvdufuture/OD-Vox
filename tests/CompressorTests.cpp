#include <juce_audio_processors/juce_audio_processors.h>

#include "Compressor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    double twoPi() { return 2.0 * juce::MathConstants<double>::pi; }

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> out ((size_t) n);

        for (int i = 0; i < n; ++i)
            out[(size_t) i] = amplitude
                                * (float) std::sin (twoPi() * (double) freq * (double) i / kSampleRate);

        return out;
    }

    float peakOf (const std::vector<float>& v, int from = 0, int to = -1)
    {
        if (to < 0) to = (int) v.size();

        float peak = 0.0f;

        for (int i = from; i < to; ++i)
            peak = juce::jmax (peak, std::abs (v[(size_t) i]));

        return peak;
    }

    float toDb (float gain) { return juce::Decibels::gainToDecibels (gain, -200.0f); }

    /** La table macro retenue, portee par src/Compressor.cpp.
        Dupliquee volontairement : un test qui importe la table du code teste ne
        detecterait pas une modification accidentelle de ses valeurs. */
    odvox::Compressor::Preset targetTable (float amount01)
    {
        struct Key { float amount; float ratio; float makeUpDb; };

        static constexpr Key keys[] =
        {
            { 0.00f, 1.50f,  1.7f },
            { 0.25f, 1.63f,  7.3f },
            { 0.50f, 1.79f, 13.1f },
            { 0.75f, 2.05f, 18.2f },
            { 1.00f, 4.37f, 29.2f },
        };

        amount01 = juce::jlimit (0.0f, 1.0f, amount01);

        // Le seuil est un LITTERAL ici, pas `kFixedThresholdDb` : la copie doit
        // rester independante du code teste, sinon elle n'en detecterait jamais
        // la modification (le harnais, lui, verifie le seuil au signal).
        constexpr float kThreshold = -50.0f;

        for (size_t i = 0; i + 1 < std::size (keys); ++i)
        {
            const auto& a = keys[i];
            const auto& b = keys[i + 1];

            if (amount01 <= b.amount)
            {
                const float t = (amount01 - a.amount) / (b.amount - a.amount);
                return { kThreshold,
                         juce::jmap (t, a.ratio, b.ratio),
                         odvox::Compressor::kDefaultReleaseMs,
                         juce::jmap (t, a.makeUpDb, b.makeUpDb) };
            }
        }

        const auto& last = keys[std::size (keys) - 1];
        return { kThreshold, last.ratio,
                 odvox::Compressor::kDefaultReleaseMs, last.makeUpDb };
    }

    /** Applique un signal au compresseur prepare pour `numChannels`, sur place.
        Le module lit `numChannels` pointeurs : lui passer un tableau mono alors
        qu'il est prepare stereo lit hors du tableau — comportement indefini.
        (Un hote fournit toujours le bon nombre de canaux ; un test doit le
        faire aussi.) */
    void runThrough (odvox::Compressor& comp, std::vector<float>& signal, int numChannels)
    {
        std::vector<float*> channels ((size_t) numChannels, signal.data());

        for (int i = 0; i < (int) signal.size(); ++i)
            comp.processSample (channels.data(), i);
    }

    /** Rend un signal mono a travers un processeur : le buffer fourni a
        `processBlock` est STEREO, comme un hote le ferait pour un plugin
        declare stereo — sinon le gate et le compresseur lisent un pointeur de
        canal absent du tableau mono (comportement indefini). */
    std::vector<float> renderThroughProcessor (ODVoxAudioProcessor& processor,
                                               const std::vector<float>& signal,
                                               int blockSize)
    {
        juce::MidiBuffer midi;
        std::vector<float> out (signal.size());

        for (int start = 0; start < (int) out.size(); start += blockSize)
        {
            const int n = juce::jmin (blockSize, (int) out.size() - start);
            juce::AudioBuffer<float> block (2, n);   // stereo : le bus declare

            for (int i = 0; i < n; ++i)
            {
                const float x = signal[(size_t) start + i];
                block.setSample (0, i, x);
                block.setSample (1, i, x);   // mono duplique sur les deux canaux
            }

            processor.processBlock (block, midi);

            for (int i = 0; i < n; ++i)
                out[(size_t) start + i] = block.getSample (0, i);
        }

        return out;
    }

    /** Reduction statique moyenne (dB) : reduction = entree - sortie (cretes),
        seconde moitie du rendu (regime etabli). */
    float measuredReductionDb (const odvox::Compressor::Settings& s, float inputLevel,
                               double seconds = 1.0)
    {
        odvox::Compressor comp;
        comp.prepare (kSampleRate, 1);
        comp.setSettings (s);

        auto signal = makeSine (1000.0f, inputLevel, seconds);
        runThrough (comp, signal, 1);

        const int half = (int) signal.size() / 2;
        const float inDb  = toDb (inputLevel);
        const float outDb = toDb (peakOf (signal, half));

        return juce::jmax (0.0f, inDb - outDb);
    }

    class CompressorTests final : public juce::UnitTest
    {
    public:
        CompressorTests() : juce::UnitTest ("Compresseur (F1.7b rev, US-05, fidelite cible)") {}

        void runTest() override
        {
            testTableFidelity();
            testFixedThreshold();
            testNeverDisabledByAmount();
            testLevelingRisesWithAmount();
            testMeterMatchesAppliedReduction();
            testAmountEngagesModule();
            testZeroAmountIsBitExact();
        }

    private:
        /** F1.7b : la table reproduit les cinq points mesures, au signal. */
        void testTableFidelity()
        {
            beginTest ("La table reproduit les cinq points mesures de la cible");

            // Verification non circulaire : la table du code est comparee a une
            // copie locale, point par point.
            for (float amount01 : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto mine = odvox::Compressor::tableFor (amount01);
                const auto ref  = targetTable (amount01);

                expectWithinAbsoluteError (mine.thresholdDb, ref.thresholdDb, 0.01f,
                                           "seuil fixe");
                expectWithinAbsoluteError (mine.ratio, ref.ratio, 0.01f, "ratio");
                expectWithinAbsoluteError (mine.makeUpDb, ref.makeUpDb, 0.01f, "make-up");
            }
        }

        /** F1.7b : le seuil est fixe ; ratio et make-up croissent. */
        void testFixedThreshold()
        {
            beginTest ("Le seuil est fixe quel que soit le curseur (fidelite F1.7b)");

            const auto at0   = odvox::Compressor::tableFor (0.0f);
            const auto at50  = odvox::Compressor::tableFor (0.5f);
            const auto at100 = odvox::Compressor::tableFor (1.0f);

            expectEquals (at0.thresholdDb, at50.thresholdDb, "seuil 0 % = 50 %");
            expectEquals (at0.thresholdDb, at100.thresholdDb, "seuil 0 % = 100 %");
            expectEquals (at0.releaseMs, at100.releaseMs, "release fixe");
            expect (at0.ratio < at50.ratio && at50.ratio < at100.ratio,
                    "le ratio croit avec le curseur");
            expect (at0.makeUpDb < at50.makeUpDb && at50.makeUpDb < at100.makeUpDb,
                    "le make-up croit avec le curseur");
        }

        /** Le signal faible SORT PLUS FORT que le fort (celle de la cible
            d'entree : -26 dBFS -> -53,6 dB ; -6 dBFS -> -53,6 dB aussi). */
        void testNeverDisabledByAmount()
        {
            beginTest ("Le curseur ne desactive jamais le compresseur (fidelite F1.7b)");

            // A 0 %, le module compresse deja a 1,5:1 : un signal au-dessus
            // du seuil fixe DOIT etre reduit, meme a zero.
            odvox::Compressor::Settings s;
            s.amountPct    = 0.0f;

            // -6 dBFS crete = 44 dB au-dessus du seuil fixe (-50) : a 1,5:1,
            // reduction theorique = 44 x (1 - 1/1.5) = 14,7 dB.
            const float reduction = measuredReductionDb (s, 0.5f);

            expect (reduction > 8.0f,
                    "a 0 %, la reduction doit rester : " + juce::String (reduction, 2)
                        + " dB mesures (theorie ~14,7 dB)");
        }

        /** Fidelite de FORME a la cible (harnais aussi) : le make-up croissant
            fait MONTER le niveau de sortie avec le curseur (nivellement). */
        void testLevelingRisesWithAmount()
        {
            beginTest ("Le make-up fait monter le niveau avec le curseur (fidelite F1.7b)");

            const auto peakAt = [&] (float amountPct)
            {
                odvox::Compressor::Settings s;
                s.amountPct = amountPct;
                s.enabled   = true;

                odvox::Compressor comp;
                comp.prepare (kSampleRate, 1);
                comp.setSettings (s);

                auto signal = makeSine (440.0f, 0.25f, 1.0);
                runThrough (comp, signal, 1);

                return toDb (peakOf (signal, (int) signal.size() / 2));
            };

            const float low  = peakAt (10.0f);
            const float mid  = peakAt (50.0f);
            const float high = peakAt (100.0f);

            expect (low < mid && mid < high,
                    "le niveau monte avec le curseur : " + juce::String (low, 1) + " < "
                        + juce::String (mid, 1) + " < " + juce::String (high, 1) + " dBFS");
        }

        /** AC2 de US-05 : le vumetre rapporte la GR de compression de table
            (seuil fixe -50, ratio interpole) — PAS le changement de niveau net,
            qui est GR − make-up depuis que la table porte elle-meme le
            nivellement (suppression du suiveur, rev du 2026-09-21). */
        void testMeterMatchesAppliedReduction()
        {
            beginTest ("Le vumetre de reduction colle a la GR de table (AC2 de US-05)");

            odvox::Compressor::Settings s;
            s.amountPct = 50.0f;
            s.enabled   = true;

            odvox::Compressor comp;
            comp.prepare (kSampleRate, 1);
            comp.setSettings (s);

            auto signal = makeSine (1000.0f, 0.5f, 1.0);   // -6 dBFS, loin au-dessus
            runThrough (comp, signal, 1);

            // GR theorique de TABLE a 50 % : le vumetre rapporte la compression
            // appliquee par le VCA, pas le bilan level (GR − make-up).
            const auto preset = odvox::Compressor::tableFor (0.5f);
            const float overDb  = toDb (0.5f) - preset.thresholdDb;   // 44 dB
            const float applied = overDb * (1.0f - 1.0f / preset.ratio);

            const float reported = comp.currentReductionDb();

            // Le vumetre rapporte la GR de regime (les cretes d'attaque sortent
            // de la moyenne de la seconde moitie) : tolerance large mais honnete.
            expect (reported > 1.0f,
                    "le vumetre allume sur un signal fort : " + juce::String (reported, 2) + " dB");
            expect (std::abs (reported - applied) < 3.0f,
                    "vumetre " + juce::String (reported, 2)
                        + " dB vs GR de table " + juce::String (applied, 2) + " dB");
        }

        /** Rev 2026-09-21 : le curseur ENGAGE le module — c'est le seul
            interrupteur, comme dans la cible. */
        void testAmountEngagesModule()
        {
            beginTest ("Le curseur engage le module (seul interrupteur)");

            // amount > 0 -> le processeur active le comp, meme sans autre
            // reglage ; amount = 0 -> court-circuit complet.
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);

            // Sans toucher a comp_amount (0 % par defaut), un signal traverse
            // la chaine SANS reduction rapportee — le module est inerte.
            const auto quiet = renderThroughProcessor (p, makeSine (440.0f, 0.05f, 0.4), 512);
            expect (p.lastCompReductionDb() < 0.01f,
                    "a 0 %, aucun reduction rapportee (module inerte) : "
                        + juce::String (p.lastCompReductionDb(), 3) + " dB");

            // Poser le curseur a 70 % engage : la reduction rapportee s'allume
            // des que le signal depasse le seuil fixe.
            p.setParameterActual ("comp_amount", 70.0f);
            const auto loud = renderThroughProcessor (p, makeSine (440.0f, 0.5f, 0.6), 512);
            expect (p.lastCompReductionDb() > 1.0f,
                    "a 70 %, la reduction s'allume : " + juce::String (p.lastCompReductionDb(), 2) + " dB");
        }

        /** Edge case : comp a 0 % = transparence au bit pres (court-circuit). */
        void testZeroAmountIsBitExact()
        {
            beginTest ("comp 0 % est transparent au bit pres (court-circuit du processeur)");

            constexpr int blockSize = 256;

            ODVoxAudioProcessor processor;

            // Defauts du catalogue : comp_amount a 0 % ; EQ neutralise pour
            // isoler (Air +2,5 dB, reglage tonal hors contrat US-02 AC2). Le
            // low cut et le filtre DC de sortie sont On par defaut — des
            // interrupteurs de NETTOYAGE, meme nature : on les eteint aussi.
            processor.setParameterActual ("gate_amount", 0.0f);
            processor.setParameterActual ("lowcut_amount", 0.0f);
            processor.setParameterActual ("output_dc_filter", 0.0f);
            processor.setParameterActual ("eq_on", 0.0f);
            processor.setParameterActual ("deess_amount", 0.0f);
            processor.setParameterActual ("drive_amount", 0.0f);
            processor.setParameterActual ("doubler_amount", 0.0f);
            processor.setParameterActual ("delay_amount", 0.0f);
            processor.setParameterActual ("reverb_short_pct", 0.0f);
            processor.setParameterActual ("reverb_small_pct", 0.0f);
            processor.setParameterActual ("reverb_big_pct", 0.0f);
            processor.setParameterActual ("reverb_lush_pct", 0.0f);

            // prepareToPlay APRES les reglages : il amorce le lissage sur les
            // valeurs courantes. Regler apres coup ne suffit pas ici, car un bloc
            // de 256 echantillons (5,3 ms) est plus court que la rampe de lissage
            // (~10 a 25 ms) — le module n'aurait pas encore atteint son etat.
            processor.prepareToPlay (kSampleRate, blockSize);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::AudioBuffer<float> reference (2, blockSize);
            juce::MidiBuffer midi;

            juce::Random random (20260918);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float x = random.nextFloat() * 0.5f - 0.25f;
                    buffer.setSample (ch, i, x);
                    reference.setSample (ch, i, x);
                }

            processor.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    expectEquals (buffer.getSample (ch, i), reference.getSample (ch, i),
                                  "comp 0 % : l'echantillon doit ressortir a l'identique");
        }
    };

    CompressorTests compressorTests;
}
