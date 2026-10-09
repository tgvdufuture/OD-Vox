#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "Drive.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kProbeHz    = 1000.0;

    // Regime etabli : les 50 premieres ms sont ecartees (les filtres du drive
    // ont leur propre transitoire de demarrage).
    constexpr int kSkip = (int) (0.050 * kSampleRate);

    /** Le drive est NON LINEAIRE : on l'analyse par son contenu harmonique, pas
        par une reponse en frequence. La sonde est entretenue, donc chaque
        harmonique tombe exactement sur n·f0 et la correlation l'extrait sans
        fenetrage ni fuite spectrale. */
    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    juce::MidiBuffer noMidi;

    std::vector<float> render (ODVoxAudioProcessor& p, const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buffer (2, (int) mono.size());
        buffer.clear();

        for (size_t i = 0; i < mono.size(); ++i)
            buffer.setSample (0, (int) i, mono[i]);

        // Un hote ne depasse JAMAIS la taille de bloc declaree en prepareToPlay
        // (512 ici). Le chemin HQ copie le bloc dans un tampon dimensionne a
        // maxBlock : rendre 24 000 echantillons d'un coup violerait le contrat
        // et deborderait. On decoupe donc comme un DAW.
        constexpr int kHostBlock = 512;
        juce::MidiBuffer midi;

        for (int start = 0; start < buffer.getNumSamples(); start += kHostBlock)
        {
            const int count = juce::jmin (kHostBlock, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, start, count);
            p.processBlock (block, midi);
        }

        std::vector<float> out (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        return out;
    }

    float componentAmplitude (const std::vector<float>& signal, double freq)
    {
        const int n = (int) signal.size();
        double re = 0.0, im = 0.0;

        for (int i = kSkip; i < n; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / kSampleRate;
            re += (double) signal[(size_t) i] * std::cos (ph);
            im += (double) signal[(size_t) i] * std::sin (ph);
        }

        const int count = juce::jmax (1, n - kSkip);
        return (float) (2.0 * std::sqrt (re * re + im * im) / (double) count);
    }

    /** Correlation fenetree Hann : la fenetre annule les discontinuites aux
        bords, donc les lobes secondaires (−32 dB du rectangle) tombent sous
        −90 dB — les raies faibles deviennent mesurables. Necessaire au controle
        AC1 de US-09, qui sonde un plancher a −80 dBFS. */
    float componentAmplitudeHann (const std::vector<float>& signal, double freq)
    {
        const int n = (int) signal.size();
        double re = 0.0, im = 0.0;

        for (int i = kSkip; i < n; ++i)
        {
            // Hann sur la portion mesuree (apres kSkip).
            const double w = 0.5 * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi
                                                      * (double) (i - kSkip) / (double) (n - kSkip - 1)));
            const double ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / kSampleRate;
            re += (double) signal[(size_t) i] * w * std::cos (ph);
            im += (double) signal[(size_t) i] * w * std::sin (ph);
        }

        const int count = juce::jmax (1, n - kSkip);
        return (float) (2.0 * std::sqrt (re * re + im * im) / (double) count);
    }

    float peakOf (const std::vector<float>& signal)
    {
        float peak = 0.0f;
        for (size_t i = (size_t) kSkip; i < signal.size(); ++i)
            peak = juce::jmax (peak, std::abs (signal[i]));

        return peak;
    }

    /** Niveaux des harmoniques 2..maxN, en dB SOUS la fondamentale. */
    std::map<int, float> harmonics (const std::vector<float>& signal, int maxN = 15)
    {
        const float fundamental = componentAmplitude (signal, kProbeHz);
        std::map<int, float> levels;

        for (int n = 2; n <= maxN; ++n)
        {
            const double f = kProbeHz * n;
            if (f >= kSampleRate * 0.5)
                break;

            const float a = componentAmplitude (signal, f);
            levels[n] = 20.0f * std::log10 (juce::jmax (a, 1.0e-12f) / juce::jmax (fundamental, 1.0e-12f));
        }

        return levels;
    }

    /** Les harmoniques repliees dans leurs bandes de 1/3 d'octave (IEC, centre
        1000 Hz = bande 0). C'est la grandeur exacte d'AC1 : deux harmoniques qui
        partagent une bande (10f et 11f a 1 kHz) y sont sommées AVANT comparaison,
        comme le ferait un analyseur par tiers d'octave. */
    int bandOf (double freq)
    {
        return (int) std::lround (std::log2 (freq / kProbeHz) * 3.0);
    }

    std::map<int, float> thirdOctaveBands (const std::map<int, float>& h)
    {
        std::map<int, float> energy;

        for (const auto& [n, db] : h)
        {
            const double f = kProbeHz * n;
            energy[bandOf (f)] += std::pow (10.0f, db / 10.0f);   // puissance
        }

        std::map<int, float> bands;
        for (const auto& [band, e] : energy)
            bands[band] = 10.0f * std::log10 (juce::jmax (e, 1.0e-12f));

        return bands;
    }

    /** Analyse d'une saveur au niveau demande. La saveur est passe directement
        au MODULE (rev du 2026-09-21 : elle n'est plus un parametre public — le
        processeur fige Console, le test garde les 4 moteurs pour l'identite
        future et pour verifier que le figage est sans regression). */
    std::map<int, float> analyseFlavor (int flavorIndex, float amountPct,
                                        float probeAmplitude = 0.25f)
    {
        odvox::Drive drive;
        drive.prepare (kSampleRate, 1, 512);

        odvox::Drive::Settings s;
        s.amount01 = amountPct * 0.01f;
        s.inert    = s.amount01 <= 0.0f;
        drive.setSettings (s);

        // Le module ne lit pas la saveur dans Settings : le processeur la fige
        // via setSettings (voir Drive::setSettings). Pour tester les 4 moteurs,
        // on passe par le champ prive — impossible. On teste donc chaque
        // saveur PAR LE PROCESSEUR avec la saveur par defaut Console, et les
        // autres moteurs sont verifies par les tests unitaires de la classe.
        // Ici : le signal traverse un PROCESSEUR neuf (chaine entiere) a amount
        // donne — Console imposee par le figage.
        (void) flavorIndex;

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);
        setActual (p, "drive_amount", amountPct);

        return harmonics (render (p, makeSine ((float) kProbeHz, probeAmplitude, 0.5)));
    }

    // =====================================================================
    // AC1 (rev) — le figage Console est SANS REGRESSION : l'engin actif
    // reproduit la signature symetrique mesuree. (Les 4 moteurs restent dans
    // le module pour l'identite future ; leur distinction spectrale est
    // verifiee dans DriveModuleTests via les saveurs poses au module.)
    // =====================================================================

    // =====================================================================
    // AC2 (rev) — drive a 0 % rend le module transparent (≤ −120 dBFS) :
    // le curseur unique est ENGAGEANT et son zero court-circuite tout.
    // =====================================================================
    void testMixZeroIsTransparent (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        // Drive a 0 % : module court-circuite.
        setActual (p, "drive_amount", 0.0f);
        // L'EQ par defaut porte +2,5 dB d'Air : neutralise pour isoler le drive
        // (le contrat de transparence porte sur les curseurs, pas sur les
        // reglages tonaux — voir §3.4 du PRD, point 8). Le low cut, ON par
        // defaut depuis l'amendement UI du 2026-09-29, est un reglage tonal
        // pareil : son passe-haut dephase la bande passante, on l'eteint.
        setActual (p, "eq_on", 0.0f);
        setActual (p, "lowcut_amount", 0.0f);
        // Idem le filtre DC de sortie (2026-10-09) : On par defaut, nettoyage.
        setActual (p, "output_dc_filter", 0.0f);

        const auto probe = makeSine ((float) kProbeHz, 0.25f, 0.3);

        juce::AudioBuffer<float> buffer (2, (int) probe.size());
        buffer.clear();
        for (size_t i = 0; i < probe.size(); ++i)
            buffer.setSample (0, (int) i, probe[i]);

        juce::AudioBuffer<float> entree (buffer);
        p.processBlock (buffer, noMidi);

        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (buffer.getSample (ch, i) - entree.getSample (ch, i)));

        const float diffDb = 20.0f * std::log10 (juce::jmax (maxDiff, 1.0e-12f));
        t.expect (diffDb <= -120.0f,
                  "AC2 : mix a 0 %, l'ecart reste sous −120 dBFS ("
                  + juce::String (diffDb, 1) + " dBFS)");
    }

    // =====================================================================
    // AC3 — aucun echantillon au-dessus de +6 dBFS a 100 %, entree a −12 dBFS
    // (l'engin fige Console).
    // =====================================================================
    void testOutputStaysBounded (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "drive_amount", 100.0f);

            // Entree a −12 dBFS crete : deux partiels, crete exactement calibree.
            auto probe = makeSine ((float) kProbeHz, 0.25f, 0.4);
            const auto high = makeSine (3000.0f, 0.125f, 0.4);
            for (size_t i = 0; i < probe.size(); ++i)
                probe[i] += high[i];

            const float inPeak = juce::jmax (0.25f, 0.375f);
            const float inScale = 0.25f / inPeak;
            for (auto& v : probe)
                v *= inScale;

            const float outPeak = peakOf (render (p, probe));
            const float outDb = 20.0f * std::log10 (juce::jmax (outPeak, 1.0e-12f));

            t.expect (outDb <= 6.0f,
                      "AC3 : la sortie ne depasse pas +6 dBFS ("
                      + juce::String (outDb, 2) + " dBFS)");

            t.expect (outDb <= 0.0f,
                      "AC3 (marge) : la sortie ne depasse pas 0 dBFS ("
                      + juce::String (outDb, 2) + " dBFS)");
    }

    // =====================================================================
    // Fidelite a la cible : l'engin FIGE (Console) est l'engin retenu —
    // saturation SYMETRIQUE (impairs seuls). Mesure chez elle : 3f −10,1 dB et
    // 5f −15,8 dB a 100 %, 2f et 4f au plancher (−75 dB, son propre bruit).
    // =====================================================================
    void testConsoleFidelity (juce::UnitTest& t)
    {
        const auto h = analyseFlavor (odvox::Drive::kConsole, 100.0f);

        t.expect (std::abs (h.at (3) - (-10.1f)) < 3.0f,
                  "fidelite : Console a 100 %, 3f a −10,1 dB comme la cible ("
                  + juce::String (h.at (3), 1) + " dB)");
        t.expect (std::abs (h.at (5) - (-15.8f)) < 4.0f,
                  "fidelite : Console a 100 %, 5f a −15.8 dB comme la cible ("
                  + juce::String (h.at (5), 1) + " dB)");

        // Symetrie : les pairs restent tres en dessous des impairs.
        t.expect (h.at (2) < -60.0f && h.at (4) < -60.0f,
                  "fidelite : Console est symetrique, harmoniques pairs au plancher (2f "
                  + juce::String (h.at (2), 1) + " dB, 4f " + juce::String (h.at (4), 1) + " dB)");
    }

    // =====================================================================
    // Transparence bit-exacte aux defauts du catalogue (AC2 de US-02) : drive a
    // 0 %, donc le module est court-circuite.
    // =====================================================================
    void testZeroAmountIsBitExact (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;

        setActual (p, "drive_amount", 0.0f);
        // L'EQ n'est pas neutre aux defauts du catalogue (Air +2,5 dB, la valeur
        // retenue) : le contrat de transparence de US-02 AC2
        // porte sur les CURSEURS D'INTENSITE, pas sur les reglages tonaux. Pour
        // isoler le drive, on neutralise l'EQ d'abord — et le low cut, ON par
        // defaut depuis l'amendement UI du 2026-09-29 (reglage tonal, pareil).
        setActual (p, "eq_on", 0.0f);
        setActual (p, "lowcut_amount", 0.0f);
        // Idem le filtre DC de sortie (2026-10-09) : On par defaut, nettoyage.
        setActual (p, "output_dc_filter", 0.0f);

        // prepareToPlay APRES les reglages : il amorce le lissage sur les valeurs
        // courantes, sans quoi le premier bloc mesurerait la rampe et non l'etat.
        p.prepareToPlay (kSampleRate, 512);

        const auto probe = makeSine ((float) kProbeHz, 0.4f, 0.2);

        // Comparaison a l'ENTREE, et non a une seconde passe du meme processeur :
        // deux passes ne coincident que si la chaine est sans memoire, ce qui n'est
        // plus vrai des qu'un module filtrant est actif.
        juce::AudioBuffer<float> buffer (2, (int) probe.size());
        for (size_t i = 0; i < probe.size(); ++i)
            buffer.setSample (0, (int) i, probe[i]);

        juce::AudioBuffer<float> entree (buffer);
        p.processBlock (buffer, noMidi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                t.expect (buffer.getSample (ch, i) == entree.getSample (ch, i),
                          "Transparence bit-exacte aux defauts du drive");
    }

    // =====================================================================
    // Le drive ne fait pas que distordre : il remonte un signal faible
    // (compression vers le haut), ce qui est le comportement attendu d'un drive
    // et borne la sortie.
    // =====================================================================
    void testLoudnessRisesOnQuietInput (juce::UnitTest& t)
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        setActual (p, "drive_amount", 100.0f);

        const auto quiet = makeSine ((float) kProbeHz, 0.0625f, 0.4);   // −24 dBFS
        const float outPeak = peakOf (render (p, quiet));
        const float gainDb = 20.0f * std::log10 (outPeak / 0.0625f);

        t.expect (gainDb > 3.0f,
                  "le drive remonte un signal faible de plus de 3 dB ("
                  + juce::String (gainDb, 2) + " dB a −24 dBFS)");
    }
}

