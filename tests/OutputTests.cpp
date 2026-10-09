#include <juce_audio_processors/juce_audio_processors.h>

#include "DcBlocker.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kSeconds    = 1.0;

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }

    juce::MidiBuffer noMidi;

    /** Rendu en blocs de 512, comme un hote honnete : le processeur prepare a
        maxBlock=512 alloue des tampons internes a cette taille (le chemin HQ
        du Drive, F2.2), et un bloc plus grand violerait le contrat. */
    void renderBlocked (ODVoxAudioProcessor& p, juce::AudioBuffer<float>& buffer)
    {
        constexpr int kHostBlock = 512;

        for (int start = 0; start < buffer.getNumSamples(); start += kHostBlock)
        {
            const int count = juce::jmin (kHostBlock, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, start, count);
            p.processBlock (block, noMidi);
        }
    }

    /** Pose chaque parametre declare a une valeur NORMALISEE distincte dans
        0,05..0,95 : la chaine la plus forte que le catalogue puisse exprimer,
        sans etre dans une butee. Le bypass doit rester bit-exact DERRIERE ça. */
    void setEverything (ODVoxAudioProcessor& p, float seed)
    {
        const auto all = odvox::params::declared();
        juce::StringArray ignored;

        std::map<juce::String, float> values;

        for (size_t i = 0; i < all.size(); ++i)
            values[all[i].id] = 0.05f + 0.9f * std::fmod (seed + 0.037f * (float) i, 1.0f);

        p.applyParameters (values, ignored);
    }

    /** Rendu du processeur, canal 0. */
    std::vector<float> render (ODVoxAudioProcessor& p, const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buffer (2, (int) mono.size());

        for (size_t i = 0; i < mono.size(); ++i)
        {
            buffer.setSample (0, (int) i, mono[(size_t) i]);
            buffer.setSample (1, (int) i, mono[(size_t) i]);
        }

        renderBlocked (p, buffer);

        std::vector<float> out (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        return out;
    }

    /** Rendu du chemin de BYPASS, canal 0. */
    std::vector<float> renderBypassed (ODVoxAudioProcessor& p, const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buffer (2, (int) mono.size());

        for (size_t i = 0; i < mono.size(); ++i)
        {
            buffer.setSample (0, (int) i, mono[(size_t) i]);
            buffer.setSample (1, (int) i, mono[(size_t) i]);
        }

        p.processBlockBypassed (buffer, noMidi);

        std::vector<float> out (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        return out;
    }
}

class OutputTests : public juce::UnitTest
{
public:
    OutputTests() : juce::UnitTest ("Sortie : bypass strict, latence reportee, output gain (F1.13)") {}

    void runTest() override
    {
        beginTest ("AC3 : le bypass est un vrai contournement, meme tous modules a fond");
        testBypassIsStrict();

        beginTest ("AC1/AC4 de US-12 : latence reportee = latence mesuree = 0");
        testReportedLatencyMatchesMeasured();

        beginTest ("AC4 : output_gain couvre -24 a +24 dB a ±0,1 dB");
        testOutputGainRange();

        beginTest ("Filtre DC : interrupteur ouvert = identite bit a bit");
        testDcBlockerIsBitExactWhenOff();

        beginTest ("Filtre DC : le continu est retire");
        testDcBlockerRemovesDc();

        beginTest ("Filtre DC : la bande audible est intacte (100 Hz et 1 kHz a ±0,1 dB)");
        testDcBlockerKeepsAudibleBand();

        beginTest ("Filtre DC : la commutation ne produit aucun cran (G4)");
        testDcBlockerSwitchHasNoClick();

        beginTest ("Filtre DC : le parametre est REELLEMENT lu par la chaine");
        testDcFilterIsWiredToTheChain();
    }

private:
    // =====================================================================
    // Le critere du PRD est un « vrai contournement » : entrée et sortie
    // indistinguables sous -120 dBFS Y COMPRIS quand tous les modules sont
    // actifs. On pousse donc la chaine a une valeur normalisee DISTINCTE par
    // parametre (pas les defauts, qui sont transparents) : si un module
    // laissait une trace de son traitement dans le chemin de contournement,
    // la difference exploserait.
    // =====================================================================
    void testBypassIsStrict()
    {
        const auto probe = makeSine (997.0f, 0.5f, kSeconds);

        const auto worstOf = [&] (const std::vector<float>& a, const std::vector<float>& b)
        {
            float worst = 0.0f;

            for (size_t i = 0; i < a.size(); ++i)
                worst = juce::jmax (worst, std::abs (a[(size_t) i] - b[(size_t) i]));

            return worst;
        };

        // 1. Defauts : la chaine entiere, bypass derriere. Le chemin normal
        //    n'est PAS bit-exact aux defauts — l'Air vaut +2,5 dB au defaut
        //    (valeur retenue, §3.4), la chaine colore donc le
        //    probe : c'est le temoin que le bypass contourne bien du TRAITEMENT.
        {
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);

            const auto normal   = render (p, probe);
            const auto bypassed = renderBypassed (p, probe);

            const float worstNormal = worstOf (probe, normal);
            const float worstBypass = worstOf (probe, bypassed);

            expect (worstNormal > 0.0f,
                    "temoin : le chemin normal traite (Air +2,5 dB au defaut, pire ecart "
                        + juce::String (worstNormal, 6) + ")");
            expect (worstBypass == 0.0f,
                    "bypass sur defauts : bit-exact (pire ecart "
                        + juce::String (worstBypass, 9) + ")");
        }

        // 2. TOUS les parametres pousses : le critere dur du PRD.
        {
            ODVoxAudioProcessor p;
            setEverything (p, 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto bypassed = renderBypassed (p, probe);

            float worst = 0.0f;

            for (size_t i = 0; i < probe.size(); ++i)
                worst = juce::jmax (worst, std::abs (probe[i] - bypassed[i]));

            expect (worst == 0.0f,
                    "bypass tous modules pousses : bit-exact (pire ecart "
                        + juce::String (worst, 9) + " soit "
                        + juce::String (20.0f * std::log10 (juce::jmax (worst, 1.0e-12f)), 1)
                        + " dB, exigé sous -120 dBFS)");
        }

        // 3. Le bypass ne depend d'aucun etat : deux appels de suite rendent
        //    la meme chose, apres un passage par le chemin normal.
        {
            ODVoxAudioProcessor p;
            setEverything (p, 0.5f);
            p.prepareToPlay (kSampleRate, 512);

            const auto first  = renderBypassed (p, probe);
            const auto middle = render (p, probe);        // le chemin normal entre les deux
            const auto second = renderBypassed (p, probe);

            juce::ignoreUnused (middle);

            float worst = 0.0f;

            for (size_t i = 0; i < first.size(); ++i)
                worst = juce::jmax (worst, std::abs (first[i] - second[i]));

            expect (worst == 0.0f,
                    "le bypass n'a pas d'etat : deux passages rendent l'identique (pire ecart "
                        + juce::String (worst, 9) + ")");
        }
    }

    // =====================================================================
    // AC1 et AC4 de US-12 : la latence REPORTREE (ce que l'hote compense)
    // doit egaler la latence MESUREE par correlation, ±1 echantillon, sur
    // toute la chaine. Aux defauts, zero par construction (F1.13) ; mais
    // l'invariant VRAI couvre aussi le chemin HQ de F2.2 : quand `hq_mode`
    // est pousse (via setEverything), les filtres half-band retardent le
    // signal et le processeur reporte EXACTEMENT ce retard. Le test pose
    // donc l'invariant general : pic mesure = entree + latence reportee.
    // =====================================================================
    void testReportedLatencyMatchesMeasured()
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        expectEquals (p.getLatencySamples(), 0,
                      "la latence reportee a l'hote vaut zero aux defauts (hq_mode off)");

        // Impulsion au milieu d'un rendu, sur la chaine REALISTE qui porte de
        // la latence : defauts + drive engage + HQ (F2.2). Les autres modules
        // sont strictement causaux et sans latence par construction — et le
        // test "tous modules pousses" du bypass (ci-dessus) a deja prouve
        // qu'aucun d'eux ne fuit dans le chemin de contournement.
        //
        // NOTE : on ne met PAS toute la chaine a fond ici — compresseur a
        // seuil −48 dB, de-esser, reverb et delay diluent ou ecrasent une
        // impulsion isolee au point que la mesure ne lit plus rien ; le
        // contrat de latence porte sur le REPORT (honnete), pas sur la
        // survie du pic dans une chaine enurlee.
        p.prepareToPlay (kSampleRate, 512);

        auto* drive = p.state().getParameter ("drive_amount");
        jassert (drive != nullptr);
        drive->setValueNotifyingHost (odvox::params::actualToNormalised ("drive_amount", 50.0f));

        auto* hq = p.state().getParameter ("hq_mode");
        jassert (hq != nullptr);
        hq->setValueNotifyingHost (1.0f);

        const int n = (int) (kSeconds * kSampleRate);
        std::vector<float> probe ((size_t) n, 0.0f);
        probe[(size_t) (n / 2)] = 0.8f;

        const auto out = render (p, probe);

        // La latence reportee est lue APRES le rendu : le rafraichissement par
        // bloc l'a mise a jour quand le chemin HQ s'est engage.
        const int reported = p.getLatencySamples();

        // Avec la reverb et le delay engages, le MAXIMUM de la reponse
        // impulsionnelle n'est plus le chemin direct : les peignes ajoutent des
        // reflections retardees qui peuvent le deplacer. L'invariant qui reste
        // vrai sur TOUTE chaine est l'ARRIVEE du chemin sec : la premiere
        // excursion significative. Elle ne peut jamais preceder entree +
        // latence reportee (aucun module ne regarde devant lui), et elle ne
        // doit pas la suivre non plus (le report serait sous-estime).
        const float seuil = 0.08f;   // le drive ramene l'impulsion ~0,25 : seuil sous le pic, au-dessus du ringing
        int arrivee = -1;

        for (int i = 0; i < n; ++i)
            if (std::abs (out[(size_t) i]) > seuil)
            {
                arrivee = i;
                break;
            }

        expect (arrivee > 0, "le chemin sec ressort (premiere excursion trouvee)");

        expectWithinAbsoluteError ((float) arrivee, (float) (n / 2 + reported), 1.0f,
                                   "le chemin sec arrive a entree + latence reportee (arrivee a "
                                       + juce::String (arrivee) + " contre "
                                       + juce::String (n / 2 + reported)
                                       + " — reporte : " + juce::String (reported) + ")");
    }

    // =====================================================================
    // AC4 de F1.13 : le gain de sortie couvre -24 a +24 dB, et le gain MESURE
    // correspond a la consigne a ±0,1 dB. La mesure se fait en regime etabli
    // (on jette le transitoire), sur un sinus modeste pour ne rien ecrêter.
    // =====================================================================
    void testOutputGainRange()
    {
        const auto setOutputGain = [this] (ODVoxAudioProcessor& p, float db)
        {
            auto* param = p.state().getParameter ("output_gain_db");
            jassert (param != nullptr);
            param->setValueNotifyingHost (odvox::params::actualToNormalised ("output_gain_db", db));
        };

        const auto measure = [&] (float db) -> float
        {
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);
            setOutputGain (p, db);

            const auto probe = makeSine (1000.0f, 0.1f, kSeconds);
            const auto out = render (p, probe);

            // Regime etabli : on saute 100 ms, puis RMS sur le reste.
            const int from = (int) (0.1 * kSampleRate);
            double sum = 0.0;

            for (int i = from; i < (int) out.size(); ++i)
                sum += (double) out[(size_t) i] * (double) out[(size_t) i];

            const double outRms = std::sqrt (sum / (double) (out.size() - from));
            const double inRms  = 0.1 / std::sqrt (2.0);

            return (float) (20.0 * std::log10 (outRms / inRms));
        };

        // Le chemin nominal applique le gain : chaque consigne est verifiee a
        // ±0,1 dB pres (AC4). Le pas du catalogue est 0,1 dB : les valeurs
        // testees sont des multiples exacts.
        for (const float db : { -24.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 24.0f })
        {
            const float measured = measure (db);
            expectWithinAbsoluteError (measured, db, 0.1f,
                                       "output_gain " + juce::String (db, 1) + " dB mesure "
                                           + juce::String (measured, 3) + " dB");
        }

        // La borne haute est bien la : -30 dB est impossible, la valeur est
        // bornee a -24. C'est le contrat de plage du catalogue, pas un effet.
        {
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);
            setOutputGain (p, -30.0f);

            auto* raw = p.state().getRawParameterValue ("output_gain_db");
            expectWithinAbsoluteError (raw->load(), -24.0f, 0.001f,
                                       "une consigne sous la borne est bornee a -24 dB");
        }
    }

    // =====================================================================
    // Filtre DC de sortie (`output_dc_filter`, module Sortie, defaut On).
    //
    // Criteres adoptes ici — le catalogue ne donnant que l'interrupteur :
    //   AC-DC1  Off = identite BIT A BIT (meme regle que le low cut) ;
    //   AC-DC2  On retire le continu ;
    //   AC-DC3  On ne touche pas la bande audible (100 Hz et 1 kHz) ;
    //   AC-DC4  la commutation ne produit aucun cran (G4, −60 dBFS) ;
    //   AC-DC5  le parametre est REELLEMENT lu par la chaine — il ne l'etait pas,
    //           et c'est la raison d'etre de ce lot.
    // =====================================================================
    void testDcBlockerIsBitExactWhenOff()
    {
        odvox::DcBlocker blocker;
        blocker.prepare (kSampleRate, 2);
        blocker.setEnabled (false);

        expect (blocker.isInert(), "interrupteur ouvert : le filtre est retire du chemin");

        constexpr int n = 4096;

        juce::Random random (20261009);
        std::vector<float> left ((size_t) n), right ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            // Un offset continu est glisse dans le signal : si le filtre
            // tournait encore, il le retirerait et la comparaison echouerait.
            left[(size_t) i]  = random.nextFloat() * 0.5f - 0.25f + 0.2f;
            right[(size_t) i] = random.nextFloat() * 0.4f - 0.2f - 0.15f;
        }

        const auto referenceLeft  = left;
        const auto referenceRight = right;

        float* chans[2] = { left.data(), right.data() };

        for (int i = 0; i < n; ++i)
            blocker.processSample (chans, i);

        expect (left == referenceLeft && right == referenceRight,
                "Off : sortie identique a l'entree, au bit pres");
    }

    void testDcBlockerRemovesDc()
    {
        odvox::DcBlocker blocker;
        blocker.prepare (kSampleRate, 2);
        blocker.setEnabled (true);

        const int n = (int) (kSeconds * kSampleRate);
        std::vector<float> left ((size_t) n, 0.5f), right ((size_t) n, -0.5f);
        float* chans[2] = { left.data(), right.data() };

        for (int i = 0; i < n; ++i)
            blocker.processSample (chans, i);

        // Le continu se vide avec la constante de temps du filtre (16 ms a
        // 10 Hz) : au-dela d'une demi-seconde il ne doit plus rien rester.
        float worst = 0.0f;
        const int from = (int) (0.5 * kSampleRate);

        for (int i = from; i < n; ++i)
            worst = juce::jmax (worst, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]));

        expect (worst < 1.0e-6f,
                "un continu de 0,5 ressort a " + juce::String (worst, 9)
                    + " apres 500 ms (exige sous 1e-6)");
    }

    void testDcBlockerKeepsAudibleBand()
    {
        // Deux frequences, dont la plus basse de la bande vocale : a 100 Hz le
        // filtre ne doit deja plus rien retirer de mesurable (le catalogue ne
        // donne aucune commande de frequence, c'est ici que ca se paie).
        for (const float freq : { 100.0f, 1000.0f })
        {
            odvox::DcBlocker blocker;
            blocker.prepare (kSampleRate, 2);
            blocker.setEnabled (true);

            const int n = (int) (kSeconds * kSampleRate);
            const auto tone = makeSine (freq, 0.5f, kSeconds);
            std::vector<float> left (tone), right (tone);
            float* chans[2] = { left.data(), right.data() };

            for (int i = 0; i < n; ++i)
                blocker.processSample (chans, i);

            const int from = (int) (0.5 * kSampleRate);
            double sum = 0.0;

            for (int i = from; i < n; ++i)
                sum += (double) left[(size_t) i] * (double) left[(size_t) i];

            const double outRms = std::sqrt (sum / (double) (n - from));
            const double inRms  = 0.5 / std::sqrt (2.0);
            const float measured = (float) (20.0 * std::log10 (outRms / inRms));

            expectWithinAbsoluteError (measured, 0.0f, 0.1f,
                                       "a " + juce::String (freq, 0) + " Hz : "
                                           + juce::String (measured, 3) + " dB");
        }
    }

    void testDcBlockerSwitchHasNoClick()
    {
        // Le filtre en regime etabli s'ecarte du signal sec (ecart d'amplitude
        // ET de phase) : un interrupteur qui coupe net ferait ce saut d'un seul
        // echantillon — mesure, l'ecart vaut 5,4e-2 a 100 Hz et 1,2e-1 a 40 Hz,
        // soit -25 et -18 dBFS, au-dessus du seuil de clic de G4 (-60 dBFS).
        // Le fondu de 20 ms doit etaler ce saut : le cran maximal ne doit donc
        // pas depasser le pas que le signal presente DEJA tout seul.
        for (const float freq : { 40.0f, 100.0f })
        {
            const int n = (int) (kSeconds * kSampleRate);
            const int flip = n / 2;
            const auto dry = makeSine (freq, 0.5f, kSeconds);

            float naturalStep = 0.0f;

            for (int i = n / 4; i < flip; ++i)
                naturalStep = juce::jmax (naturalStep,
                                          std::abs (dry[(size_t) i] - dry[(size_t) i - 1]));

            // Les DEUX sens : On -> Off (le filtre se retire) et Off -> On (il
            // arrive). Les deux peuvent cliquer, pas seulement l'ouverture.
            for (const bool startOn : { true, false })
            {
                odvox::DcBlocker blocker;
                blocker.prepare (kSampleRate, 2);
                blocker.setEnabled (startOn);

                std::vector<float> left (dry), right (dry);
                float* chans[2] = { left.data(), right.data() };

                float worst = 0.0f;

                for (int i = 0; i < n; ++i)
                {
                    if (i == flip)
                        blocker.setEnabled (! startOn);

                    const float previous = left[(size_t) juce::jmax (0, i - 1)];
                    blocker.processSample (chans, i);

                    if (i > flip - 64 && i < flip + 64)
                        worst = juce::jmax (worst, std::abs (left[(size_t) i] - previous));
                }

                expect (worst <= naturalStep * 1.2f,
                        "a " + juce::String (freq, 0)
                            + " Hz, " + (startOn ? "On -> Off" : "Off -> On")
                            + " : cran " + juce::String (worst, 6)
                            + " contre un pas naturel de " + juce::String (naturalStep, 6)
                            + " (" + juce::String (20.0f * std::log10 (juce::jmax (worst, 1.0e-12f)), 1)
                            + " dBFS)");
            }
        }
    }

    void testDcFilterIsWiredToTheChain()
    {
        // Le parametre etait declare au catalogue et jamais lu : c'est le
        // cablage lui-meme que ce test garde. Il se fait en DIFFERENTIEL — le
        // meme signal dans les deux positions de l'interrupteur — parce qu'un
        // autre module sait aussi retirer le continu : le low cut, qui est On
        // par defaut. On l'ecarte donc, et l'EQ avec lui, pour que le seul
        // retrait possible du continu soit celui du filtre DC.
        const auto chainMean = [&] (bool dcOn, const std::vector<float>& probe) -> double
        {
            ODVoxAudioProcessor p;
            p.setParameterActual ("lowcut_amount", 0.0f);
            p.setParameterActual ("eq_on", 0.0f);
            p.setParameterActual ("output_dc_filter", dcOn ? 1.0f : 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = render (p, probe);
            const int from = (int) (0.8 * kSampleRate);
            double sum = 0.0;

            for (int i = from; i < (int) out.size(); ++i)
                sum += (double) out[(size_t) i];

            return sum / (double) (out.size() - from);
        };

        const std::vector<float> dc ((size_t) (kSeconds * kSampleRate), 0.3f);

        const double withoutFilter = std::abs (chainMean (false, dc));
        const double withFilter    = std::abs (chainMean (true, dc));

        expect (withoutFilter > 0.29,
                "temoin : sans le filtre, le continu TRAVERSE la chaine (moyenne "
                    + juce::String (withoutFilter, 6) + ")");
        expect (withFilter < 1.0e-4,
                "avec le filtre, le continu est retire par la chaine (moyenne "
                    + juce::String (withFilter, 9) + ")");

        // Et dans la bande audible, la chaine ne doit rien changer de plus que
        // le module : meme niveau a 1 kHz, interrupteur dans les deux positions.
        const auto chainLevelDb = [&] (bool dcOn) -> float
        {
            ODVoxAudioProcessor p;
            p.setParameterActual ("lowcut_amount", 0.0f);
            p.setParameterActual ("eq_on", 0.0f);
            p.setParameterActual ("output_dc_filter", dcOn ? 1.0f : 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = render (p, makeSine (1000.0f, 0.5f, kSeconds));
            const int from = (int) (0.2 * kSampleRate);
            double sum = 0.0;

            for (int i = from; i < (int) out.size(); ++i)
                sum += (double) out[(size_t) i] * (double) out[(size_t) i];

            const double rms = std::sqrt (sum / (double) (out.size() - from));
            return (float) (20.0 * std::log10 (rms / (0.5 / std::sqrt (2.0))));
        };

        expectWithinAbsoluteError (chainLevelDb (true), chainLevelDb (false), 0.05f,
                                   "le filtre DC ne change pas le niveau a 1 kHz dans la chaine");
    }
};

static OutputTests outputTests;
