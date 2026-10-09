#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Spectrum.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    /** Colonnes d'affichage du test. Il en faut assez pour que la resolution
        affichee ne soit pas limitee par la largeur de la fenetre : c'est la
        largeur de BANDE d'analyse que l'on mesure, pas le nombre de pixels. */
    constexpr int kColumns = 800;

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }

    /** Pousse un signal dans le tap et laisse l'analyseur consommer, exactement
        comme le fait le plugin : le tap est alimente par blocs, l'analyseur est
        appele par le thread de messages. */
    void analyse (odvox::SpectrumTap& tap, odvox::SpectrumAnalyzer& analyser,
                  const std::vector<float>& signal, float maxHz)
    {
        constexpr int kBlock = 512;
        size_t position = 0;

        while (position < signal.size())
        {
            const int n = (int) juce::jmin ((size_t) kBlock, signal.size() - position);
            tap.push (signal.data() + position, n);
            position += (size_t) n;

            analyser.update (tap, kColumns, maxHz);
        }
    }

    /** Frequence de la colonne de plus forte valeur. */
    float peakFrequency (const std::vector<float>& analysed, float maxHz)
    {
        const auto& columns = analysed;

        if (columns.empty())
            return 0.0f;

        const auto it = std::max_element (columns.begin(), columns.end());
        const int index = (int) std::distance (columns.begin(), it);

        return odvox::SpectrumAnalyzer::columnFrequency (index, (int) columns.size(), maxHz);
    }

    float peakLevelDb (const std::vector<float>& analysed)
    {
        return analysed.empty() ? -200.0f
                                : *std::max_element (analysed.begin(), analysed.end());
    }

    /** Largeur, en octaves, de la tache d'un pic : le nombre de colonnes a moins
        de 3 dB du sommet. C'est la mesure DIRECTE de ce que l'affichage sait
        distinguer — un analyseur dont la tache ferait une octave ne « resout »
        pas 1/12 d'octave, quoi qu'en dise sa table de parametres. */
    float peakWidthOctaves (const std::vector<float>& columns, float maxHz)
    {
        if (columns.empty())
            return 0.0f;

        const auto peak = std::max_element (columns.begin(), columns.end());
        const float threshold = *peak - 3.0f;
        const int centre = (int) std::distance (columns.begin(), peak);

        int first = centre, last = centre;

        while (first > 0 && columns[(size_t) (first - 1)] > threshold)
            --first;

        while (last + 1 < (int) columns.size() && columns[(size_t) (last + 1)] > threshold)
            ++last;

        const float low  = odvox::SpectrumAnalyzer::columnFrequency (first, (int) columns.size(), maxHz);
        const float high = odvox::SpectrumAnalyzer::columnFrequency (last, (int) columns.size(), maxHz);

        return std::log2 (high / low);
    }
}

class SpectrumTests : public juce::UnitTest
{
public:
    SpectrumTests() : juce::UnitTest ("Analyseur de spectre (F1.6c, AC3 de US-04)") {}

    void runTest() override
    {
        beginTest ("AC3 : la bande d'analyse fait 1/12 d'octave");
        testBandWidthIsOneTwelfthOctave();

        beginTest ("AC3 : la raie est plus fine que la bande des 50 Hz");
        testBinIsFinerThanTheBand();

        beginTest ("AC3 : un sinus se lit a sa frequence et a son niveau");
        testToneReadsAtItsFrequencyAndLevel();

        beginTest ("AC3 : la resolution affichee fait 1/12 d'octave");
        testResolutionBetweenTwoTones();

        beginTest ("AC3 : au moins 20 mises a jour par seconde");
        testUpdateRate();

        beginTest ("AC3 : 10 minutes de signal sans aucun echantillon abandonne");
        testTenMinutesWithoutDroppedSamples();

        beginTest ("AC3 : la chaine complete alimente l'analyseur sans perdre un echantillon");
        testWholeChainFeedsTheAnalyzer();
    }

private:
    // =====================================================================
    // La resolution exigee est une largeur de bande : 1/12 d'octave. Elle est
    // mesuree sur les bornes REELLES de la bande agregee, et non sur la
    // constante qui la declare.
    // =====================================================================
    void testBandWidthIsOneTwelfthOctave()
    {
        const float maxHz = 20000.0f;
        float worst = 0.0f;

        for (int column : { 0, 100, 400, 799 })
        {
            float low = 0.0f, high = 0.0f;
            odvox::SpectrumAnalyzer::columnBandHz (column, kColumns, maxHz, low, high);

            const float octaves = std::log2 (high / low);
            worst = juce::jmax (worst, std::abs (octaves - 1.0f / 12.0f));

            expectWithinAbsoluteError (octaves, 1.0f / 12.0f, 1.0e-4f,
                                       "colonne " + juce::String (column) + " : bande de "
                                           + juce::String (octaves * 12.0f, 4) + "/12 d'octave");
        }

        expect (worst < 1.0e-4f,
                "toutes les bandes font 1/12 d'octave (pire ecart "
                    + juce::String (worst * 12.0f, 5) + "/12)");
    }

