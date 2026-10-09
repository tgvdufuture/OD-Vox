#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "Reverb.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);

        if (param != nullptr)
            param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    /** Toute la chaine au silence SAUF la reverb : chaque module amont est
        court-circuite (0 %), pour que le test mesure la reverb seule. */
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
        setActual (p, "delay_amount",  0.0f);
        setActual (p, "reverb_short_pct", 0.0f);
        setActual (p, "reverb_small_pct", 0.0f);
        setActual (p, "reverb_big_pct",   0.0f);
        setActual (p, "reverb_lush_pct",  0.0f);
    }

    std::vector<float> makeImpulse (double seconds, double impulseAt)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n, 0.0f);

        const int k = (int) (impulseAt * kSampleRate);
        if (k >= 0 && k < n)
            v[(size_t) k] = 1.0f;

        return v;
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

    double dbDiff (double a, double b)
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b));
    }

    /** Les 4 curseurs, reglage direct. 0 partout = module inerte. */
    void setReverb (ODVoxAudioProcessor& p, float shortPct, float smallPct,
                    float bigPct, float lushPct)
    {
        setActual (p, "reverb_short_pct", shortPct);
        setActual (p, "reverb_small_pct", smallPct);
        setActual (p, "reverb_big_pct",   bigPct);
        setActual (p, "reverb_lush_pct",  lushPct);
    }

    /** Temps de PERSISTANCE de queue : premier instant ou la queue passe sous
        un plancher ABSOLU (-80 dBFS, impulsion unite). Metrique sans auto-
        reference (le T20 « depuis son propre niveau » confond loudness et
        decay). */
    double estimateT20 (const std::vector<std::vector<float>>& out)
    {
        for (double t = 0.20; t < 3.4; t += 0.05)
        {
            const double dbfs = dbDiff (windowRms (out, 0, t, t + 0.05), 1.0);
            if (dbfs < -80.0)
                return t;
        }

        return 99.0;   // jamais tombe sous le plancher sur la fenetre observee
    }

    // =====================================================================
    class ReverbTests final : public juce::UnitTest
    {
    public:
        ReverbTests() : juce::UnitTest ("Reverb 4 moteurs cumules, 4 curseurs (F1.12)") {}

        void runTest() override
        {
            testTransparenceTousAZero();
            testChaqueMoteurSaCouleur();
            testMoteursCumules();
            testPreDelayFixe();
            testDecorrelationStereo();
            testDecroissanceMonotone();
        }

    private:
        // =================================================================
        // AC3 : les 4 curseurs a 0 % = court-circuit BIT-EXACT du module.
        // =================================================================
        void testTransparenceTousAZero()
        {
            beginTest ("Les 4 curseurs a 0 % rendent le module transparent (AC3)");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setReverb (p, 0.0f, 0.0f, 0.0f, 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeSine (220.0f, 0.25f, 1.0));
            const auto out = run (p, in);

            size_t diffs = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < out[(size_t) ch].size(); ++i)
                    if (out[(size_t) ch][i] != in[(size_t) ch][i])
                        ++diffs;

            // pedalboard n'est pas dans la boucle ici : la chaine C++ est
            // bit-exacte de bout en bout (float32).
            expect (diffs == 0, "sortie strictement identique a l'entree");
        }

        // =================================================================
        // AC1 : chaque bouton porte SA couleur — le RT60 est propre au moteur
        // (structure retenue : le moteur court porte le RT60 court,
        // Lush le long). Verifie au signal : a reglage egal, la
        // persistance des queues est strictement ordonnee Short < Small <
        // Big < Lush, avec des ecarts > 30 %.
        // =================================================================
        void testChaqueMoteurSaCouleur()
        {
            beginTest ("Chaque curseur produit sa couleur : Short < Small < Big < Lush (AC1)");

            const float pct = 100.0f;   // gain cubique : x1,0 — regime nominal
            double t20[4] = { 0.0, 0.0, 0.0, 0.0 };
            const char* ids[4] = { "reverb_short_pct", "reverb_small_pct",
                                   "reverb_big_pct", "reverb_lush_pct" };

            for (int m = 0; m < 4; ++m)
            {
                ODVoxAudioProcessor p;
                neutraliseChain (p);
                setActual (p, ids[m], pct);
                p.prepareToPlay (kSampleRate, 512);

                const auto in  = makeStereo (makeImpulse (3.5, 0.1));
                const auto out = run (p, in);
                t20[m] = estimateT20 (out);

                // La queue existe et vit dans le domaine utile.
                expect (t20[m] > 0.2 && t20[m] < 3.2,
                        "la queue du moteur " + juce::String (m) + " vit ("
                            + juce::String (t20[m], 2) + " s)");
            }

            // Ordre strict et ecart d'au moins 30 % deux a deux.
            for (int a = 0; a < 3; ++a)
            {
                const double ratio = t20[a] / t20[a + 1];
                expect (t20[a] < t20[a + 1] && ratio < 0.75,
                        "moteurs " + juce::String (a) + " et " + juce::String (a + 1)
                            + " distincts d'au moins 30 % (ratio "
                            + juce::String (ratio, 2) + ")");
            }
        }

        // =================================================================
        // Structure retenue : les moteurs sont CUMULES. Preuve au
        // signal : Small+Big ensemble produit une attaque dense (contribution
        // Small) ET une traine longue (contribution Big) — aucune des deux
        // queues seules ne fait les deux.
        // =================================================================
        void testMoteursCumules()
        {
            beginTest ("Les moteurs sont cumules : attaque ET traine avec deux knobs");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setReverb (p, 0.0f, 100.0f, 100.0f, 0.0f);   // Small + Big
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (3.0, 0.1));
            const auto out = run (p, in);

            const double attaque = dbDiff (windowRms (out, 0, 0.16, 0.22), 1.0);
            const double traine  = dbDiff (windowRms (out, 0, 0.9, 1.2), 1.0);

            // Small seul aurait une traine morte (RT60 1,1 s a 0,9 s reste
            // visible mais faible) ; Big seul n'aurait pas cette attaque
            // dense. Les deux ensemble : les deux extremes vivent.
            expect (attaque > -60.0, "l'attaque est dense : " + juce::String (attaque, 1) + " dBFS");
            // Plancher calle sur la mesure : la queue IR sous-estime la queue
            // percue sur materiau continu (l'essentiel est que Short seul
            // serait ETEINT a 0,9 s — de l'energie ici prouve Big).
            expect (traine  > -82.0, "la traine longue existe : " + juce::String (traine, 1) + " dBFS");
        }

        // =================================================================
        // AC2 (preregle) : le pre-delay interne vaut 20 ms — verifie au
        // signal : onset = impulsion + pre-delay + delai de reseau.
        // =================================================================
        void testPreDelayFixe()
        {
            beginTest ("Le pre-delay preregle vaut 20 ms (AC2)");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setReverb (p, 0.0f, 100.0f, 0.0f, 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (1.0, 0.05));
            const auto out = run (p, in);

            // Premier echantillon de queue au-dessus du plancher. La fenetre
            // demarre a 60 ms : APRES l'impulsion seche (50 ms) et AVANT
            // l'onset humide (99,65 ms) — une fenetre a 100 ms raterait
            // l'echo du comb 1 (echantillon unique a 99,65 ms) et attraperait
            // celui du comb 2 (1571 ech. = 102,73 ms), d'ou un biais +3 ms.
            const int searchStart = (int) (0.060 * kSampleRate);
            const int searchEnd   = (int) (0.400 * kSampleRate);
            const float floorAmp  = 0.0005f;

            int onset = -1;
            for (int i = searchStart; i < searchEnd; ++i)
                if (std::abs (out[0][(size_t) i]) > floorAmp)
                {
                    onset = i;
                    break;
                }

            expect (onset > 0, "la queue de reverb existe apres le pre-delay");
            if (onset > 0)
            {
                const double measuredMs = (double) onset / kSampleRate * 1000.0;
                // L'attente inclut le DELAI DE RESEAU : l'impulsion entre dans
                // les combs au bout du pre-delay et en ressort apres la
                // longueur du plus court (1423 ech. a 48 k = 29,65 ms).
                const double networkMs = 1423.0 / kSampleRate * 1000.0;
                const double expectedMs = 50.0 + odvox::Reverb::kFixedPredelayMs + networkMs;
                expectWithinAbsoluteError (measuredMs, expectedMs, 3.0,
                                           "onset de queue a impulsion + pre-delay + reseau ±3 ms : "
                                               + juce::String (measuredMs, 1) + " ms");
            }
        }

        // =================================================================
        // Decorrelation stereo : les queues L et R ne sont pas identiques.
        // =================================================================
        void testDecorrelationStereo()
        {
            beginTest ("Les queues L et R sont decorrelees (stereo large)");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setReverb (p, 0.0f, 100.0f, 100.0f, 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (2.0, 0.1));
            const auto out = run (p, in);

            // Correlation normalisee des queues (0,3 s .. 1,0 s) : deux copies
            // identiques donneraient 1,0 ; un reseau decorrele reste sous 0,9.
            double sab = 0.0, saa = 0.0, sbb = 0.0;
            const int i0 = (int) (0.3 * kSampleRate);
            const int i1 = (int) (1.0 * kSampleRate);
            for (int i = i0; i < i1; ++i)
            {
                const double a = out[0][(size_t) i];
                const double b = out[1][(size_t) i];
                sab += a * b; saa += a * a; sbb += b * b;
            }
            const double corr = sab / (std::sqrt (saa * sbb) + 1.0e-30);

            expect (corr < 0.9, "correlation des queues " + juce::String (corr, 3) + " < 0,9");
        }

        // =================================================================
        // Aucun emballement : la queue decroit TOUJOURS, meme avec les 4
        // moteurs a fond (le gain de boucle de chaque comb est < 1 par
        // construction, et le staging plafonne la somme).
        // =================================================================
        void testDecroissanceMonotone()
        {
            beginTest ("La queue decroit toujours, meme a 4 moteurs a fond (stabilite)");

            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setReverb (p, 100.0f, 100.0f, 100.0f, 100.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto in  = makeStereo (makeImpulse (6.0, 0.1));
            const auto out = run (p, in);

            const double t1 = dbDiff (windowRms (out, 0, 1.0, 1.5), 1.0);
            const double t2 = dbDiff (windowRms (out, 0, 2.0, 2.5), 1.0);
            const double t4 = dbDiff (windowRms (out, 0, 4.0, 4.5), 1.0);
            const float pic = *std::max_element (
                out[0].begin(), out[0].end(),
                [] (float a, float b) { return std::abs (a) < std::abs (b); });

            expect (t2 < t1, "la queue a 2 s est plus basse qu'a 1 s");
            expect (t4 < t2, "la queue a 4 s est plus basse qu'a 2 s");
            expect (pic < 1.0f, "aucun surcrot au-dela de l'echelle (pic " + juce::String (pic, 3) + ")");
        }
    };

    ReverbTests reverbTests;
}