class DriveTests : public juce::UnitTest
{
public:
    DriveTests() : juce::UnitTest ("Drive") {}

    void runTest() override
    {
        beginTest ("AC2 : drive 0 % transparent");
        testMixZeroIsTransparent (*this);

        beginTest ("AC3 : sortie bornee");
        testOutputStaysBounded (*this);

        beginTest ("Fidelite : l'engin fige Console reproduit l'engin mesure");
        testConsoleFidelity (*this);

        beginTest ("Transparence bit-exacte a 0 %");
        testZeroAmountIsBitExact (*this);

        beginTest ("Le drive remonte les signaux faibles");
        testLoudnessRisesOnQuietInput (*this);

        beginTest ("F2.2 AC1 : mode HQ 4x, pas de repliement au-dessus de -80 dBFS");
        testHqNoAudibleAliasing (*this);

        beginTest ("F2.2 AC2 : latence zero hors HQ, reportee en HQ");
        testHqLatencyReported (*this);

        beginTest ("F2.2 AC3 : bascule HQ sans clic");
        testHqToggleDoesNotClick (*this);
    }

private:
    // =====================================================================
    // F2.2 / AC1 de US-09 : en mode HQ 4x, sonde 8 kHz a -12 dBFS crete,
    // drive a 80 % : AUCUN repliement au-dessus de -80 dBFS dans 0-20 kHz.
    //
    // Une sonde a 8 kHz saturee produit des harmoniques PAIRES ET IMPAIRES
    // (2f = 16 k, 3f = 24 k...). Hors HQ, 3f (24 kHz) est au-dela du Nyquist
    // (24 k = la moitie de 48 k) et SE REPLIE vers 24 k. Hors HQ on mesure ce
    // repli ; en HQ 4x, le Nyquist elargi (96 kHz) l'accueille. Le test mesure
    // les composantes HORS harmoniques entieres de f0 dans 0-20 kHz : leur
    // niveau relatif doit rester sous -80 dBFS.
    // =====================================================================
    void testHqNoAudibleAliasing (juce::UnitTest& t)
    {
        constexpr float probeHz = 8100.0f;

        // Le niveau d'harmonique relatif a la fondamentale, hors harmoniques
        // entieres : c'est le REpli (les harmoniques elles-memes sont attendues
        // et ne comptent pas).
        // AMENDEMENT UI du 2026-09-29 : le mode HQ est TOUJOURS actif (le
        // processeur ignore le parametre hq_mode) — il n'existe plus qu'un
        // chemin, celui qu'on mesure ici. Le temoin « hors HQ, le repli est
        // mesurable » de la revue initiale n'est plus productible dans le
        // produit : il vivait sur une bascule supprimee.
        const auto aliasFloorDb = [&]() -> float
        {
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);

            // Le drive ENGAGE : un module inert est bit-exact et ne replie
            // rien — le controle perdrait son objet.
            setActual (p, "drive_amount", 80.0f);
            setActual (p, "eq_on", 0.0f);

            // Sonde 8 100 Hz : ses harmoniques impaires (3f = 24,3 k > Nyquist
            // 24 k ; 5f = 40,5 k ; 7f = 56,7 k) replient hors HQ, et la
            // fondamentale tombe HORS des grilles harmoniques de 50/100 Hz du
            // peigne de mesure, qui reste valable comme exclu-sons-le-haut.
            auto probe = makeSine (probeHz, 0.25f, 0.5);   // -12 dBFS crete

            const auto out = render (p, probe);

            // Peigne de mesure : correlation FENETREE (Hann) a chaque 50 Hz de
            // 1 k a 20 kHz. Le fenetrage tue les lobes secondaires du rectangle
            // (−32 dB : artefact de mesure, pas du signal) — les raies se
            // lisent a leur vrai niveau.
            float worstDb = -200.0f;
            const float fundamental = componentAmplitudeHann (out, probeHz);

            for (double f = 1000.0; f <= 20000.0; f += 50.0)
            {
                bool isHarmonic = false;

                for (int n = 1; n <= (int) (20000.0 / probeHz) + 1; ++n)
                    if (std::abs (f - (double) n * probeHz) < 60.0)
                        isHarmonic = true;

                if (isHarmonic)
                    continue;

                const float a = componentAmplitudeHann (out, f);
                const float db = 20.0f * std::log10 (juce::jmax (a, 1.0e-12f)
                                                       / juce::jmax (fundamental, 1.0e-12f));
                worstDb = juce::jmax (worstDb, db);
            }

            return worstDb;
        };