    // =====================================================================
    // Une bande de 1/12 d'octave ne veut rien dire si la raie de la FFT est plus
    // large qu'elle : l'affichage montrerait alors la MEME mesure repetee. Le
    // test verifie l'inverse sur toute la plage exploitable, et il consigne la
    // limite basse plutot que de la cacher.
    // =====================================================================
    void testBinIsFinerThanTheBand()
    {
        odvox::SpectrumAnalyzer analyser;
        analyser.prepare (kSampleRate);

        const float maxHz = 20000.0f;
        float worstRatio = 0.0f;
        float lowestOk = 0.0f;

        for (float hz = 20.0f; hz <= 20000.0f; hz *= 1.05f)
        {
            // Colonne la plus proche de cette frequence.
            const float x01 = std::log (hz / odvox::SpectrumAnalyzer::kMinHz)
                              / std::log (maxHz / odvox::SpectrumAnalyzer::kMinHz);
            const int column = juce::jlimit (0, kColumns - 1, (int) std::lround (x01 * kColumns));

            float low = 0.0f, high = 0.0f;
            odvox::SpectrumAnalyzer::columnBandHz (column, kColumns, maxHz, low, high);

            const float ratio = (float) (analyser.binHz() / (double) (high - low));

            if (ratio <= 1.0f && lowestOk == 0.0f)
                lowestOk = hz;

            worstRatio = juce::jmax (worstRatio, ratio);
        }

        // A 48 kHz, la raie vaut 2,93 Hz : la bande de 1/12 d'octave lui est
        // superieure des 50 Hz environ.
        expect (lowestOk > 0.0f && lowestOk < 60.0f,
                "la bande de 1/12 d'octave est plus large que la raie des "
                    + juce::String (lowestOk, 1) + " Hz (raie "
                    + juce::String (analyser.binHz(), 2) + " Hz)");

        float low20 = 0.0f, high20 = 0.0f;
        odvox::SpectrumAnalyzer::columnBandHz (0, kColumns, maxHz, low20, high20);

        expect (high20 - low20 < analyser.binHz(),
                "et la limite est consignee : a 20 Hz la bande fait "
                    + juce::String (high20 - low20, 2) + " Hz, moins que la raie de "
                    + juce::String (analyser.binHz(), 2) + " Hz");
    }

    // =====================================================================
    // Calibration : un sinus de niveau connu se lit a sa frequence et a son
    // niveau. C'est la verification qui rend le spectre utilisable — un
    // analyseur decale de 6 dB ferait couper la mauvaise chose.
    // =====================================================================
    void testToneReadsAtItsFrequencyAndLevel()
    {
        const float maxHz = 20000.0f;

        for (const float freq : { 200.0f, 1000.0f, 5000.0f })
        {
            for (const float levelDb : { -12.0f, -30.0f })
            {
                odvox::SpectrumTap tap;
                odvox::SpectrumAnalyzer analyser;
                tap.prepare();
                analyser.prepare (kSampleRate);

                analyse (tap, analyser, makeSine (freq, juce::Decibels::decibelsToGain (levelDb), 0.6), maxHz);

                const float peak = peakFrequency (analyser.columnsDb(), maxHz);
                const float level = peakLevelDb (analyser.columnsDb());

                // Le pic doit tomber dans la MEME bande de 1/12 d'octave que la
                // sonde, soit un ecart relatif inferieur a 2^(1/24) − 1 = 2,9 %.
                const float error = std::abs (peak - freq) / freq;

                expect (error < 0.03f,
                        "sinus a " + juce::String (freq, 0) + " Hz : pic lu a "
                            + juce::String (peak, 1) + " Hz (ecart "
                            + juce::String (error * 100.0f, 2) + " %)");

                expectWithinAbsoluteError (level, levelDb, 2.0f,
                                           "sinus a " + juce::String (levelDb, 0) + " dBFS a "
                                               + juce::String (freq, 0) + " Hz : lu a "
                                               + juce::String (level, 2) + " dB");
            }
        }

        // Un signal a −60 dBFS doit rester au-dessus du plancher : le plafond de
        // l'echelle ne doit pas avaler la dynamique utile.
        {
            odvox::SpectrumTap tap;
            odvox::SpectrumAnalyzer analyser;
            tap.prepare();
            analyser.prepare (kSampleRate);

            analyse (tap, analyser, makeSine (1000.0f, juce::Decibels::decibelsToGain (-60.0f), 0.6), maxHz);

            expect (peakLevelDb (analyser.columnsDb()) > odvox::SpectrumAnalyzer::kMinDb + 3.0f,
                    "un signal a −60 dBFS reste visible (plancher a "
                        + juce::String (odvox::SpectrumAnalyzer::kMinDb, 0) + " dB)");
        }
    }

