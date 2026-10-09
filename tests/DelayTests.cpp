#include <juce_audio_processors/juce_audio_processors.h>

#include "Delay.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kSeconds    = 3.0;

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);

        if (param != nullptr)
            param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    /** Toute la chaine au silence SAUF le delay : chaque module amont est
        court-circuite (0 %), pour que le test mesure le delay seul. */
    void neutraliseChain (ODVoxAudioProcessor& p)
    {
        setActual (p, "gate_amount",   0.0f);
        setActual (p, "lowcut_amount", 0.0f);
        setActual (p, "output_dc_filter", 0.0f);
        setActual (p, "eq_on",         0.0f);
        setActual (p, "comp_amount",  0.0f);
        setActual (p, "deess_amount",  0.0f);
        setActual (p, "drive_amount",  0.0f);
        setActual (p, "doubler_amount", 0.0f);
        setActual (p, "width_amount",  0.0f);
    }

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * (double) freq * (double) i / kSampleRate));

        return v;
    }

    /** Impulsion mono : la matiere premiere des tests d'echos. */
    std::vector<float> makeImpulse (double seconds, double impulseAt)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n, 0.0f);

        const int k = (int) (impulseAt * kSampleRate);
        if (k >= 0 && k < n)
            v[(size_t) k] = 1.0f;

        return v;
    }

    std::vector<std::vector<float>> makeStereo (const std::vector<float>& mono)
    {
        return { mono, mono };
    }

    std::vector<std::vector<float>> run (ODVoxAudioProcessor& p,
                                         const std::vector<std::vector<float>>& in)
    {
        const int n = (int) in[0].size();
        juce::AudioBuffer<float> buffer (2, n);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, in[(size_t) ch][(size_t) i]);

        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) n));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                out[(size_t) ch][(size_t) i] = buffer.getSample (ch, i);

        return out;
    }

    /** Des blocs de silence pour laisser le lissage de temps converger : le
        glide (15 ms) decroît d'un facteur e^-1 par ~15 ms, donc 8 blocs de
        512 (85 ms) laissent moins de 0,1 % de l'ecart initial. */
    void settle (ODVoxAudioProcessor& p)
    {
        const auto silence = makeStereo (std::vector<float> (512, 0.0f));
        for (int b = 0; b < 8; ++b)
            run (p, silence);
    }

    double windowRms (const std::vector<std::vector<float>>& x, int ch, double t0, double t1)
    {
        const int i0 = (int) (t0 * kSampleRate);
        const int i1 = juce::jmin ((int) x[(size_t) ch].size(), (int) (t1 * kSampleRate));

        double sum = 0.0;
        int    n   = 0;

        for (int i = juce::jmax (0, i0); i < i1; ++i)
        {
            const float v = x[(size_t) ch][(size_t) i];
            sum += (double) v * (double) v;
            ++n;
        }

        return n > 0 ? std::sqrt (sum / (double) n) : 0.0;
    }

    /** Ecart en dB entre deux niveaux RMS. */
    double dbDiff (double a, double b)
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b));
    }

    /** Parametres d'un delay actif et simple : echoes nets, pas de ducking.

        rev. suppression du mode Avance : le feedback est une constante de
        conception (20 %) reglee DANS le module — un test qui veut un echo plus
        riche la pose directement via la Settings du module. */
    void setPlainDelay (ODVoxAudioProcessor& p, int divisionIndex, float feedback01 = 0.20f)
    {
        auto st = p.delaySettingsForTests();
        st.feedback01 = juce::jlimit (0.0f, 0.95f, feedback01);
        p.setDelaySettingsForTests (st);

        setActual (p, "delay_amount",   100.0f);
        setActual (p, "delay_time",     (float) divisionIndex);
        setActual (p, "delay_sync",     1.0f);
        setActual (p, "delay_ducking",  0.0f);
    }

    class DelayTests final : public juce::UnitTest
    {
    public:
        DelayTests() : juce::UnitTest ("Delay ping-pong, ducking et tempo (F1.11)") {}

        void runTest() override
        {
            testMappingMusical();
            testModeLibre();
            testTransparenceAmountZero();
            testPingPongAlterneLesCanaux();
            testFeedbackPlafonneNEmballePas();
            testEchoSuitLeNiveauDuAmount();
            testDuckingProfondeurPuisRelache();
            testChangementDeTempoSansClic();
        }

    private:
        // =====================================================================
        // AC1 : le mapping division x tempo est musical. Mesure au signal :
        // a 120 BPM, 1/4 = 0,500 s exactement. On exige la meme
        // exactitude (1 ms) et la dependance au tempo (1/2 @ 60 = 2 s).
        // =====================================================================
        void testMappingMusical()
        {
            beginTest ("Le mapping division x tempo est musical (AC1)");

            struct Case { int index; const char* label; double beats; double bpm; };
            constexpr Case cases[] =
            {
                { 10, "1/4  @ 120", 1.0,  120.0 },
                { 11, "1/4D @ 120", 1.5,  120.0 },
                {  9, "1/4T @ 120", 2.0 / 3.0, 120.0 },
                { 13, "1/2  @ 120", 2.0,  120.0 },
                { 13, "1/2  @  60", 2.0,   60.0 },
                {  7, "1/8  @ 120", 0.5,  120.0 },
            };

            for (const auto& c : cases)
            {
                ODVoxAudioProcessor p;
                neutraliseChain (p);
                setPlainDelay (p, c.index);
                p.setTestBpm ((float) c.bpm);
                p.prepareToPlay (kSampleRate, 512);
                settle (p);

                const double expected = c.beats * 60.0 / c.bpm * 1000.0;
                const double got      = (double) p.delayCurrentMs();

                expectWithinAbsoluteError (got, expected, 1.0,
                                           juce::String (c.label) + " : attendu "
                                               + juce::String (expected, 1) + " ms, obtenu "
                                               + juce::String (got, 1) + " ms");
            }
        }

        // =====================================================================
        // AC1 : le mode libre court-circuite le tempo.
        // =====================================================================
        void testModeLibre()
        {
            beginTest ("Le mode libre utilise delay_time_ms et ignore le tempo");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setPlainDelay (p, 10);
            setActual (p, "delay_sync",   0.0f);
            setActual (p, "delay_time_ms", 750.0f);
            p.setTestBpm (137.0f); // sans effet en mode libre
            p.prepareToPlay (kSampleRate, 512);
            settle (p);

            expectWithinAbsoluteError ((double) p.delayCurrentMs(), 750.0, 1.0,
                                       "le temps libre doit etre utilise tel quel");

            // Les bornes : 1 ms et 2000 ms restent dans la ligne (plafond 8 s).
            setActual (p, "delay_time_ms", 2000.0f);
            settle (p);
            expectWithinAbsoluteError ((double) p.delayCurrentMs(), 2000.0, 1.0,
                                       "la borne haute du temps libre est accessible");

            setActual (p, "delay_time_ms", 1.0f);
            settle (p);
            expect ((double) p.delayCurrentMs() <= 2.0,
                    "la borne basse du temps libre est accessible");
        }

        // =====================================================================
        // AC de chaine : delay_amount 0 % court-circuite le module ENTIER,
        // la sortie est bit-exacte des le premier echantillon.
        // =====================================================================
        void testTransparenceAmountZero()
        {
            beginTest ("A 0 % le module est bit-exact (transparence)");

            const auto in = makeStereo (makeSine (220.0f, 0.3f, 1.0));

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "delay_amount", 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, in);

            int diffs = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < in[0].size(); ++i)
                    if (out[(size_t) ch][i] != in[(size_t) ch][i])
                        ++diffs;

            expectEquals (diffs, 0, "aucun echantillon ne doit changer a 0 %");
        }

        // =====================================================================
        // Definition du ping-pong : les echos ALTERNENT de canal. Defaut corrige
        // le 2026-09-20 : le sec etait ecrit dans les deux lignes, les echos
        // sortaient identiques en L et R — un double delay, pas un ping-pong.
        // =====================================================================
        void testPingPongAlterneLesCanaux()
        {
            beginTest ("Les echos alternent de canal a chaque repetition (ping-pong)");

            // Impulsion a 0,2 s, 1/4 @ 120 BPM = echos a 0,7 / 1,2 / 1,7 s…
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setPlainDelay (p, 10, 0.60f);
            p.setTestBpm (120.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (kSeconds, 0.2));
            const auto out = run (p, in);

            const double dt = 0.5; // le temps reellement mesure, relit via le processeur
            juce::ignoreUnused (dt);
            const double step = (double) p.delayCurrentMs() * 0.001;
            expectWithinAbsoluteError (step, 0.5, 0.001, "1/4 @ 120 BPM = 0,5 s");

            // Le sec de l'impulsion est present des 0,2 s sur les deux canaux.
            const double dryL = windowRms (out, 0, 0.199, 0.201);
            const double dryR = windowRms (out, 1, 0.199, 0.201);
            expect (dryL > 0.1 && dryR > 0.1, "le sec n'est jamais retarde");

            // Echo 1 : CANAL GAUCHE seulement. Echo 2 : CANAL DROIT seulement.
            const double e1L = windowRms (out, 0, 0.7 - 0.02, 0.7 + 0.02);
            const double e1R = windowRms (out, 1, 0.7 - 0.02, 0.7 + 0.02);
            const double e2L = windowRms (out, 0, 1.2 - 0.02, 1.2 + 0.02);
            const double e2R = windowRms (out, 1, 1.2 - 0.02, 1.2 + 0.02);
            const double e3L = windowRms (out, 0, 1.7 - 0.02, 1.7 + 0.02);
            const double e3R = windowRms (out, 1, 1.7 - 0.02, 1.7 + 0.02);

            expect (e1L > 0.02, "le premier echo existe a gauche");
            expect (dbDiff (e1R, e1L) < -20.0,
                    "le premier echo est absent a droite : " + juce::String (dbDiff (e1R, e1L), 1) + " dB");
            expect (e2R > 0.005, "le second echo existe a droite");
            expect (dbDiff (e2L, e2R) < -20.0,
                    "le second echo est absent a gauche : " + juce::String (dbDiff (e2L, e2R), 1) + " dB");
            expect (dbDiff (e3R, e3L) < -20.0, "le troisieme echo est reparti a gauche");

            // Le rapport e3/e2 est le seul propre : les deux ont traverse la
            // boucle (l'echo 1 sort de l'ecriture brute, non filtree — mesure
            // du 2026-09-20 : e2/e1 valait -9,3 dB a cause du filtre de
            // boucle, pas du feedback). Successivement e(n+1) = fb x F(e(n))
            // avec F deja applique a e2 : le ratio tend vers fb = -4,4 dB.
            const double ratio = dbDiff (e3L, e2R);
            expect (ratio < -3.0 && ratio > -6.5,
                    "l'echo 3 vaut environ le feedback (0,6 = -4,4 dB) : "
                        + juce::String (ratio, 1) + " dB");
        }

        // =====================================================================
        // AC3 : feedback au plafond 95 % pendant 12 s — la serie des echos
        // decroit STRICTEMENT (l'emballement est impossible par construction).
        // =====================================================================
        void testFeedbackPlafonneNEmballePas()
        {
            beginTest ("Feedback a 95 % : la serie d'echos decroit strictement (AC3)");

            const double seconds = 12.0;
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setPlainDelay (p, 10, 0.95f); // le PLAFOND de la constante de conception
            p.setTestBpm (120.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (seconds, 0.2));
            const auto out = run (p, in);

            // Echos du canal GAUCHE : un sur deux seulement (ping-pong), donc
            // espacés de 2 x 0,5 = 1,0 s. Erreur corrigee le 2026-09-20 : la
            // moitie de mes fenetres etaient vides, ce qui comptait des
            // « croissances » imaginaires.
            double previous = 0.0;
            int    decreasing = 0;
            int    counted    = 0;

            for (int k = 0; k < 12; ++k)
            {
                const double t = 0.7 + 1.0 * (double) k;
                if (t + 0.05 > seconds)
                    break;

                const double peak = windowRms (out, 0, t - 0.02, t + 0.02);
                if (k > 0 && peak < previous)
                    ++decreasing;
                previous = peak;
                ++counted;
            }

            expect (counted >= 10, "au moins 10 echos doivent exister sur 12 s");
            expectEquals (decreasing, counted - 1,
                          "chaque echo doit etre plus faible que le precedent");
        }

        // =====================================================================
        // Le knob delay_amount est le NIVEAU des echos : 50 % = -6 dB de wet,
        // le sec intact.
        // =====================================================================
        void testEchoSuitLeNiveauDuAmount()
        {
            beginTest ("delay_amount 50 % = 6 dB d'echo en moins que 100 %");

            auto render = [&] (float amountPct)
            {
                ODVoxAudioProcessor p;
                neutraliseChain (p);
                setPlainDelay (p, 10);
                setActual (p, "delay_amount", amountPct);
                p.setTestBpm (120.0f);
                p.prepareToPlay (kSampleRate, 512);

                const auto in  = makeStereo (makeImpulse (2.0, 0.2));
                return run (p, in);
            };

            const auto full = render (100.0f);
            const auto half = render (50.0f);

            const double wFull = windowRms (full, 0, 0.7 - 0.02, 0.7 + 0.02);
            const double wHalf = windowRms (half, 0, 0.7 - 0.02, 0.7 + 0.02);
            const double db    = dbDiff (wFull, wHalf);

            expectWithinAbsoluteError (db, 6.0, 0.5,
                                       "50 % doit valoir 6 dB de moins que 100 %");
        }

        // =====================================================================
        // US-15 / AC2 : le ducking efface les echos sous la voix (profondeur
        // mesuree) puis les RELACHE en moins de 300 ms. Defaut corrige le
        // 2026-09-20 : l'enveloppe etait lisse au lieu de la rampe, le duck
        // persistait des secondes apres la fin du mot.
        // =====================================================================
        void testDuckingProfondeurPuisRelache()
        {
            // Voix : sinus continu 1,2 s puis SILENCE 0,8 s. Le delay tient
            // des echos a 0,25 s d'intervalle (1/8 @ 120) : la bande 1,4-1,6 s
            // ne contient QUE des echos (la voix est finie), a ~0,3 s de la
            // fin du mot — la zone ou AC2 juge le relachement.
            auto render = [&] (float duckPct)
            {
                ODVoxAudioProcessor p;
                neutraliseChain (p);
                setPlainDelay (p, 7, 0.50f);
                setActual (p, "delay_ducking", duckPct);
                p.setTestBpm (120.0f);
                p.prepareToPlay (kSampleRate, 512);

                auto voice = makeSine (220.0f, 0.25f, kSeconds);
                for (size_t i = (size_t) (1.2 * kSampleRate); i < voice.size(); ++i)
                    voice[i] = 0.0f;

                return run (p, makeStereo (voice));
            };

            const auto noDuck = render (0.0f);
            const auto ducked = render (100.0f);

            // --- Profondeur PENDANT la voix --------------------------------
            // La voix est bit-exacte (le sec n'est jamais touche), donc la
            // soustraction des deux rendus isole les echos a un echantillon pres.
            double sumNo = 0.0;
            double sumDk = 0.0;
            for (int i = (int) (0.5 * kSampleRate); i < (int) (0.9 * kSampleRate); ++i)
            {
                const double wNo = (double) noDuck[0][(size_t) i] - (double) noDuck[1][(size_t) i];
                const double wDk = (double) ducked[0][(size_t) i] - (double) ducked[1][(size_t) i];
                sumNo += wNo * wNo;
                sumDk += wDk * wDk;
            }

            const double depthDb = 10.0 * std::log10 (sumNo / juce::jmax (1.0e-20, sumDk));
            expect (depthDb > 10.0,
                    "le ducking a 100 % doit effacer les echos d'au moins 12 dB "
                    "(mesure " + juce::String (depthDb, 1) + " dB) — tolerance 10 dB "
                    "car la fenetre contient le moment ou le duck s'installe");

            // --- Relachement (AC2 : < 300 ms) -------------------------------
            // A ~0,3 s apres la fin du mot, la rampe (release 180 ms) a decru
            // a 19 % : le duck residual vaut environ -2,6 dB, donc l'echo
            // revient a moins de 3 dB de son niveau non ducky. Le defaut corrige
            // laissait -10 dB et plus : c'est ce qui est exclu ici.
            const double lateNo = windowRms (noDuck, 0, 1.4, 1.6);
            const double lateDk = windowRms (ducked, 0, 1.4, 1.6);
            const double residual = dbDiff (lateNo, lateDk);

            expect (residual > 0.2,
                    "le ducking doit rester actif en residu juste apres le mot ("
                        + juce::String (residual, 2) + " dB)");
            expect (residual < 6.0,
                    "les echos doivent revenir en moins de 300 ms (AC2) : residu "
                        + juce::String (residual, 1) + " dB seulement — un residu fort "
                        "signifie un relachement trop lent");
        }

        // =====================================================================
        // Cas limite du PRD : changement de tempo SANS clic — le temps glisse
        // (15 ms), l'artefact est un glissement de pitch bref, pas une
        // discontinuite.
        // =====================================================================
        void testChangementDeTempoSansClic()
        {
            beginTest ("Un changement de tempo ne produit pas de clic");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setPlainDelay (p, 13, 0.30f); // 1/2, echo bien audible
            p.setTestBpm (120.0f);
            p.prepareToPlay (kSampleRate, 512);

            // Signal continu doux, rendu en blocs de 512 : le tempo change
            // entre deux blocs.
            const int numBlocks = 80;
            const int blockLen  = 512;
            juce::AudioBuffer<float> buffer (2, numBlocks * blockLen);

            double phase = 0.0;
            const double omega = 2.0 * juce::MathConstants<double>::pi * 110.0 / kSampleRate;
            for (int i = 0; i < numBlocks * blockLen; ++i)
            {
                const float v = (float) (0.2 * std::sin (phase));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                phase += omega;
            }

            juce::MidiBuffer midi;
            for (int b = 0; b < numBlocks; ++b)
            {
                if (b == numBlocks / 2)
                    p.setTestBpm (200.0f); // saut brutal, en pleine lecture

                float* chans[2] = { buffer.getWritePointer (0, b * blockLen),
                                    buffer.getWritePointer (1, b * blockLen) };
                juce::AudioBuffer<float> view (chans, 2, blockLen);
                p.processBlock (view, midi);
            }

            // Aucun echantillon ne doit sauter : l'ecart entre voisins reste
            // borné par la pente du signal + le glide. Un clic (deplacement de
            // tete de lecture sans interpolation) produirait un ecart bien plus
            // grand que la pente naturelle d'un sinus a 0,2.
            double worst = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 1; i < numBlocks * blockLen; ++i)
                    worst = juce::jmax (worst,
                                        std::abs ((double) buffer.getSample (ch, i)
                                                  - (double) buffer.getSample (ch, i - 1)));

            expect (worst < 0.06,
                    "discontinuite max " + juce::String (worst, 4)
                        + " : le temps doit glisser, pas sauter");
            expect (! std::isnan (worst), "pas de NaN");
        }
    };

    static DelayTests delayTests;
}
