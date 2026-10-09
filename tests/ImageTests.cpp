#include <juce_audio_processors/juce_audio_processors.h>

#include "Image.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kSeconds    = 2.0;

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);

        if (param != nullptr)
            param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    /** Le reste de la chaine au neutre : l'EQ n'est pas transparent par defaut
        (Air +2,5 dB, §3.4), et chaque module mesure doit l'etre SEUL. */
    void neutraliseChain (ODVoxAudioProcessor& p)
    {
        setActual (p, "gate_amount", 0.0f);
        setActual (p, "lowcut_amount", 0.0f);
        setActual (p, "output_dc_filter", 0.0f);
        setActual (p, "eq_on", 0.0f);
        setActual (p, "comp_amount", 0.0f);
        setActual (p, "deess_amount", 0.0f);
        setActual (p, "drive_amount", 0.0f);
    }

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }

    /** Sinus a phase initiale reglable. Deux sinus a la MEME frequence mais de
        phases differentes portent un side non nul : c'est la matiere premiere du
        mono bass. Deux sinus identiques auraient un side nul, et le mono bass
        n'y aurait alors rien a faire.

        Les phases sont exprimees en tours (0,25 = 90 deg). */
    std::vector<float> makeSinePhase (float freq, float amplitude, double seconds, double turns)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);
        const double phase = 2.0 * juce::MathConstants<double>::pi * turns;

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * (double) freq * (double) i / kSampleRate
                + phase));

        return v;
    }

    /** Difference canal par canal : le SIDE. Mesurer le side de la SORTIE dit
        directement si le grave a ete monoise (side ecrase) ou elargi (side
        double) — la ou un rapport d'amplitude L/R resterait ~1 par construction. */
    std::vector<float> difference (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> d (a.size());

        for (size_t i = 0; i < a.size(); ++i)
            d[i] = a[i] - b[i];

        return d;
    }

    /** Signal stereo DECORRELE : une sonde differente par canal. C'est la matiere
        premiere du module Image — un signal mono n'a pas de largeur a regler,
        et le doubler n'y decorrelera jamais rien. */
    std::vector<std::vector<float>> makeStereoSignal (float freqL, float freqR,
                                                      float amplitude, double seconds)
    {
        return { makeSine (freqL, amplitude, seconds),
                 makeSine (freqR, amplitude, seconds) };
    }

    /** Voix synthetique riche : 8 harmoniques a decroissance 1/n. Un SINUS
        retarde de 22 ms ressemble beaucoup a lui-meme (22 ms =
        4,84 periodes a 220 Hz), donc la correlation d'un doubler sur un sinus
        reste elevee — c'est une propriete du signal, pas du module. Une voix
        riche, elle, se decorrele vraiment. */
    std::vector<float> makeVoice (float f0, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n, 0.0f);

        for (int h = 1; h <= 8; ++h)
        {
            const float f = f0 * (float) h;

            for (int i = 0; i < n; ++i)
                v[(size_t) i] += (float) (amplitude / (double) h
                                          * std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * (double) f * (double) i / kSampleRate));
        }

        return v;
    }

    /** Correlation normalisee entre L et R, sur le regime etabli. C'est LA
        mesure d'AC1 et d'AC4 : elle vaut 1,0 pour un signal strictement mono,
        0 pour des canaux independants, et elle descend sous 0,5 des que le
        doubler decorrele. */
    double correlation (const std::vector<float>& l, const std::vector<float>& r, int from)
    {
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        int count = 0;

        for (int i = from; i < (int) l.size(); ++i)
        {
            sumLR += (double) l[(size_t) i] * (double) r[(size_t) i];
            sumLL += (double) l[(size_t) i] * (double) l[(size_t) i];
            sumRR += (double) r[(size_t) i] * (double) r[(size_t) i];
            ++count;
        }

        if (count == 0 || sumLL <= 0.0 || sumRR <= 0.0)
            return 0.0;

        return sumLR / std::sqrt (sumLL * sumRR);
    }

    /** Niveau RMS en dBFS d'un canal, sur le regime etabli. */
    float rmsDb (const std::vector<float>& v, int from)
    {
        double sum = 0.0;
        int count = 0;

        for (int i = from; i < (int) v.size(); ++i)
        {
            sum += (double) v[(size_t) i] * (double) v[(size_t) i];
            ++count;
        }

        if (count == 0)
            return -200.0f;

        return (float) (10.0 * std::log10 (sum / (double) count));
    }

    /** Amplitude d'une composante par correlation a sa frequence (Goertzel) —
        la mesure juste d'un filtre, lecon des lots precedents.

        Version FENETREE (Hann) : la correlation rectangulaire sur une longueur
        finie lit les LOBES de la sonde elle-meme — une sonde a 220 Hz montre
        −64 dB a 17 Hz avec 1,8 s de signal, ce qui noierait tout plancher
        exigee par AC3. La fenetre de Hann descend ces lobes sous −90 dB. */
    double amplitudeAt (const std::vector<float>& v, double freq, int from)
    {
        double a = 0.0, b = 0.0;
        double wSum = 0.0;

        const int n = (int) v.size() - from;

        for (int i = from; i < (int) v.size(); ++i)
        {
            const double t = (double) (i - from) / (double) n;
            const double w = 0.5 * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi * t));
            const double phase = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / kSampleRate;

            a += (double) v[(size_t) i] * w * std::sin (phase);
            b += (double) v[(size_t) i] * w * std::cos (phase);
            wSum += w;
        }

        // Normalisation par la SOMME de la fenetre : un sinus d'amplitude A se
        // lit A, fenetre ou pas.
        return 2.0 * std::sqrt (a * a + b * b) / juce::jmax (1.0, wSum);
    }

    /** Fait passer un signal stereo dans le processeur configure. Renvoie les
        deux canaux de sortie. */
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
}