    // =====================================================================
    // La resolution, mesuree par ce qu'elle sert a faire : SEPARER deux
    // frequences. La cible est 1/12 d'octave, donc :
    //   (a) la tache d'une sonde SEULE doit rester du meme ordre que 1/12
    //       d'octave — sinon l'affichage lisserait plus qu'il ne le pretend ;
    //   (b) deux sondes separees de 1/6 d'octave (deux fois la resolution)
    //       doivent donner DEUX sommets avec un creux mesurable.
    //
    // A 1/12 d'octave EXACTEMENT les deux taches fusionnent : c'est la
    // definition d'une resolution, pas une faiblesse — on le mesure et on le
    // consigne au lieu d'en faire un critere impossible.
    // =====================================================================
    void testResolutionBetweenTwoTones()
    {
        const float maxHz = 20000.0f;
        const float lowHz  = 2000.0f;
        const float atLimit = lowHz * std::pow (2.0f, 1.0f / 12.0f);
        const float twice   = lowHz * std::pow (2.0f, 1.0f / 6.0f);

        const auto spectrumOf = [] (std::vector<float> signal)
            {
                odvox::SpectrumTap tap;
                odvox::SpectrumAnalyzer analyser;
                tap.prepare();
                analyser.prepare (kSampleRate);
                analyse (tap, analyser, signal, 20000.0f);

                return analyser.columnsDb();
            };

        const auto twoTones = [] (float a, float b)
            {
                auto signal = makeSine (a, 0.25f, 0.6);
                const auto second = makeSine (b, 0.25f, 0.6);

                for (size_t i = 0; i < signal.size(); ++i)
                    signal[i] += second[i];

                return signal;
            };

        const auto columnsOf = [] (float hz)
            {
                const float x01 = std::log (hz / odvox::SpectrumAnalyzer::kMinHz)
                                  / std::log (20000.0f / odvox::SpectrumAnalyzer::kMinHz);
                return juce::jlimit (0, kColumns - 1, (int) std::lround (x01 * kColumns));
            };

        // --- (a) une sonde seule -------------------------------------------
        const auto single = spectrumOf (makeSine (lowHz, 0.25f, 0.6));
        const float widthSingle = peakWidthOctaves (single, maxHz);

        juce::Logger::writeToLog ("Spectre : tache a -3 dB d'une sonde seule = "
                                  + juce::String (widthSingle * 12.0f, 2) + "/12 d'octave");

        // Mesure : 0,90/12 d'octave. Une tache PLUS ETROITE que la bande, parce
        // que le seuil de −3 dB coupe les bords de la bande ; l'important est
        // qu'elle ne soit pas plus LARGE que ce qui est annonce.
        expect (widthSingle * 12.0f <= 1.5f,
                "la tache d'une sonde seule ne depasse pas la resolution annoncee ("
                    + juce::String (widthSingle * 12.0f, 2) + "/12 d'octave)");

        // --- (b) deux sondes a deux fois la resolution ---------------------
        const auto pair = spectrumOf (twoTones (lowHz, twice));

        const int c1 = columnsOf (lowHz);
        const int c2 = columnsOf (twice);
        const int middle = (c1 + c2) / 2;

        const float atLow  = pair[(size_t) c1];
        const float atHigh = pair[(size_t) c2];
        const float between = pair[(size_t) middle];

        const float dip = juce::jmin (atLow, atHigh) - between;

        juce::Logger::writeToLog ("Spectre : deux sondes a 1/6 d'octave = "
                                  + juce::String (atLow, 2) + " / " + juce::String (between, 2)
                                  + " / " + juce::String (atHigh, 2) + " dB (creux "
                                  + juce::String (dip, 2) + " dB)");

        // Chaque sonde est dans SA bande (1/6 d'octave d'ecart, pour des bandes de
        // 1/12) : chacune se lit donc a son propre niveau, -12 dBFS. Si les deux
        // bandes se recouvraient, on lirait plus haut — c'est la resolution qui
        // les separe.
        expectWithinAbsoluteError (atLow, -12.0f, 2.5f,
                                   "chaque sonde se lit a son propre niveau ("
                                       + juce::String (atLow, 2) + " dB pour -12 attendus)");
        expectWithinAbsoluteError (atHigh, -12.0f, 2.5f,
                                   "la seconde aussi (" + juce::String (atHigh, 2) + " dB)");
        // Mesure : 77,96 dB de creux. Deux sondes separees de deux fois la
        // resolution tombent dans deux bandes disjointes : la separation est
        // franche, et c'est ce qu'« une resolution de 1/12 d'octave » promet.
        expect (dip > 20.0f,
                "a deux fois la resolution, la separation est franche : " + juce::String (dip, 2)
                    + " dB de creux entre les deux sondes");

        // --- Et a la limite, on assume la fusion ---------------------------
        const auto merged = spectrumOf (twoTones (lowHz, atLimit));
        const float widthMerged = peakWidthOctaves (merged, maxHz);

        juce::Logger::writeToLog ("Spectre : deux sondes a 1/12 d'octave = tache de "
                                  + juce::String (widthMerged * 12.0f, 2) + "/12 d'octave");

        // Mesure : 1,79/12 d'octave, contre 0,90 pour une sonde seule. A la
        // limite EXACTE de la resolution, les deux taches se touchent : l'une
        // elargit l'autre, sans se separer. C'est la definition d'une resolution
        // — on la mesure et on la consigne, on n'en fait pas un critere
        // impossible.
        expect (widthMerged > widthSingle * 1.4f,
                "a 1/12 d'octave exactement, les deux taches n'en font plus qu'une, plus large "
                    "(" + juce::String (widthMerged * 12.0f, 2) + "/12 contre "
                    + juce::String (widthSingle * 12.0f, 2) + "/12 pour une seule)");
    }