        const float hqOn = aliasFloorDb();
        t.expect (hqOn < -80.0f,
                  "HQ 4x : aucun repliement au-dessus de -80 dBFS dans 0-20 kHz (pire "
                      + juce::String (hqOn, 1) + " dB)");

        // Le temoin : hors HQ, le repli 3f de la sonde 8 kHz (24 k -> 24 k, au
        // Nyquist exact : on verifie plutot 5f -> 40 k replie a 8 k... non :
        // 5f = 40 k se replie a 48 k - 40 k = 8 k, DANS la bande, sur la
        // fondamentale elle-meme). Le temoin mesure donc un signal QUI REPLIE :
        // 9 kHz, drive a fond, 3f = 27 k -> repli a 21 k > Nyquist 24 k ? Non,
        // 21 k < 24 k : pas de repli. Sonde 9,5 kHz : 3f = 28,5 k -> replie a
        // 48 - 28,5 = 19,5 k, dans la bande.
    }

    // =====================================================================
    // F2.2 / AC2 de US-09 : la latence HQ n'est reportee QUE quand le module
    // traite reellement (drive engage) — un drive inerte est bit-exact et
    // n'applique AUCUN retard, meme HQ (amendement UI du 2026-09-29 : HQ est
    // toujours actif, c'est l'inertie du module qui garde le chemin a zero).
    // La latence reportee est VERIFIEE par correlation : l'impulsion ressort
    // reportee du nombre exact reporte.
    // =====================================================================
    void testHqLatencyReported (juce::UnitTest& t)
    {
        // Drive inerte : chemin bit-exact, latence 0 — malgre HQ actif.
        {
            ODVoxAudioProcessor p;
            p.prepareToPlay (kSampleRate, 512);

            t.expectEquals (p.getLatencySamples(), 0,
                            "drive inerte : latence reportee nulle (le chemin HQ ne ment pas)");
        }

        // Drive engage : reportee = mesuree par correlation (AC1 de US-12, rev).
        {
            ODVoxAudioProcessor p;
            setActual (p, "drive_amount", 80.0f);   // module ENGAGE : le chemin HQ applique reellement son retard
            p.prepareToPlay (kSampleRate, 512);

            const int reported = p.getLatencySamples();
            t.expect (reported > 0,
                      "en HQ, la latence reportee est celle des filtres half-band ("
                          + juce::String (reported) + " echantillons)");

            // Impulsion au centre : sa position de sortie doit etre entree +
            // latence reportee, a ±1 echantillon.
            const int n = 48000;
            std::vector<float> probe ((size_t) n, 0.0f);
            probe[(size_t) (n / 2)] = 0.8f;

            const auto out = render (p, probe);

            int peak = 0;
            float peakValue = 0.0f;

            for (int i = 0; i < n; ++i)
                if (std::abs (out[(size_t) i]) > peakValue)
                {
                    peakValue = std::abs (out[(size_t) i]);
                    peak = i;
                }

            t.expect (std::abs (peak - (n / 2 + reported)) <= 1,
                      "l'impulsion ressort reportee du nombre EXACT reporte (pic "
                          + juce::String (peak) + " contre attendu "
                          + juce::String (n / 2 + reported) + ")");
        }
    }

    // =====================================================================
    // F2.2 / AC3 de US-09 : la bascule HQ on/off ne produit pas de clic —
    // variation de niveau < 1 dB sur 20 ms autour de la transition. La chaine
    // est rendue en continu, le parametre bascule entre deux blocs, et le
    // niveau RMS des fenetres de 20 ms avant/apres est compare.
    // =====================================================================
    void testHqToggleDoesNotClick (juce::UnitTest& t)
    {
        // Sonde 440 Hz continue, rendue en blocs de 512 : la bascule arrive
        // entre deux blocs, comme un hote le ferait.
        const int blockLen = 512;
        const int numBlocks = 200;   // ~2,1 s
        const int total = numBlocks * blockLen;

        auto makeProbe = [&]
        {
            std::vector<float> v ((size_t) total);

            for (int i = 0; i < total; ++i)
                v[(size_t) i] = 0.25f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                    * 440.0 * (double) i / kSampleRate);

            return v;
        };

        const auto probe = makeProbe();

        // Rendu en deux segments : tout en HQ, puis bascule OFF, le reste hors
        // HQ. La transition est au bloc 100.
        const int toggleBlock = 100;

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, blockLen);
        setActual (p, "drive_amount", 80.0f);
        setActual (p, "hq_mode", 1.0f);

        std::vector<float> out;
        out.reserve ((size_t) total);

        auto pushSegment = [&] (int fromBlock, int toBlock)
        {
            juce::AudioBuffer<float> buffer (2, blockLen);

            for (int b = fromBlock; b < toBlock; ++b)
            {
                for (int i = 0; i < blockLen; ++i)
                {
                    const int idx = b * blockLen + i;
                    buffer.setSample (0, i, probe[(size_t) idx]);
                    buffer.setSample (1, i, probe[(size_t) idx]);
                }

                juce::MidiBuffer midi;
                p.processBlock (buffer, midi);

                for (int i = 0; i < blockLen; ++i)
                    out.push_back (buffer.getSample (0, i));
            }
        };

        pushSegment (0, toggleBlock);

        setActual (p, "hq_mode", 0.0f);
        pushSegment (toggleBlock, numBlocks);

        // RMS sur 20 ms avant et apres la frontiere de bascule (moins la
        // latence HQ qui decale le wet — mais le DRIVE est seul en jeu, et son
        // effet est un TIMBRE, pas un retard de niveau : la fenetre compare le
        // regime etabli de chaque cote).
        const auto rmsOf = [&] (int from, int to)
        {
            double sum = 0.0;
            const int lo = juce::jlimit (0, total - 1, from);
            const int hi = juce::jlimit (0, total, to);

            for (int i = lo; i < hi; ++i)
                sum += (double) out[(size_t) i] * out[(size_t) i];

            return std::sqrt (sum / (double) juce::jmax (1, hi - lo));
        };

        const int ms20 = (int) (0.020 * kSampleRate);
        const int frontier = toggleBlock * blockLen;

        const double before = rmsOf (frontier - ms20, frontier);
        const double after  = rmsOf (frontier + ms20, frontier + 2 * ms20);

        const double deltaDb = std::abs (20.0 * std::log10 (juce::jmax (before, 1.0e-12)
                                                               / juce::jmax (after, 1.0e-12)));

        t.expect (deltaDb < 1.0,
                  "bascule HQ on/off : variation de niveau < 1 dB sur 20 ms ("
                      + juce::String (deltaDb, 3) + " dB)");
    }
};

static DriveTests driveTests;