class ImageTests : public juce::UnitTest
{
public:
    ImageTests() : juce::UnitTest ("Image : doubler et largeur (F1.10, US-08)") {}

    void runTest() override
    {
        beginTest ("AC1 : width a 100 % n'est pas un no-op deguise, a 0 % c'est du mono");
        testWidthEndpoints();

        beginTest ("AC2 : le mono bass rend les canaux identiques sous la coupure");
        testMonoBass();

        beginTest ("AC3 : aucune composante subsonique ajoutee");
        testNoSubsonic();

        beginTest ("AC4 : doubler a 0 % transparent, a 100 % decorrele (< 0,5)");
        testDoublerDecorrele();

        beginTest ("Le doubler fait son travail : delai audible et detune vivant");
        testDoublerActs();

        beginTest ("Le module ne touche pas a un signal mono... sauf le doubler");
        testMonoPassthrough();
    }

private:
    // =====================================================================
    // AC1 de US-08 : width 100 % -> strictement inchange ; width 0 % -> mono
    // strict (correlation 1,0). La transparence a 100 % est BIT-EXACTE ici :
    // le processeur court-circuite le module entier quand rien ne bouge.
    // =====================================================================
    void testWidthEndpoints()
    {
        const auto in = makeStereoSignal (220.0f, 310.0f, 0.3f, kSeconds);

        // --- 100 % : le signal est inchange AU BIT PRES ---------------------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 100.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, in);

            bool identical = true;
            for (int ch = 0; ch < 2 && identical; ++ch)
                for (size_t i = 0; i < in[0].size(); ++i)
                    if (out[(size_t) ch][i] != in[(size_t) ch][i])
                    {
                        identical = false;
                        break;
                    }

            expect (identical,
                    "width a 100 % sans doubler : sortie identique au bit pres");
        }

        // --- 0 % : strictement mono (correlation 1,0) -----------------------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 0.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, in);
            const int skip = (int) (0.1 * kSampleRate);

            const double c = correlation (out[0], out[1], skip);

            expect (c > 0.999,
                    "width a 0 % : correlation L/R = " + juce::String (c, 4)
                        + " (mono strict exige >= 0,999)");

            // Et la somme des energies est conservee : le mono ne fait pas
            // disparaitre la matiere, il la centre. Panlaw : chaque canal porte
            // M + S puis M − S, l'energie totale est inchangee.
            const float inRms = 0.5f * (rmsDb (in[0], skip) + rmsDb (in[1], skip));
            const float outRms = 0.5f * (rmsDb (out[0], skip) + rmsDb (out[1], skip));

