#include <juce_audio_processors/juce_audio_processors.h>

#include "Gate.h"
#include "LowCut.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    double twoPi() { return 2.0 * juce::MathConstants<double>::pi; }

    float decibelsOf (double ratio)
    {
        return juce::Decibels::gainToDecibels ((float) ratio, -200.0f);
    }

    float decibelsFsOf (float peak)
    {
        return juce::Decibels::gainToDecibels (peak, -200.0f);
    }

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> out ((size_t) n);

        for (int i = 0; i < n; ++i)
            out[(size_t) i] = amplitude
                                * (float) std::sin (twoPi() * (double) freq * (double) i / kSampleRate);

        return out;
    }

    double rmsOf (const std::vector<float>& v, int from, int to)
    {
        from = juce::jlimit (0, (int) v.size(), from);
        to   = juce::jlimit (from, (int) v.size(), to);

        double sum = 0.0;

        for (int i = from; i < to; ++i)
            sum += (double) v[(size_t) i] * (double) v[(size_t) i];

        return std::sqrt (sum / juce::jmax (1, to - from));
    }

    float peakOf (const std::vector<float>& v)
    {
        float peak = 0.0f;

        for (auto x : v)
            peak = juce::jmax (peak, std::abs (x));

        return peak;
    }

    /** Enveloppe de crete par fenetres : c'est ce qui permet de lire une rampe de
        gain sur un sinus, la ou un simple RMS melangerait la rampe et la forme du
        signal. */
    std::vector<float> peakEnvelope (const std::vector<float>& v, int window)
    {
        std::vector<float> out;

        for (int start = 0; start < (int) v.size(); start += window)
            out.push_back (peakOf (std::vector<float> (v.begin() + start,
                                                       v.begin() + juce::jmin ((int) v.size(),
                                                                               start + window))));

        return out;
    }

    void processMono (odvox::Gate& gate, std::vector<float>& signal)
    {
        float* channels[1] { signal.data() };

        for (int i = 0; i < (int) signal.size(); ++i)
            gate.processSample (channels, i);
    }

    /** Reponse d'un low cut a une frequence donnee, en dB, une fois le regime
        etabli : la premiere moitie du signal est jetee. */
    float lowCutResponseDb (const odvox::LowCut::Settings& settings, float freq,
                            double seconds = 0.4)
    {
        odvox::LowCut filter;
        filter.prepare (kSampleRate, 1);
        filter.setSettings (settings);
        filter.reset();

        auto signal = makeSine (freq, 0.5f, seconds);
        float* channels[1] { signal.data() };

        for (int i = 0; i < (int) signal.size(); ++i)
            filter.processSample (channels, i);

        const int settle = (int) signal.size() / 2;

        double inEnergy = 0.0, outEnergy = 0.0;

        for (int i = settle; i < (int) signal.size(); ++i)
        {
            const double x = 0.5 * std::sin (twoPi() * (double) freq * (double) i / kSampleRate);
            inEnergy  += x * x;
            outEnergy += (double) signal[(size_t) i] * (double) signal[(size_t) i];
        }

        return decibelsOf (std::sqrt (outEnergy / juce::jmax (1.0e-30, inEnergy)));
    }

    /** Reponse de la chaine complete a une frequence, lue a travers le processeur :
        c'est ce qui valide le chemin parametre -> reglages derives -> filtre, et
        pas seulement le filtre tout seul. */
    float processorResponseDb (ODVoxAudioProcessor& processor, float freq, double seconds = 1.0)
    {
        constexpr int blockSize = 512;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        const int total  = (int) (seconds * kSampleRate);
        const int settle = total / 2;

        double phase = 0.0;
        const double step = twoPi() * (double) freq / kSampleRate;
        double inEnergy = 0.0, outEnergy = 0.0;

        for (int start = 0; start < total; start += blockSize)
        {
            const int n = juce::jmin (blockSize, total - start);

            for (int i = 0; i < n; ++i)
            {
                const float v = 0.5f * (float) std::sin (phase);

                phase += step;

                if (phase > twoPi())
                    phase -= twoPi();

                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            processor.processBlock (buffer, midi);

            for (int i = 0; i < n; ++i)
            {
                if (start + i < settle)
                    continue;

                const double y = buffer.getSample (0, i);
                const double x = 0.5 * std::sin (twoPi() * (double) freq * (double) (start + i) / kSampleRate);

                inEnergy  += x * x;
                outEnergy += y * y;
            }
        }

        return decibelsOf (std::sqrt (outEnergy / juce::jmax (1.0e-30, inEnergy)));
    }

    /** Pilote la chaine complete avec un sinus a phase continue. Un sinus plutot
        qu'un signal continu : la suite du lot implementera le filtre DC de sortie,
        et ces tests ne doivent pas en dependre. */
    struct ChainFeeder
    {
        static constexpr int kBlockSize = 512;
        static constexpr double kToneHz = 1000.0;

        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        ODVoxAudioProcessor processor;

        double phase = 0.0;

        ChainFeeder() : buffer (2, kBlockSize)
        {
            processor.prepareToPlay (kSampleRate, kBlockSize);
        }

        void run (float amplitude, double seconds, std::vector<float>* recorded = nullptr)
        {
            const int blocks = (int) std::ceil (seconds * kSampleRate / kBlockSize);
            const double step = twoPi() * kToneHz / kSampleRate;

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < kBlockSize; ++i)
                {
                    const float v = amplitude * (float) std::sin (phase);

                    phase += step;

                    if (phase > twoPi())
                        phase -= twoPi();

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.setSample (ch, i, v);
                }

                processor.processBlock (buffer, midi);

                if (recorded != nullptr)
                    recorded->insert (recorded->end(),
                                      buffer.getReadPointer (0),
                                      buffer.getReadPointer (0) + kBlockSize);
            }
        }
    };

    class DspModuleTests final : public juce::UnitTest
    {
    public:
        DspModuleTests() : juce::UnitTest ("Modules DSP : entree, gate, low cut (F1.3 a F1.5)") {}

        void runTest() override
        {
            testGateReductionAndTransparency();
            testGateLeavesPlosiveOnsetIntact();
            testLowCutCornerAndSlope();
            testLowCutTransparency();
            testLowCutHasNoHiddenControls();
            testLowCutToggleReachesTheFilter();
            testCalibrationPlacesPeakInRange();
            testCalibrationCancelLeavesGainUnchanged();
            testCalibrationFadesWithoutClipping();
            testCalibrationGivesUpWithoutSignal();
            testDefaultChainIsBitExact();
            testDefaultEqDeviationIsOnlyTheAirBand();
        }

    private:
        // --- F1.4 : gate -------------------------------------------------------

        void testGateReductionAndTransparency()
        {
            beginTest ("Le gate reduit le silence de la valeur reglee (AC1 et AC3 de US-03)");

            odvox::Gate gate;
            gate.prepare (kSampleRate, 1);

            odvox::Gate::Settings settings;
            settings.thresholdDb = -45.0f;
            settings.releaseMs   = 150.0f;
            settings.rangeDb     = 80.0f;
            settings.amountPct   = 100.0f;
            gate.setSettings (settings);

            // Deux niveaux : une « voix » a -12 dBFS, puis une sonde a -54 dBFS qui
            // represente le silence tout en restant mesurable — un vrai silence
            // donnerait un rapport 0 sur 0.
            auto voice = makeSine (1000.0f, 0.25f, 0.5);
            auto probe = makeSine (1000.0f, 0.002f, 1.0);

            processMono (gate, voice);
            processMono (gate, probe);

            const double probeIn  = 0.002 / std::sqrt (2.0);
            const double probeOut = rmsOf (probe, (int) probe.size() / 2, (int) probe.size());
            const float reduction = decibelsOf (probeOut / probeIn);

            expect (reduction <= -60.0f,
                    "la reduction doit depasser 60 dB (AC1), mesuree : "
                        + juce::String (reduction, 2) + " dB");
            expectWithinAbsoluteError (reduction, -80.0f, 1.0f,
                                       "la reduction doit egaler le range interne 80 dB (AC3, rev. ancres figees)");

            // US-02 AC2 : a 0 % le module est strictement transparent, bit a bit.
            odvox::Gate transparent;
            transparent.prepare (kSampleRate, 1);

            auto idle = settings;
            idle.amountPct = 0.0f;
            transparent.setSettings (idle);

            auto dry = makeSine (440.0f, 0.3f, 0.1);
            auto through = dry;

            processMono (transparent, through);

            expect (through == dry, "a 0 % le gate doit rendre le signal a l'identique");
        }

        void testGateLeavesPlosiveOnsetIntact()
        {
            beginTest ("Le gate ne tronque pas la premiere consonne (AC2 de US-03)");

            odvox::Gate gate;
            gate.prepare (kSampleRate, 1);

            odvox::Gate::Settings settings;
            settings.thresholdDb = -45.0f;
            settings.releaseMs   = 150.0f;
            settings.rangeDb     = 80.0f;
            settings.amountPct   = 100.0f;
            gate.setSettings (settings);

            // Le gate est referme par une sonde, puis une plosive arrive.
            auto probe = makeSine (1000.0f, 0.002f, 0.4);
            processMono (gate, probe);

            const int onsetSamples = (int) (0.020 * kSampleRate);   // les 20 ms de l'AC
            auto burst = makeSine (1000.0f, 0.25f, 0.02);

            auto through = burst;
            processMono (gate, through);

            const float attenuation = decibelsOf (rmsOf (through, 0, onsetSamples)
                                                    / juce::jmax (1.0e-30, rmsOf (burst, 0, onsetSamples)));

            expect (attenuation > -0.5f,
                    "l'attenuation sur les 20 premieres ms doit rester negligeable, mesuree : "
                        + juce::String (attenuation, 3) + " dB");
        }

        // --- F1.5 : low cut ---------------------------------------------------

        void testLowCutCornerAndSlope()
        {
            beginTest ("Le coupe-bas coupe a -3 dB a 120 Hz avec 24 dB/oct, FIGE (AC1 de F1.5)");

            odvox::LowCut::Settings settings;
            settings.enabled = true;

            const float corner = odvox::LowCut::kFixedHz;

            const float atCorner = lowCutResponseDb (settings, corner);

            expectWithinAbsoluteError (atCorner, -3.0f, 0.6f,
                                       "-3 dB attendu au coin figé (120 Hz), mesure "
                                           + juce::String (atCorner, 2) + " dB");

            // La pente se mesure une octave dans la bande attenuee. On reste a
            // corner/2 et corner/4 : plus bas, le float32 ne porte plus la mesure.
            const float measured = lowCutResponseDb (settings, corner / 2.0f)
                                     - lowCutResponseDb (settings, corner / 4.0f);

            expectWithinAbsoluteError (measured, (float) odvox::LowCut::kFixedSlopeDbOct, 3.0f,
                                       "pente mesuree " + juce::String (measured, 1)
                                           + " dB par octave (figee a "
                                           + juce::String (odvox::LowCut::kFixedSlopeDbOct) + ")");
        }

        void testLowCutTransparency()
        {
            beginTest ("Le coupe-bas FERME est strictement transparent, au bit pres (AC4 de F1.5)");

            odvox::LowCut filter;
            filter.prepare (kSampleRate, 1);

            odvox::LowCut::Settings settings;
            settings.enabled = false;

            filter.setSettings (settings);

            auto dry = makeSine (80.0f, 0.4f, 0.2);
            auto through = dry;

            float* channels[1] { through.data() };

            for (int i = 0; i < (int) through.size(); ++i)
                filter.processSample (channels, i);

            expect (through == dry,
                    "interrupteur ferme, le filtre doit rendre le signal a l'identique, echantillon par echantillon");
            expectEquals (filter.numActiveStages(), 0, "aucun biquad actif quand l'interrupteur est ferme");
        }

        void testLowCutHasNoHiddenControls()
        {
            beginTest ("Le coupe-bas n'a AUCUN reglage a cote de son interrupteur (F1.5 revu)");

            // C'est le contrat de la revision du 2026-09-19 : un module fige n'a
            // pas de potards caches. Le test porte sur le CATALOGUE, pas sur le
            // module — c'est le catalogue qui decide de ce que l'utilisateur voit.
            const auto all = odvox::params::declared();

            juce::StringArray cleanup;
            bool isToggle = false;

            for (const auto& info : all)
                if (info.module == odvox::params::Module::cleanup)
                {
                    cleanup.add (info.id);

                    if (juce::String (info.id) == "lowcut_amount")
                        isToggle = info.unit == odvox::params::Unit::boolean;
                }

            expectEquals (cleanup.size(), 1,
                          "le module Nettoyage ne doit porter qu'un parametre : "
                              + cleanup.joinIntoString (", "));
            expectEquals (cleanup[0], juce::String ("lowcut_amount"), "l'unique parametre du Nettoyage");
            expect (isToggle, "`lowcut_amount` doit etre un BOOLEEN : c'est un interrupteur, pas un dosage");

            // Et le module lui-meme ne propose qu'une chose a regler.
            odvox::LowCut filter;
            filter.prepare (kSampleRate, 2);

            odvox::LowCut::Settings on;
            on.enabled = true;

            filter.setSettings (on);

            expectEquals (filter.numActiveStages(), odvox::LowCut::kNumStages,
                          "interrupteur ouvert : le passe-haut figé est en place ("
                              + juce::String (odvox::LowCut::kNumStages) + " biquads)");
            expectWithinAbsoluteError (filter.cutoffHz(), odvox::LowCut::kFixedHz, 0.01f,
                                       "frequence de coupure figee a "
                                           + juce::String (odvox::LowCut::kFixedHz, 1) + " Hz");
        }

        void testLowCutToggleReachesTheFilter()
        {
            beginTest ("L'interrupteur du coupe-bas atteint bien le filtre, a travers le processeur");

            // Le test existe pour la meme raison que l'ancien test d'index -> pente :
            // un module juste ne prouve pas que le PARAMETRE l'atteint. Le harnais
            // hors ligne avait trouve un index de choix compare a une pente ;
            // ici, un booleen lu comme un pourcentage laisserait le coupe-bas
            // ouvert en permanence (20 % >= 0,5 -> vrai) ou ferme a tort.
            ODVoxAudioProcessor processor;
            processor.prepareToPlay (kSampleRate, 512);

            processor.setParameterActual ("eq_air_db", 0.0f);   // sort l'Air du chemin

            processor.setParameterActual ("lowcut_amount", 0.0f);
            const float closed = processorResponseDb (processor, 60.0f);

            processor.setParameterActual ("lowcut_amount", 1.0f);
            const float open = processorResponseDb (processor, 60.0f);

            // A 60 Hz, soit une octave sous le coin de 120 Hz, un 24 dB/oct doit
            // couper d'environ 27 dB.
            expectWithinAbsoluteError (open - closed, -27.0f, 3.0f,
                                       "a 60 Hz l'ecart ouvert/ferme doit valoir ~-27 dB, mesure "
                                           + juce::String (open - closed, 1) + " dB");

            processor.setParameterActual ("lowcut_amount", 0.0f);

            expectWithinAbsoluteError (processorResponseDb (processor, 60.0f), closed, 0.01f,
                                       "refermer l'interrupteur doit rendre exactement la reponse de depart");
        }

        // --- F1.3 : calibration ------------------------------------------------

        void testCalibrationPlacesPeakInRange()
        {
            beginTest ("La calibration place la crete entre -12 et -6 dBFS (AC1 de US-06)");

            ChainFeeder feeder;
            feeder.processor.setParameterActual ("input_gain_db", 0.0f);

            const float amplitude = 0.0316f;   // environ -30 dBFS crete

            feeder.run (amplitude, 0.2);
            feeder.processor.startCalibration();
            feeder.run (amplitude, 6.0);       // 5 s de mesure, puis la rampe

            std::vector<float> settled;
            feeder.run (amplitude, 0.2, &settled);

            const float peakDb = decibelsFsOf (peakOf (settled));

            expect (peakDb >= -12.0f && peakDb <= -6.0f,
                    "la crete appliquee doit tomber dans -12..-6 dBFS, mesuree : "
                        + juce::String (peakDb, 2) + " dBFS");
        }

        void testCalibrationCancelLeavesGainUnchanged()
        {
            beginTest ("Annuler la calibration laisse le gain d'entree inchange (AC2 de US-06)");

            ChainFeeder feeder;
            feeder.processor.setParameterActual ("input_gain_db", -3.0f);

            const float before = feeder.processor.captureParameters().at ("input_gain_db");

            feeder.processor.startCalibration();
            feeder.run (0.0316f, 2.0);

            expect (feeder.processor.calibrationState() == odvox::Calibrator::State::measuring,
                    "la mesure doit etre en cours au bout de 2 s");

            feeder.processor.cancelCalibration();
            feeder.run (0.0316f, 1.0);

            expect (feeder.processor.calibrationState() == odvox::Calibrator::State::idle,
                    "l'annulation doit ramener la calibration au repos");
            expect (feeder.processor.captureParameters().at ("input_gain_db") == before,
                    "le gain d'entree doit etre reste identique, a la valeur normalisee pres");
        }

        void testCalibrationFadesWithoutClipping()
        {
            beginTest ("La calibration applique son gain en fondu, sans depasser 0 dBFS (AC3 de US-06)");

            ChainFeeder feeder;
            feeder.processor.setParameterActual ("input_gain_db", 0.0f);

            const float amplitude = 0.05f;   // environ -26 dBFS crete : il faut monter

            feeder.run (amplitude, 0.2);
            feeder.processor.startCalibration();

            std::vector<float> recorded;
            feeder.run (amplitude, 6.5, &recorded);

            expect (peakOf (recorded) <= 1.0f,
                    "aucun echantillon ne doit depasser 0 dBFS, crete mesuree : "
                        + juce::String (peakOf (recorded), 4));

            // La rampe dure 50 ms. On lit l'enveloppe de crete par fenetres de 128
            // echantillons (2,7 ms) et on chronometre le passage du niveau de depart
            // au niveau final, avec 0,1 dB de tolerance de part et d'autre.
            const int window = 128;
            const auto envelope = peakEnvelope (recorded, window);

            const float oldLevel = amplitude;
            const float newLevel = peakOf (recorded);

            int startIndex = -1;

            for (int i = 0; i < (int) envelope.size(); ++i)
                if (envelope[(size_t) i] > oldLevel * 1.0116f)
                {
                    startIndex = i;
                    break;
                }

            expect (startIndex >= 0, "le changement de gain doit etre visible dans le signal");

            int endIndex = -1;

            for (int i = juce::jmax (0, startIndex); i < (int) envelope.size(); ++i)
                if (envelope[(size_t) i] >= newLevel * 0.9886f)
                {
                    endIndex = i;
                    break;
                }

            expect (endIndex > startIndex, "la rampe doit se terminer");

            const double rampMs = 1000.0 * (double) (endIndex - startIndex)
                                    * (double) window / kSampleRate;

            expect (rampMs >= 45.0,
                    "le fondu doit durer au moins ~50 ms, mesure : "
                        + juce::String (rampMs, 1) + " ms");
        }

        void testCalibrationGivesUpWithoutSignal()
        {
            beginTest ("Sans son pendant 10 s, la calibration abandonne (cas limite de F1.3)");

            ChainFeeder feeder;
            feeder.processor.setParameterActual ("input_gain_db", 0.0f);

            const float before = feeder.processor.captureParameters().at ("input_gain_db");

            feeder.processor.startCalibration();
            feeder.run (0.0f, 11.0);

            expect (feeder.processor.calibrationState() == odvox::Calibrator::State::abandoned,
                    "la calibration doit abandonner apres 10 s sans son");
            expect (feeder.processor.captureParameters().at ("input_gain_db") == before,
                    "le gain doit rester inchange apres un abandon");
        }

        // --- Transparence de la chaine ----------------------------------------

        /** Contrat AMENDE le 2026-09-19, apres verification du comportement reel.

            `eq_air_db` vaut +2,5 dB par defaut — la valeur retenue a la
            conception, et §3.4 du PRD la veut par defaut.
            L'Air n'est pas un curseur d'intensite mais un reglage TONAL : le
            contrat de US-02 AC2 (« mettre chaque curseur a 0 % rend le module
            transparent ») porte sur les curseurs, pas sur les reglages tonaux.
            La chaine aux defauts du catalogue n'est donc PAS bit-exacte : elle
            rend la voix avec +2,5 dB d'air, comme la cible.

            Les deux verites sont verifiees ici : (1) l'ecart au defaut est
            EXACTEMENT la bande Air — remettre `eq_air_db` a 0 dB rend la chaine
            bit-exacte, et rien d'autre ne bouge ; (2) EQ neutralise, tout module
            inerte traverse la chaine sans la modifier d'un bit. */
        void testDefaultChainIsBitExact()
        {
            beginTest ("Aux valeurs par defaut avec l'EQ neutralise, la chaine est transparente au bit pres (AC2 de US-02)");

            constexpr int blockSize = 512;

            ODVoxAudioProcessor processor;
            processor.prepareToPlay (kSampleRate, blockSize);

            // Defauts du catalogue : gains a 0 dB, `gate_amount`,
            // `comp_amount`, `deess_amount` et `drive_amount` a 0 %, donc tous
            // les modules de traitement sont inertes. Seul l'EQ
            // est actif par defaut (Air +2,5 dB) : on le neutralise ici.
            // AMENDEMENT du 2026-09-29 : `lowcut_amount` est ON par defaut
            // (comme la cible) — c'est un reglage TONAL, hors du contrat de
            // transparence des curseurs ; ce test l'eteint explicitement pour
            // verifier la transparence BIT-EXACTE des curseurs.
            // Filtre DC de sortie (2026-10-09) : meme nature que le low cut —
            // interrupteur de nettoyage, On par defaut, hors du contrat de
            // transparence des curseurs. Eteint ici pour la meme raison.
            processor.setParameterActual ("eq_on", 0.0f);
            processor.setParameterActual ("lowcut_amount", 0.0f);
            processor.setParameterActual ("output_dc_filter", 0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::AudioBuffer<float> reference (2, blockSize);
            juce::MidiBuffer midi;

            juce::Random random (20260918);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

            reference.makeCopyOf (buffer);
            processor.processBlock (buffer, midi);

            int differing = 0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (buffer.getSample (ch, i) != reference.getSample (ch, i))
                        ++differing;

            expectEquals (differing, 0,
                          "la chaine doit rendre l'entree a l'identique quand tous les "
                          "curseurs sont a 0 % et l'EQ neutralise");
        }

        /** L'ecart des defauts est la bande Air, et rien que la bande Air.
            Mesure au signal plutot que re-derivation des coefficients de JUCE :
            une cloche a 100 Hz et 300 Hz ne doit RIEN changer (0,05 dB),
            l'Air doit etre un plateau haut d'environ +2,5 dB, et remettre
            `eq_air_db` a 0 dB doit rendre la chaine bit-exacte.

            AMENDEMENT du 2026-09-29 : `lowcut_amount` est ON par defaut. Le
            coupe-bas est un reglage TONAL du defaut (comme l'Air), et son
            passe-haut a 120 Hz perturberait la mesure a 100 Hz : le test
            l'eteint pour isoler la deviation de l'EQ. */
        void testDefaultEqDeviationIsOnlyTheAirBand()
        {
            beginTest ("Aux defauts, l'ecart de la chaine est exactement la bande Air (+2,5 dB)");

            ODVoxAudioProcessor processor;
            processor.prepareToPlay (kSampleRate, 512);
            processor.setParameterActual ("lowcut_amount", 0.0f);
            // Meme raison pour le filtre DC de sortie (2026-10-09), lui aussi
            // On par defaut : sa mesure a 100 Hz serait perturbee.
            processor.setParameterActual ("output_dc_filter", 0.0f);

            const auto gainAt = [&] (float freq)
            {
                const auto in = makeSine (freq, 0.2f, 0.5);
                auto out = in;

                juce::AudioBuffer<float> buffer (2, (int) in.size());

                for (size_t i = 0; i < in.size(); ++i)
                    buffer.setSample (0, (int) i, in[i]);

                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);

                for (size_t i = 0; i < out.size(); ++i)
                    out[i] = buffer.getSample (0, (int) i);

                // Regime etabli (les 100 premieres ms sont le transitoire des
                // filtres), mesure par correlation a la frequence sonde.
                const int skip = (int) (0.1 * kSampleRate);
                double a = 0.0, b = 0.0, na = 0.0;

                for (int i = skip; i < (int) out.size(); ++i)
                {
                    const double phase = twoPi() * (double) freq * (double) i / kSampleRate;
                    a  += (double) out[(size_t) i] * std::sin (phase);
                    b  += (double) out[(size_t) i] * std::cos (phase);
                    na += (double) in[(size_t) i] * std::sin (phase);
                }

                const double outAmp = 2.0 * std::sqrt (a * a + b * b) / (double) (out.size() - skip);
                const double inAmp  = 2.0 * std::abs (na) / (double) (out.size() - skip);

                return decibelsOf (outAmp / inAmp);
            };

            const float low  = gainAt (100.0f);
            const float mids = gainAt (300.0f);
            // Le plateau de l'Air se lit a 15 kHz, pas a 12 : pour un shelf de
            // JUCE, le COIN vaut exactement la moitie du plateau en dB (+1,25 sur
            // +2,5) et le plateau s'atteint une demi-octave au-dessus.
            const float air  = gainAt (15000.0f);

            expect (std::abs (low) < 0.05f,
                    "une cloche a 100 Hz reste neutre : " + juce::String (low, 3) + " dB");
            expect (std::abs (mids) < 0.05f,
                    "une cloche a 300 Hz reste neutre : " + juce::String (mids, 3) + " dB");
            expect (air > 2.3f,
                    "l'Air est un plateau haut : " + juce::String (air, 3)
                        + " dB a 15 kHz (attendu > +2,3 — un shelf de JUCE a Q = 1,0 "
                          "atteint son plateau dans la bande, la ou Q = 0,71 plafonne a +1,7)");

            // Neutraliser la SEULE bande Air suffit a rendre la chaine exacte.
            processor.setParameterActual ("eq_air_db", 0.0f);

            juce::MidiBuffer midi;

            // Les parametres de module sont LISSES (~10 a 25 ms) : sans bloc de
            // chauffe, la bande Air n'a pas encore atteint 0 dB et le test
            // mesurerait le fondu au lieu de la transparence.
            juce::AudioBuffer<float> warmUp (2, 4096);
            warmUp.clear();
            processor.processBlock (warmUp, midi);

            juce::Random random (20260919);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioBuffer<float> reference (2, 512);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

            reference.makeCopyOf (buffer);
            processor.processBlock (buffer, midi);

            int differing = 0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    if (buffer.getSample (ch, i) != reference.getSample (ch, i))
                        ++differing;

            expectEquals (differing, 0,
                          "Air a 0 dB : la chaine entiere doit redevenir bit-exacte");
        }
    };

    DspModuleTests dspModuleTests;
}