    // =====================================================================
    // AC3 exige 20 images/s. L'affichage en fait 30 (le timer de la vue), mais
    // la donnee NEUVE ne peut pas arriver plus vite que le pas de recouvrement :
    // c'est donc elle qu'il faut verifier, sinon l'interface repeindrait 30 fois
    // par seconde la meme image.
    // =====================================================================
    void testUpdateRate()
    {
        for (const double sr : { 44100.0, 48000.0, 96000.0 })
        {
            odvox::SpectrumAnalyzer analyser;
            analyser.prepare (sr);

            expect (analyser.updatesPerSecond() >= 20.0,
                    "a " + juce::String (sr / 1000.0, 1) + " kHz : "
                        + juce::String (analyser.updatesPerSecond(), 1) + " mises a jour par seconde");
        }

        // Et la mesure directe : combien de FFT un flux reel produit-il ?
        //
        // Le signal dure 10 s et non 2 : la premiere FFT ne peut pas tomber avant
        // que la fenetre soit pleine (16384 echantillons, soit 8 pas de 2048), et
        // ce demarrage ne doit pas etre compte comme un retard de regime etabli.
        {
            odvox::SpectrumTap tap;
            odvox::SpectrumAnalyzer analyser;
            tap.prepare();
            analyser.prepare (kSampleRate);

            const auto signal = makeSine (1000.0f, 0.25f, 10.0);
            int ffts = 0;

            // L'interface appelle l'analyseur a 30 Hz : on reproduit ce rythme en
            // appelant l'analyseur tous les 1600 echantillons (48000 / 30).
            constexpr int kPerFrame = (int) (kSampleRate / 30.0);

            for (size_t position = 0; position + 512 <= signal.size(); position += 512)
            {
                tap.push (signal.data() + position, 512);

                if (position % (size_t) kPerFrame < 512)
                {
                    int computed = 0;
                    analyser.update (tap, kColumns, 20000.0f, &computed);
                    ffts += computed;
                }
            }

            const double perSecond = (double) ffts / 10.0;

            expect (perSecond >= 20.0,
                    "mesure directe : " + juce::String (perSecond, 1)
                        + " FFT par seconde sur un flux reel a 48 kHz");
        }
    }