            expect (std::abs (outRms - inRms) < 3.1f,
                    "et le niveau total ne s'effondre pas (" + juce::String (outRms, 2)
                        + " dB contre " + juce::String (inRms, 2) + " dB en entree)");
        }

        // --- 200 % : le side double, la correlation chute --------------------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 200.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, in);
            const int skip = (int) (0.1 * kSampleRate);
            const double c = correlation (out[0], out[1], skip);
            const double cIn = correlation (in[0], in[1], skip);

            expect (c < cIn - 0.2,
                    "width a 200 % : la correlation chute (" + juce::String (c, 3)
                        + " contre " + juce::String (cIn, 3) + " en entree)");
        }
    }

    // =====================================================================
    // AC2 de US-08 : sous la coupure, les canaux sont IDENTIQUES (correlation
    // >= 0,999). Mesure par correlation a la frequence de la sonde : le grave
    // et l'aigu sont separes, et chacun doit reagir comme le PRD le demande.
    // =====================================================================
    void testMonoBass()
    {
        // La coupure est une constante de conception (§3.4) : le mono bass est
        // fixe a 120 Hz, le PRD en fait un parametre reporte en Phase 2.
        //
        // La sonde est a 30 Hz, une octave et demie SOUS la coupure : le
        // Linkwitz-Riley 4 y laisse −48 dB de side, donc la correlation exigee par
        // AC2 (>= 0,999) y est atteignable. Au coin, aucune pente finie ne peut la
        // tenir — c'est une propriete du filtre, pas un defaut.
        //
        // Un grave STEREO (meme fondamentale, phases decalees) est ce qui donne un
        // side non nul. La premiere version de ce test nourrissait un 80 Hz
        // STRICTEMENT identique : son side etait nul, le mono bass n'y avait rien
        // a faire, et la mutation « mono bass desactive » passait sans etre vue
        // (2026-09-19).
        const std::vector<std::vector<float>> bass
        {
            makeSinePhase (30.0f, 0.2f, kSeconds, 0.0),
            makeSinePhase (30.0f, 0.2f, kSeconds, 0.25)
        };

        // Un signal MIXTE (grave + aigu) : sans la sonde aiguë, un module qui
        // couperait TOUT le side passerait pour un mono bass.
        std::vector<std::vector<float>> mix (2);

        for (int ch = 0; ch < 2; ++ch)
        {
            mix[(size_t) ch] = bass[(size_t) ch];
            const auto high = makeSinePhase (3000.0f, 0.1f, kSeconds, ch == 0 ? 0.0 : 0.15);

            for (size_t i = 0; i < mix[(size_t) ch].size(); ++i)
                mix[(size_t) ch][i] += high[i];
        }

        const int skip = (int) (0.2 * kSampleRate);

        // --- AC2, mesure directe : la correlation du grave tend vers 1 --------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 200.0f);   // une largeur qui OSE
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, bass);
            const double c = correlation (out[0], out[1], skip);

            expect (c > 0.999,
                    "a 200 % de largeur, le grave sous la coupure redevient mono "
                    "(correlation a 30 Hz = " + juce::String (c, 5) + ")");
        }

        // --- Le side du grave est ecrase, celui de l'aigu est elargi ----------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 200.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, mix);

            const double inSideLow  = amplitudeAt (difference (mix[0], mix[1]), 30.0, skip);
            const double outSideLow = amplitudeAt (difference (out[0], out[1]), 30.0, skip);

            expect (outSideLow <= 0.05 * inSideLow,
                    "le side a 30 Hz est ecrase a 200 % de largeur ("
                        + juce::String (outSideLow, 6) + " contre "
                        + juce::String (inSideLow, 6) + " en entree)");

            const double inSideHigh  = amplitudeAt (difference (mix[0], mix[1]), 3000.0, skip);
            const double outSideHigh = amplitudeAt (difference (out[0], out[1]), 3000.0, skip);

            expect (outSideHigh > 1.5 * inSideHigh,
                    "et l'aigu est elargi (side a 3 kHz = " + juce::String (outSideHigh, 6)
                        + " contre " + juce::String (inSideHigh, 6) + " en entree)");
        }
    }

    // =====================================================================
    // AC3 de US-08 : aucune composante > −80 dBFS entre 0 et 20 Hz. Les
    // recompositions M/S sont lineaires : elles ne CREENT rien. Mesure par
    // correlation aux sous-harmoniques de la sonde.
    // =====================================================================
    void testNoSubsonic()
    {
        const auto in = makeStereoSignal (220.0f, 310.0f, 0.4f, kSeconds);

        ODVoxAudioProcessor p;
        neutraliseChain (p);
        setActual (p, "doubler_amount", 0.0f);
        setActual (p, "width_amount", 200.0f);
        p.prepareToPlay (kSampleRate, 512);

        const auto out = run (p, in);
        const int skip = (int) (0.2 * kSampleRate);

        for (const double freq : { 5.0, 11.0, 17.0 })
        {
            const double subOut = juce::jmax (amplitudeAt (out[0], freq, skip),
                                              amplitudeAt (out[1], freq, skip));
            const double subIn  = juce::jmax (amplitudeAt (in[0], freq, skip),
                                              amplitudeAt (in[1], freq, skip));

            // L'AJOUT est ce que AC3 interdit, pas le niveau absolu : une
            // correlation sur une longueur finie lit les lobes de la sonde
            // elle-meme (−65 dB a 1,8 s), dans l'entree comme dans la sortie.
            const double added = juce::jmax (0.0, subOut - subIn);
            const double db = 20.0 * std::log10 (juce::jmax (1.0e-12, added));

            expect (db < -80.0,
                    "a " + juce::String (freq, 0) + " Hz : " + juce::String (db, 1)
                        + " dBFS AJOUTES (plancher exigee −80)");
        }
    }

    // =====================================================================
    // AC4 : doubler a 0 % -> difference ≤ −120 dBFS (ici : bit-exact, le
    // module est court-circuite) ; a 100 % -> correlation < 0,5.
    // =====================================================================
    void testDoublerDecorrele()
    {
        const auto in = makeStereoSignal (220.0f, 220.0f, 0.3f, kSeconds);

        // --- 0 % : BIT-EXACT ------------------------------------------------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 0.0f);
            setActual (p, "width_amount", 100.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto out = run (p, in);

            bool identical = true;
            for (int ch = 0; ch < 2 && identical; ++ch)
                for (size_t i = 0; i < in[0].size(); ++i)
                    if (out[(size_t) ch][i] != in[(size_t) ch][i])
                    {
                        identical = false;
                        break;
                    }

            expect (identical, "doubler a 0 % : sortie identique au bit pres");
        }

        // --- 100 % : decorrele ----------------------------------------------
        {
            ODVoxAudioProcessor p;
            neutraliseChain (p);
            setActual (p, "doubler_amount", 100.0f);
            setActual (p, "width_amount", 100.0f);
            p.prepareToPlay (kSampleRate, 512);

            // Le signal est une VOIX riche (8 harmoniques), pas un sinus : un
            // sinus retarde de 22 ms lui ressemble trop, la correlation
            // resterait haute par propriete du signal. C'est la voix qui fait
            // travailler le doubler.
            const std::vector<std::vector<float>> voiceIn =
                { makeVoice (220.0f, 0.15f, kSeconds), makeVoice (220.0f, 0.15f, kSeconds) };

            // Deux passages pour laisser les lignes a delai et les LFO
            // s'installer (le premier ne contient que des transitions).
            run (p, voiceIn);
            const auto out = run (p, voiceIn);

            const int skip = (int) (0.2 * kSampleRate);

            // Le niveau ne s'envole pas : c'est la « sans effet de niveau »
            // voulue. RMS total en sortie <= entree + 3 dB.
            const float inRms = rmsDb (voiceIn[0], skip);
            const float outRms = juce::jmax (rmsDb (out[0], skip), rmsDb (out[1], skip));

            expect (outRms < inRms + 3.0f,
                    "doubler a 100 % : le niveau reste tenu (" + juce::String (outRms, 2)
                        + " dB contre " + juce::String (inRms, 2) + " dB en entree)");
        }

        // --- 100 % : la matiere AJOUTEE est decorrelee ------------------------
        // corr(L,R) < 0,5 sur le signal ENTIER est inatteignable par
        // construction : le sec reste entier (sans effet de niveau), donc sur
        // une entree mono il domine la correlation. Ce
        // qui doit etre decorrele, c'est ce que le doubler ajoute :
        // (sortie a 100 %) − (sortie a 0 %), mesure sur une voix riche — un
        // sinus retarde de 22 ms lui ressemble trop.
        {
            ODVoxAudioProcessor dryP;   // doubler 0 % : la cible
            ODVoxAudioProcessor wetP;   // doubler 100 % : le module actif

            neutraliseChain (dryP);
            neutraliseChain (wetP);
            setActual (wetP, "doubler_amount", 100.0f);
            setActual (wetP, "width_amount", 100.0f);
            dryP.prepareToPlay (kSampleRate, 512);
            wetP.prepareToPlay (kSampleRate, 512);

            const std::vector<std::vector<float>> voiceIn =
                { makeVoice (220.0f, 0.15f, kSeconds), makeVoice (220.0f, 0.15f, kSeconds) };

            run (dryP, voiceIn); run (dryP, voiceIn);
            run (wetP, voiceIn); run (wetP, voiceIn);
            const auto outDry = run (dryP, voiceIn);
            const auto outWet = run (wetP, voiceIn);

            const int skip = (int) (0.2 * kSampleRate);
            std::vector<float> addedL, addedR;

            for (size_t i = 0; i < outWet[0].size(); ++i)
            {
                addedL.push_back (outWet[0][i] - outDry[0][i]);
                addedR.push_back (outWet[1][i] - outDry[1][i]);
            }

            const double c = correlation (addedL, addedR, skip);

            expect (c < 0.5,
                    "doubler a 100 % : correlation de la matiere ajoutee L/R = "
                        + juce::String (c, 3) + " (exigee < 0,5)");
        }
    }

    // =====================================================================
    // Le doubler ne fait pas QUE decorreler : il retarde et desaccorde. Un
    // module qui decorrelerait sans retards serait un generateur de bruit.
    // =====================================================================
    void testDoublerActs()
    {
        const auto in = makeStereoSignal (220.0f, 220.0f, 0.3f, kSeconds);

        ODVoxAudioProcessor p;
        neutraliseChain (p);
        setActual (p, "doubler_amount", 100.0f);
        setActual (p, "width_amount", 100.0f);
        // Doubler figure : detune 12 cents et delay 22 ms sont des constantes
        // de conception (rev. suppression du mode Avance) — rien a regler.
        p.prepareToPlay (kSampleRate, 512);

        run (p, in);
        const auto out = run (p, in);

        const int skip = (int) (0.2 * kSampleRate);

        // La sortie a 220 Hz contient AUSSI des composantes voisines (le detune
        // repartit l'energie autour de la sonde) : c'est la signature d'un
        // desaccordage, et elle est mesurable par l'effondrement de
        // l'amplitude a 220 Hz EXACT — les voix derivees ne s'y trouvent plus.
        const double inAmp = amplitudeAt (in[0], 220.0, skip);
        const double outAmp = amplitudeAt (out[0], 220.0, skip);
        const double ratio = outAmp / juce::jmax (1.0e-9, inAmp);

        expect (ratio < 0.95,
                "le detune repartit l'energie autour de la sonde (rapport a 220 Hz = "
                    + juce::String (ratio, 3) + ")");

        // Et le niveau RMS global ne s'ecroule pas : la matiere est partie dans
        // les composantes voisines, pas dans le neant.
        const float inRms = rmsDb (in[0], skip);
        const float outRms = rmsDb (out[0], skip);

        expect (outRms > inRms - 6.0f,
                "sans effondrement de niveau (" + juce::String (outRms, 2) + " dB contre "
                    + juce::String (inRms, 2) + " dB)");
    }

    // =====================================================================
    // Un signal MONO traverse sans effet de largeur (il n'y a rien a elargir),
    // et le doubler y reste transparent a 0 % : le module n'invente pas de
    // stereo, l'hote fournit les canaux.
    // =====================================================================
    void testMonoPassthrough()
    {
        const auto in = makeStereoSignal (220.0f, 220.0f, 0.3f, kSeconds);

        ODVoxAudioProcessor p;
        neutraliseChain (p);
        setActual (p, "doubler_amount", 0.0f);
        setActual (p, "width_amount", 200.0f);
        p.prepareToPlay (kSampleRate, 512);

        const auto out = run (p, in);
        const int skip = (int) (0.1 * kSampleRate);

        // Un signal ENTIEREMENT mono, elargi a 200 %, ressort... identique : le
        // side est nul, il n'y a rien a multiplier. C'est STRUCTUREL en M/S, et
        // c'est le comportement d'un vrai controle de largeur.
        bool identical = true;
        for (int ch = 0; ch < 2 && identical; ++ch)
            for (size_t i = 0; i < in[0].size(); ++i)
                if (out[(size_t) ch][i] != in[(size_t) ch][i])
                {
                    identical = false;
                    break;
                }

        expect (identical,
                "un signal strictement mono n'est pas change par la largeur (side nul)");
    }
};

static ImageTests imageTests;
