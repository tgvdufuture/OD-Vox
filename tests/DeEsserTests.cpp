#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "DeEsser.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    // Les filtres du de-esser ont un transitoire de demarrage (un passe-haut
    // lache une pointe avant de s'etablir). On mesure donc en REGIME ETABLI :
    // les 50 premiers ms sont ecartes de toute mesure de niveau.
    constexpr int kSkip = (int) (0.050 * kSampleRate);

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }

    /** Bascule un parametre par sa valeur reelle (le processeur convertit). */
    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    juce::MidiBuffer noMidi;

    /** Rend un signal a travers un processeur neuf, et retourne le canal 0. */
    std::vector<float> render (ODVoxAudioProcessor& p, const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buffer (2, (int) mono.size());
        buffer.clear();

        for (size_t i = 0; i < mono.size(); ++i)
            buffer.setSample (0, (int) i, mono[i]);

        p.processBlock (buffer, noMidi);

        std::vector<float> out (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        return out;
    }

    /** Amplitude du composant `probeHz` en regime etabli, par correlation I/Q.

        Bien plus fiable qu'une crete : le transitoire de demarrage des filtres
        domine la crete sans rien dire du comportement etabli, et sur un melange
        voix + sifflante la crete appartient a qui veut bien la prendre. Ici on
        mesure EXACTEMENT la bande qui nous interesse. */
    float toneAmplitude (const std::vector<float>& signal, float probeHz)
    {
        const int n = (int) signal.size();
        double re = 0.0, im = 0.0;

        for (int i = kSkip; i < n; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi
                              * (double) probeHz * (double) i / kSampleRate;
            re += (double) signal[(size_t) i] * std::cos (ph);
            im += (double) signal[(size_t) i] * std::sin (ph);
        }

        const int count = juce::jmax (1, n - kSkip);
        return (float) (2.0 * std::sqrt (re * re + im * im) / (double) count);
    }

    float steadyPeak (const std::vector<float>& signal)
    {
        float peak = 0.0f;
        for (size_t i = (size_t) kSkip; i < signal.size(); ++i)
            peak = juce::jmax (peak, std::abs (signal[i]));

        return peak;
    }

    /** Une configuration complete du module (UN SEUL curseur), pour comparer
        deux passes. */
    struct Cfg
    {
        float amount = 0.0f;
    };

    /** Niveau du composant `probeHz` du signal apres traitement, processeur neuf.

        La correlation isole la raie EXACTE meme au milieu d'un melange : c'est
        ce qui permet de mesurer une bande sans que l'autre ne contamine la
        mesure (indispensable pour AC1). */
    float levelOf (const std::vector<float>& signal, float probeHz, const Cfg& c)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "deess_amount", c.amount);

        return toneAmplitude (render (p, signal), probeHz);
    }

    /** Ecart (dB, positif = attenue) du composant `probeHz` entre deux passes. */
    float deltaDb (const std::vector<float>& signal, float probeHz,
                   const Cfg& dry, const Cfg& wet)
    {
        return -20.0f * std::log10 (juce::jmax (levelOf (signal, probeHz, wet), 1.0e-9f)
                                    / juce::jmax (levelOf (signal, probeHz, dry), 1.0e-9f));
    }

    /** Le signal d'AC1 : un « S » a 7 kHz ET du grave de plosive a 100 Hz.

        Les deux doivent etre PRESENTS ensemble : sur une sonde grave seule, le
        de-esser ne reduit rien (aucune energie dans sa bande de detection) et
        une fuite de son gain sur la bande grave resterait invisible — un test
        vacant. */
    std::vector<float> sibAndPlosive()
    {
        // La sifflante DOMINE franchement la voix (1,0 contre 0,2) : c'est la
        // condition pour que le de-esser ait quelque chose a dompter (sa
        // reference adaptative compare les deux bandes ; un « S » au niveau de
        // la voix, ou en dessous, n'est pas une sifflante a reduire — et le
        // test ne mesurerait alors rien du tout).
        auto mix = makeSine (7000.0f, 1.0f, 0.5);
        const auto low = makeSine (100.0f, 0.2f, 0.5);

        for (size_t i = 0; i < mix.size(); ++i)
            mix[i] += low[i];

        return mix;
    }

    // =====================================================================
    // AC1 (revu 2026-09-21 : plosives LIES au curseur) — les deux bandes
    // restent INDEPENDANTES dans le traitement : la reduction dynamique
    // n'agit que sur la voie haute, le shelf de plosives que sur la voie
    // basse. Sur la bande grave, seul le shelf lie agit — d'un montant BORNE,
    // jamais la reduction dynamique.
    // =====================================================================
    void testIndependenceOfBands (juce::UnitTest& t)
    {
        const auto signal = sibAndPlosive();

        const Cfg dry;                 // aucun traitement
        const Cfg aFond { 100.0f };    // de-ess + plosive liee (cap 20 %)

        // La reduction dynamique agit sur SA bande...
        const float sibOn = deltaDb (signal, 7000.0f, dry, aFond);
        t.expect (sibOn > 0.5f,
                  "AC1 : le de-ess reduit la bande sifflante 7 kHz ("
                  + juce::String (sibOn, 2) + " dB)");

        // ...et sur la bande grave, SEUL le shelf lie agit (-1,2 dB a
        // plosive01 = 0,2) — borne, jamais la reduction dynamique.
        const float plosOn = deltaDb (signal, 100.0f, dry, aFond);
        t.expect (plosOn > 0.5f && plosOn < 2.5f,
                  "AC1 : la bande grave ne voit que le shelf lie ("
                  + juce::String (plosOn, 2) + " dB, borne 0,5..2,5)");
    }

    // =====================================================================
    // Plosives LIES au curseur (pre-reglage interne) : depth = 0,6 x amount,
    // plafonnee au cap 20 % des presets d'usine (atteint des ~33 %).
    // =====================================================================
    void testPlosivesLiesAuCurseur (juce::UnitTest& t)
    {
        const auto grave = makeSine (100.0f, 0.4f, 0.5);

        // amount 20 % : plosive01 = 0,12 -> shelf -0,72 dB.
        const Cfg faible { 20.0f };
        const float p1 = deltaDb (grave, 100.0f, Cfg {}, faible);
        t.expect (p1 > 0.4f && p1 < 1.1f,
                  "a 20 %, le shelf lie vaut ~-0,7 dB ("
                  + juce::String (p1, 2) + " dB)");

        // amount 100 % : plosive01 = cap 0,20 -> shelf -1,2 dB (pas plus).
        const Cfg aFond { 100.0f };
        const float p2 = deltaDb (grave, 100.0f, Cfg {}, aFond);
        t.expect (p2 > 0.8f && p2 < 1.6f,
                  "a 100 %, le shelf lie est plafonne a -1,2 dB ("
                  + juce::String (p2, 2) + " dB)");
    }

    // =====================================================================
    // AC3 — voix continue sans sifflante : le niveau total bouge de < 0,5 dB.
    // =====================================================================
    void testVoiceUntouched (juce::UnitTest& t)
    {
        // Voix : 220 Hz + harmoniques, rien au-dessus du croisement.
        std::vector<float> voice;
        for (double sec = 0.0; sec < 1.5; sec += 1.0 / kSampleRate)
            voice.push_back (0.25f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * sec)
                                              + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * sec)
                                              + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 880.0 * sec)));

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        // L'EQ par defaut porte +2,5 dB d'Air : neutralise pour isoler le module.
        setActual (p, "eq_on", 0.0f);

        setActual (p, "deess_amount", 0.0f);
        const float clean = steadyPeak (render (p, voice));

        setActual (p, "deess_amount", 100.0f);
        const float processed = steadyPeak (render (p, voice));

        const float changeDb = -20.0f * std::log10 (processed / clean);
        t.expect (std::abs (changeDb) < 0.5f,
                  "AC3 : voix continue a 100 %, niveau total intact ("
                  + juce::String (changeDb, 3) + " dB)");

        // Et le vumetre ne s'allume pas sur une voix sans sifflante : la
        // detection adaptative ne reduit pas dans le vide.
        t.expect (p.lastDeEssReductionDb() < 0.1f,
                  "AC3 : aucune reduction a vide sur une voix sans sifflante ("
                  + juce::String (p.lastDeEssReductionDb(), 3) + " dB)");
    }

    // =====================================================================
    // Fidelite a la cible : plage utile etroite, saturation precoce
    // (−3,12 dB a 50 %, −3,64 dB a 100 % chez elle), et vumetre honnete.
    //
    // Sonde FRANCHE au-dessus du croisement (9 kHz pour 5 kHz) : c'est la seule
    // condition ou la reduction MESUREE vaut le gain applique a la voie haute
    // (0,25 dB d'ecart), donc ou le vumetre est comparable a une mesure.
    // =====================================================================
    void measureWithMeter (float probeHz, float amountPct,
                           float& measuredDb, float& meterDb)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "deess_amount", 0.0f);
        const auto clean = render (p, makeSine (probeHz, 0.5f, 0.5));

        setActual (p, "deess_amount", amountPct);
        const auto processed = render (p, makeSine (probeHz, 0.5f, 0.5));

        measuredDb = -20.0f * std::log10 (juce::jmax (toneAmplitude (processed, probeHz), 1.0e-9f)
                                          / juce::jmax (toneAmplitude (clean, probeHz), 1.0e-9f));
        meterDb = p.lastDeEssReductionDb();
    }

    void testCeilingFidelity (juce::UnitTest& t)
    {
        float measured50 = 0.0f, meter50 = 0.0f;
        float measured100 = 0.0f, meter100 = 0.0f;
        measureWithMeter (9000.0f,  50.0f, measured50,  meter50);
        measureWithMeter (9000.0f, 100.0f, measured100, meter100);

        t.expect (std::abs (measured50 - 3.1f) < 1.0f,
                  "50 % reduit ~3,1 dB comme la cible ("
                  + juce::String (measured50, 2) + " dB)");
        t.expect (std::abs (measured100 - 3.6f) < 1.0f,
                  "100 % reduit ~3,6 dB comme la cible ("
                  + juce::String (measured100, 2) + " dB)");

        // Saturation precoce : la seconde moitie du curseur ne gagne presque
        // rien (chez elle, +0,5 dB entre 50 et 100 %).
        t.expect (measured100 - measured50 < 1.0f,
                  "la saturation est precoce ("
                  + juce::String (measured100 - measured50, 2) + " dB entre 50 et 100 %)");

        // Le vumetre dit la reduction de la bande detectee, pas une crete
        // contaminee par les autres bandes.
        t.expect (std::abs (meter100 - measured100) < 0.5f,
                  "le vumetre colle a la reduction appliquee (vumetre "
                  + juce::String (meter100, 2) + " dB vs mesure "
                  + juce::String (measured100, 2) + " dB)");
    }

    // =====================================================================
    // Cas limite du PRD : bande de detection vide => aucune reduction.
    // =====================================================================
    void testNoReductionInVacuum (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "deess_amount", 100.0f);

        // Silence total : rien a detecter, rien a reduire, sortie muette.
        const std::vector<float> silence ((size_t) (0.3 * kSampleRate), 0.0f);
        const auto out = render (p, silence);

        bool allZero = true;
        for (float s : out)
            allZero = allZero && (std::abs (s) < 1.0e-9f);

        t.expect (allZero, "cas limite : un silence traverse le module sans rien creer");
        t.expect (p.lastDeEssReductionDb() < 0.01f,
                  "cas limite : bande de detection vide, aucune reduction ("
                  + juce::String (p.lastDeEssReductionDb(), 4) + " dB)");
    }

    // =====================================================================
    // Transparence bit-exacte aux defauts du catalogue (AC2 de US-02) :
    // `deess_amount` a 0 % (la plosive liee vaut zero aussi), donc le module
    // est court-circuite.
    //
    // L'EQ est neutralise : il n'est PAS neutre aux defauts (`eq_air_db` vaut
    // +2,5 dB, la valeur retenue, et §3.4 du PRD la veut par
    // defaut). Le contrat de transparence de US-02 AC2 porte sur les curseurs
    // d'INTENSITE, pas sur les reglages tonaux — pour isoler le de-esser, on
    // neutralise l'EQ d'abord.
    // =====================================================================
    void testZeroAmountIsBitExact (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;

        setActual (p, "deess_amount", 0.0f);
        setActual (p, "eq_on", 0.0f);
        // Low cut ON par defaut depuis l'amendement UI du 2026-09-29 (reglage
        // tonal, hors du contrat de transparence des curseurs) : on l'eteint
        // pour isoler le de-esser. Le filtre DC de sortie est de la meme
        // famille (interrupteur de nettoyage, On par defaut, 2026-10-09).
        setActual (p, "lowcut_amount", 0.0f);
        setActual (p, "output_dc_filter", 0.0f);

        // prepareToPlay APRES les reglages : il amorce le lissage sur les valeurs
        // courantes, sans quoi le premier bloc mesurerait la rampe et non l'etat.
        p.prepareToPlay (kSampleRate, 512);

        const auto mix = makeSine (8000.0f, 0.4f, 0.2);

        // Comparaison a l'ENTREE, et non a une seconde passe du meme processeur :
        // deux passes ne sont identiques que si la chaine est sans memoire, ce qui
        // n'est plus vrai des qu'un module filtrant est actif. La question posee
        // ici est celle de l'utilisateur — « le module touche-t-il le signal ? ».
        juce::AudioBuffer<float> buffer (2, (int) mix.size());
        for (size_t i = 0; i < mix.size(); ++i)
            buffer.setSample (0, (int) i, mix[i]);

        juce::AudioBuffer<float> entree (buffer);
        p.processBlock (buffer, noMidi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                t.expect (buffer.getSample (ch, i) == entree.getSample (ch, i),
                          "Transparence bit-exacte aux defauts du de-esser");
    }

    // =====================================================================
    // Reset : le vumetre repart de zero (les passes ne se contaminent pas).
    // =====================================================================
    void testResetClearsMeter (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "deess_amount", 100.0f);
        render (p, makeSine (8000.0f, 0.5f, 0.3));
        t.expect (p.lastDeEssReductionDb() > 0.5f, "Une sifflante allume le vumetre");

        p.reset();
        t.expect (p.lastDeEssReductionDb() == 0.0f, "reset() remet le vumetre a zero");
    }
}

class DeEsserTests : public juce::UnitTest
{
public:
    DeEsserTests() : juce::UnitTest ("DeEsser") {}

    void runTest() override
    {
        beginTest ("AC1 : bandes independantes, plosives lies au curseur");
        testIndependenceOfBands (*this);

        beginTest ("Plosives lies au curseur (cap 20 %)");
        testPlosivesLiesAuCurseur (*this);

        beginTest ("AC3 : voix continue intacte");
        testVoiceUntouched (*this);

        beginTest ("Plage utile et saturation de la cible");
        testCeilingFidelity (*this);

        beginTest ("Cas limite : aucune reduction a vide");
        testNoReductionInVacuum (*this);

        beginTest ("Transparence bit-exacte a 0 %");
        testZeroAmountIsBitExact (*this);

        beginTest ("Reset");
        testResetClearsMeter (*this);
    }
};

static DeEsserTests deEsserTests;