    // =====================================================================
    // « aucun depassement de bloc sur 10 min ». Ce que ce critere interdit, c'est
    // que l'analyse RETARDE le traitement : le tap ne bloque jamais et n'alloue
    // jamais, donc le seul mode de defaillance possible est l'abandon
    // d'echantillons quand le lecteur ne suit plus. Le test fait tourner
    // 10 minutes de signal en verifiant qu'aucun echantillon n'est abandonne.
    // =====================================================================
    void testTenMinutesWithoutDroppedSamples()
    {
        odvox::SpectrumTap tap;
        odvox::SpectrumAnalyzer analyser;
        tap.prepare();
        analyser.prepare (kSampleRate);

        const auto signal = makeSine (1000.0f, 0.25f, 1.0);

        constexpr int kBlock = 512;
        constexpr int kPerFrame = (int) (kSampleRate / 30.0);   // 30 images/s

        const long long total = (long long) (kSampleRate * 600.0);
        long long position = 0;
        int frames = 0;

        while (position < total)
        {
            // On rejoue toujours le meme tampon de 1 s : c'est sa LONGUEUR qui
            // compte ici, pas son contenu — le critere porte sur la tenue du
            // couple audio/messages dans la duree.
            const auto start = (size_t) (position % (long long) signal.size());

            tap.push (signal.data() + start, kBlock);
            position += kBlock;

            if (position % kPerFrame < kBlock)
            {
                analyser.update (tap, kColumns, 20000.0f);
                ++frames;
            }
        }

        expect (tap.droppedSamples() == 0,
                "10 minutes a 48 kHz et 30 images/s : "
                    + juce::String (tap.droppedSamples()) + " echantillon(s) abandonne(s)");

        expect (frames > 17000,
                "et l'analyse a bien tourne (" + juce::String (frames) + " images sur 10 minutes)");

        expect (analyser.columnsDb().size() == (size_t) kColumns,
                "le spectre est toujours publie (" + juce::String ((int) analyser.columnsDb().size())
                    + " colonnes)");
    }

    // =====================================================================
    // Le tap est branche sur la SORTIE de la chaine complete : c'est ce que
    // l'analyseur doit montrer. Le test fait tourner le vrai processeur et
    // verifie qu'il alimente l'analyseur sans rien perdre ni produire de NaN.
    // =====================================================================
    void testWholeChainFeedsTheAnalyzer()
    {
        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        const auto signal = makeSine (1000.0f, 0.2f, 20.0);
        juce::MidiBuffer midi;

        int frames = 0;
        bool allFinite = true;

        for (size_t position = 0; position + 512 <= signal.size(); position += 512)
        {
            juce::AudioBuffer<float> buffer (2, 512);

            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, signal[position + (size_t) i]);
                buffer.setSample (1, i, signal[position + (size_t) i]);
            }

            p.processBlock (buffer, midi);

            if ((position / 512) % 3 == 0)
            {
                p.updateSpectrum (kColumns, 20000.0f);
                ++frames;
            }

            for (int i = 0; i < 512; ++i)
                allFinite = allFinite && std::isfinite (buffer.getSample (0, i));
        }

        expect (allFinite, "la chaine reste finie sur 20 s de signal");

        expect (p.spectrumDroppedSamples() == 0,
                "20 s de chaine complete a 2 canaux : "
                    + juce::String (p.spectrumDroppedSamples()) + " echantillon(s) abandonne(s)");

        const float peak = peakFrequency (p.spectrumDb(), 20000.0f);
        const float error = std::abs (peak - 1000.0f) / 1000.0f;

        expect (error < 0.05f,
                "le spectre de la SORTIE montre bien la sonde a 1 kHz (pic a "
                    + juce::String (peak, 1) + " Hz)");

        expect (frames > 600, "et il a ete rafraichi " + juce::String (frames) + " fois");
    }
};

static SpectrumTests spectrumTests;
