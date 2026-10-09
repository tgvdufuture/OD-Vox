#include <juce_audio_processors/juce_audio_processors.h>

#include "Eq.h"
#include "EqCurve.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kSeconds    = 0.5;

    /** Taille de la zone de dessin exercee par les tests. Ce sont des pixels : la
        valeur importe peu, mais elle doit etre la MEME pour le modele et pour les
        points envoyes, sinon on ne testerait que la coherence d'un calcul faux. */
    constexpr int kWidth  = 900;
    constexpr int kHeight = 160;

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    float actualOf (ODVoxAudioProcessor& p, const char* id)
    {
        auto* value = p.state().getRawParameterValue (id);
        jassert (value != nullptr);

        return value != nullptr ? value->load() : 0.0f;
    }

    /** Les identifiants de GAIN, dans l'ordre du catalogue. Ecrits a la main pour
        que le test attrape une inversion (Low <-> Mid) : ils ne sont pas deduits
        de la table du modele, sinon une erreur y serait invisible.

        rev. suppression du mode Avance : frequence, Q et type ne sont plus des
        parametres — ancres figurees dans `Eq` (kAnchor*), bandes pitch-suiveuses. */
    const char* const kBandGainIds[odvox::Eq::kNumBands] =
    {
        "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db"
    };

    /** Le reste de la chaine neutre : l'EQ doit etre mesure SEUL, et l'Air par
        defaut (+2,5 dB) n'est pas neutre. */
    void neutraliseOthers (ODVoxAudioProcessor& p)
    {
        setActual (p, "gate_amount", 0.0f);
        setActual (p, "lowcut_amount", 0.0f);
        setActual (p, "output_dc_filter", 0.0f);
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

    /** Amplitude d'une composante par correlation a sa frequence, en regime
        etabli — la seule mesure juste d'un filtre (une crete de bloc est dominee
        par son transitoire de demarrage). */
    double amplitudeAt (const std::vector<float>& v, double freq, int from)
    {
        double a = 0.0, b = 0.0;

        for (int i = from; i < (int) v.size(); ++i)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / kSampleRate;
            a += (double) v[(size_t) i] * std::sin (phase);
            b += (double) v[(size_t) i] * std::cos (phase);
        }

        const double n = (double) ((int) v.size() - from);
        return 2.0 * std::sqrt (a * a + b * b) / n;
    }

    float gainAt (ODVoxAudioProcessor& p, float freq, float amplitude = 0.1f)
    {
        const auto probe = makeSine (freq, amplitude, kSeconds);

        juce::AudioBuffer<float> buffer (2, (int) probe.size());

        for (size_t i = 0; i < probe.size(); ++i)
            buffer.setSample (0, (int) i, probe[i]);

        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        std::vector<float> out (probe.size());
        for (size_t i = 0; i < probe.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        const int skip = (int) (0.1 * kSampleRate);
        const double outAmp = amplitudeAt (out, freq, skip);
        const double inAmp  = amplitudeAt (probe, freq, skip);

        return 20.0f * (float) std::log10 (juce::jmax (1.0e-12, outAmp / inAmp));
    }
}

class EqCurveTests : public juce::UnitTest
{
public:
    EqCurveTests() : juce::UnitTest ("Courbe d'EQ interactive (F1.6c, AC1 de US-04, rev. ancres figees)") {}

    void runTest() override
    {
        beginTest ("Les mappages de l'axe : log en frequence, dB en gain");
        testMappings();

        beginTest ("La geometrie de l'ecran LCD : constants mesurees, viewport fidele");
        testLcdGeometry();

        beginTest ("La courbe tracee EST la reponse des filtres");
        testDrawnCurveIsTheFilterResponse();

        beginTest ("AC1 : un glisser-depose ecrit le gain");
        testDragWritesGain();

        beginTest ("Molette et menu : des no-op depuis le figeage des ancres");
        testWheelAndMenuAreNoOps();

        beginTest ("AC1 : la mesure suit la courbe apres le geste, a ±0,5 dB");
        testMeasuredResponseFollowsTheGesture();

        beginTest ("Cas limite : saisie hors des poignees, bornes du gain");
        testHitTestingAndClamping();

        beginTest ("Double-clic : retour au defaut du catalogue");
        testResetBand();
    }

private:
    // =====================================================================
    // La geometrie est la seule chose que le dessin et le geste partagent : si
    // elle se trompe, le point clique et la poignee dessinee divergent.
    // =====================================================================
    void testMappings()
    {
        using namespace odvox::eqcurve;

        expectWithinAbsoluteError (frequencyForX (0.0f, (float) kWidth), kMinHz, 0.01f,
                                   "le bord gauche vaut 20 Hz");
        expectWithinAbsoluteError (frequencyForX ((float) kWidth, (float) kWidth), kMaxHz, 1.0f,
                                   "le bord droit vaut 20 kHz");

        // Une octave fait une distance CONSTANTE : c'est la definition d'un axe
        // logarithmique, et c'est ce qui rend la courbe lisible.
        const float octaveLow  = xForFrequency (1000.0f, (float) kWidth)
                                 - xForFrequency (500.0f, (float) kWidth);
        const float octaveHigh = xForFrequency (8000.0f, (float) kWidth)
                                 - xForFrequency (4000.0f, (float) kWidth);

        expectWithinAbsoluteError (octaveHigh, octaveLow, 0.01f,
                                   "une octave fait la meme largeur partout ("
                                       + juce::String (octaveLow, 2) + " px)");

        // Le milieu de l'axe vaut la moyenne GEOMETRIQUE des bornes, pas la
        // moyenne arithmetique : 632 Hz et non 10 kHz.
        const float middle = frequencyForX ((float) kWidth * 0.5f, (float) kWidth);
        expectWithinAbsoluteError (middle, 632.0f, 2.0f,
                                   "le milieu de l'axe vaut 632 Hz (mesure "
                                       + juce::String (middle, 1) + " Hz)");

        // Aller-retour sur les deux axes.
        for (const float hz : { 30.0f, 120.0f, 700.0f, 3500.0f, 11000.0f })
            expectWithinAbsoluteError (frequencyForX (xForFrequency (hz, (float) kWidth),
                                                     (float) kWidth), hz, hz * 0.01f,
                                       "aller-retour a " + juce::String (hz, 0) + " Hz");

        expectWithinAbsoluteError (gainForY (0.0f, (float) kHeight), kMaxGainDb, 0.01f,
                                   "le haut vaut +15 dB");
        expectWithinAbsoluteError (gainForY ((float) kHeight, (float) kHeight), -kMaxGainDb, 0.01f,
                                   "le bas vaut -15 dB");
        expectWithinAbsoluteError (gainForY ((float) kHeight * 0.5f, (float) kHeight), 0.0f, 0.01f,
                                   "le milieu vaut 0 dB");

        for (const float db : { 15.0f, 7.5f, 0.0f, -3.0f, -15.0f })
            expectWithinAbsoluteError (gainForY (yForGain (db, (float) kHeight), (float) kHeight),
                                       db, 0.01f, "aller-retour a " + juce::String (db, 1) + " dB");
    }

    // =====================================================================
    // La geometrie de l'ecran LCD (docs/assets/LCD_fige.png, mesuree au pixel)
    // est un CONTRAT : les ancres figees du DSP doivent tomber sur les
    // positions mesurees de la maquette (c'est la meme loi log des deux cotes),
    // et le viewport projette bien l'axe generique dans le plot.
    // =====================================================================
    void testLcdGeometry()
    {
        using namespace odvox::lcd;

        // L'axe se definit par ses labels : 100 Hz / 1 kHz / 10 kHz centres ou
        // la maquette les dessine.
        expectWithinAbsoluteError (xForFrequency (100.0f), 244.0f, 0.01f,
                                   "100 Hz est a x244 (label mesure)");
        expectWithinAbsoluteError (xForFrequency (1000.0f), 409.0f, 0.01f,
                                   "1 kHz est a x409 (label mesure)");
        expectWithinAbsoluteError (xForFrequency (10000.0f), 574.0f, 0.01f,
                                   "10 kHz est a x574 (label mesure)");

        // Une DECADE fait une distance constante (165 px) : c'est un axe log.
        expectWithinAbsoluteError (xForFrequency (1000.0f) - xForFrequency (100.0f),
                                   kDecadePx, 0.01f, "une decade fait 165 px");

        // L'axe des gains : +12 / 0 / -12 aux y des labels mesures.
        expectWithinAbsoluteError (yForGain (0.0f), kZeroY, 0.01f, "0 dB est a y194.5");
        expectWithinAbsoluteError (yForGain (12.0f), 126.0f, 0.01f, "+12 dB est a y126");
        expectWithinAbsoluteError (yForGain (-12.0f), 264.0f, 0.01f, "-12 dB est a y264");

        // Aller-retours.
        for (const float hz : { 50.0f, 120.0f, 700.0f, 1750.0f, 5000.0f })
            expectWithinAbsoluteError (frequencyForX (xForFrequency (hz)), hz, hz * 0.001f,
                                       "aller-retour frequence a " + juce::String (hz, 0) + " Hz");

        for (const float db : { 12.0f, 5.1f, 0.0f, -5.1f, -12.0f })
            expectWithinAbsoluteError (gainForY (yForGain (db)), db, 0.001f,
                                       "aller-retour gain a " + juce::String (db, 1) + " dB");

        // LE contrat : avec le viewport LCD, une EqCurveModel place les
        // poignees EXACTEMENT ou la loi de l'ecran les met — les ancres du DSP
        // (120/700/1750/10000 Hz) passent par la MEME formule que les labels
        // 100 / 1k / 10k, donc poignees et gravures ne peuvent pas diverger.
        // (Les handles de la PHOTO de maquette sont aux positions de sa demo,
        // pas aux ancres : ce ne sont pas eux le contrat.)
        {
            ODVoxAudioProcessor p;
            odvox::EqCurveModel model (p, odvox::lcd::kWidth, odvox::lcd::kHeight);
            model.setViewport (axisViewport());

            for (int b = 0; b < odvox::Eq::kNumBands; ++b)
            {
                const auto handle = model.handlePosition (b);

                expectWithinAbsoluteError (handle.x,
                                           xForFrequency (odvox::Eq::kAnchorHz[b]), 1.0f,
                                           "la bande " + juce::String (b) + " ("
                                               + juce::String (odvox::Eq::kAnchorHz[b], 0)
                                               + " Hz) est placee par la loi de l'ecran");

                // A defaut de gain, la poignee est SUR la ligne 0 dB — sauf
                // l'Air, dont le defaut du catalogue est +2,5 dB.
                const float wantedY = yForGain (b == 3 ? 2.5f : 0.0f);

                expectWithinAbsoluteError (handle.y, wantedY, 0.6f,
                                           "la poignee de la bande " + juce::String (b)
                                               + " est a son gain par defaut");
            }

            // Le geste reste borne : tirer loin hors du plot s'arrete au clamp
            // du catalogue (±15 dB), et le pointeur hors viewport est quand
            // meme lu (projection, pas rejet).
            expect (model.beginGesture (model.handlePosition (0)), "geste sur le Low");
            model.dragTo ({ -500.0f, -500.0f });
            model.endGesture();

            expectWithinAbsoluteError (actualOf (p, "eq_low_db"), 15.0f, 0.01f,
                                       "tirer hors-plot en haut s'arrete a +15 dB");
        }
    }

    // =====================================================================
    // AC2 de US-04 est vrai PAR CONSTRUCTION : la courbe vient de
    // `Eq::responseDb`, qui interroge les memes fabriques de coefficients que les
    // filtres. Ce test le verifie la ou ca compte — en comparant le trace a la
    // reponse REELLEMENT mesuree sur le processeur.
    //
    // rev. suppression du mode Avance : les poignees ne bougent qu'en GAIN, sur
    // les ancres figees.
    // =====================================================================
    void testDrawnCurveIsTheFilterResponse()
    {
        ODVoxAudioProcessor p;
        neutraliseOthers (p);
        setActual (p, "eq_on", 1.0f);

        // Des gains representatifs sur chaque bande, comme un geste les ecrit.
        struct Target { int band; float gainDb; };
        const Target targets[] =
        {
            { 0,  6.0f },
            { 1, -5.0f },
            { 2, -4.0f },
            { 3,  5.0f },
        };

        odvox::EqCurveModel model (p, kWidth, kHeight);

        for (const auto& t : targets)
            setActual (p, kBandGainIds[t.band], t.gainDb);

        p.prepareToPlay (kSampleRate, 512);

        const auto settings = model.settings();
        const float sr = (float) kSampleRate;

        float worst = 0.0f;

        // 16 kHz est dans la liste pour l'Air : c'est la seule frequence ou un
        // shelf haut atteint son plateau, donc la seule ou l'oublier se verrait.
        for (const float freq : { 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f,
                                  4000.0f, 8000.0f, 12000.0f, 16000.0f })
        {
            const float measured = gainAt (p, freq);
            const float drawn = odvox::Eq::responseDb (settings, sr, freq);
            const float error = std::abs (measured - drawn);

            worst = juce::jmax (worst, error);

            expect (error <= 0.5f,
                    "a " + juce::String (freq, 0) + " Hz : mesuree "
                        + juce::String (measured, 2) + " dB vs courbe tracee "
                        + juce::String (drawn, 2) + " dB (ecart "
                        + juce::String (error, 2) + " dB)");
        }

        expectWithinAbsoluteError (model.gainOf (3), 5.0f, 0.01f,
                                   "le modele lit bien le gain ecrit pour l'Air");

        // Et le modele montre les ANCRES : sa frequence pour chaque bande est la
        // constante figee, jamais autre chose.
        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
            expectWithinAbsoluteError (model.frequencyOf (b), odvox::Eq::kAnchorHz[b], 0.01f,
                                       "le modele place la bande " + juce::String (b)
                                           + " sur son ancre figee");

        juce::Logger::writeToLog ("Courbe d'EQ : pire ecart trace/mesure = "
                                  + juce::String (worst, 3) + " dB");
    }

    // =====================================================================
    // AC1 de US-04, le geste : on saisit une poignee, on la relache ailleurs,
    // et le GAIN doit avoir suivi — y compris la LECTURE inverse (le point vise,
    // pas la poignee d'origine).
    //
    // rev. suppression du mode Avance : l'axe X ne s'ecrit plus (bandes
    // pitch-suiveuses sur ancres figees), le geste ne porte que le gain.
    // =====================================================================
    void testDragWritesGain()
    {
        ODVoxAudioProcessor p;
        odvox::EqCurveModel model (p, kWidth, kHeight);

        constexpr int band = 1;   // Mid

        // Point de depart : la poignee elle-meme (donc forcement saisissable).
        const auto from = model.handlePosition (band);

        expect (model.bandAt (from) == band,
                "la poignee de la bande Mid est saisissable en son centre");

        expect (model.beginGesture (from), "le geste s'ouvre sur la poignee");

        const juce::Point<float> to { 0.42f * (float) kWidth, 0.25f * (float) kHeight };

        expect (model.dragTo ({ to.x, to.y }), "le geste ecrit en glissant");
        model.endGesture();

        const float wantedDb = odvox::eqcurve::gainForY (to.y, (float) kHeight);

        expectWithinAbsoluteError (actualOf (p, kBandGainIds[band]), wantedDb, 0.05f,
                                   "gain ecrit par le geste : "
                                       + juce::String (actualOf (p, kBandGainIds[band]), 2)
                                       + " dB");

        // La bande visee est bien celle-la, et les autres n'ont pas bouge.
        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
            if (b != band)
                expectWithinAbsoluteError (actualOf (p, kBandGainIds[b]), b == 3 ? 2.5f : 0.0f, 0.01f,
                                           "la bande " + juce::String (b) + " n'a pas ete touchee");

        // Et le geste est bien ferme : un second relachement n'ecrit rien.
        expect (! model.isDragging(), "le geste est ferme apres le relachement");
        expect (! model.dragTo ({ 10.0f, 10.0f }), "et un glissement sans geste n'ecrit rien");
        expectWithinAbsoluteError (actualOf (p, kBandGainIds[band]), wantedDb, 0.05f,
                                   "le gain n'a pas ete modifie par le glissement orphelin");
    }

    // =====================================================================
    // rev. suppression du mode Avance : molette (Q) et menu (type) sont des
    // no-op — Q et type sont figes par bande. Le contrat est qu'ils n'ecrivent
    // PLUS RIEN plutot qu'ils plantent.
    // =====================================================================
    void testWheelAndMenuAreNoOps()
    {
        ODVoxAudioProcessor p;
        odvox::EqCurveModel model (p, kWidth, kHeight);

        constexpr int band = 1;   // Mid

        expect (! model.adjustQ (band, 1.0f), "la molette n'ecrit plus rien");
        expect (! model.adjustQ (band, -1.0f), "ni vers le bas");
        expect (! model.setBandType (band, 2), "le menu n'ecrit plus rien");
        expect (! model.setBandType (band, 9), "un type hors catalogue reste refuse");
        expect (! model.adjustQ (9, 1.0f), "une bande inexistante reste refusee");

        // Et les valeurs figees du modele sont celles du module.
        expectWithinAbsoluteError (model.qOf (band), odvox::Eq::kAnchorQ[band], 0.001f,
                                   "le Q du modele est l'ancre figee");
        expect (model.typeOf (band) == odvox::Eq::kAnchorType[band],
                "le type du modele est l'ancre figee");
    }

    // =====================================================================
    // AC1 de US-04, bout en bout : apres un GESTE, la reponse REELLEMENT mesuree
    // suit la courbe tracee a ±0,5 dB. C'est le critere qui interdit une
    // interface qui bougerait une courbe sans bouger le son.
    // =====================================================================
    void testMeasuredResponseFollowsTheGesture()
    {
        ODVoxAudioProcessor p;
        neutraliseOthers (p);
        setActual (p, "eq_on", 1.0f);

        // Tout est neutre au depart, SAUF l'Air (defaut mesure du catalogue).
        for (int b = 0; b < 4; ++b)
            setActual (p, kBandGainIds[b], 0.0f);

        odvox::EqCurveModel model (p, kWidth, kHeight);

        // Deux gestes : le Mid qu'on creuse, l'Air qu'on leve. Le X vise est
        // IGNORE (ancres figees) : seul le Y porte le geste.
        const auto gesture = [&model] (int band, float x01, float y01)
        {
            model.setSize (kWidth, kHeight);
            const auto target = juce::Point<float> (x01 * (float) kWidth, y01 * (float) kHeight);

            if (! model.beginGesture (model.handlePosition (band)))
                return false;

            model.dragTo ({ target.x, target.y });
            model.endGesture();

            return true;
        };

        expect (gesture (1, 0.30f, 0.75f), "premier geste (Mid creuse)");
        expect (gesture (3, 0.68f, 0.28f), "second geste (Air leve)");

        p.prepareToPlay (kSampleRate, 512);

        const auto settings = model.settings();

        float worst = 0.0f;

        for (const float freq : { 100.0f, 250.0f, 400.0f, 1000.0f, 2000.0f, 3500.0f, 6000.0f })
        {
            const float measured = gainAt (p, freq);
            const float drawn = odvox::Eq::responseDb (settings, kSampleRate, freq);
            const float error = std::abs (measured - drawn);

            worst = juce::jmax (worst, error);

            expect (error <= 0.5f,
                    "apres le geste, a " + juce::String (freq, 0) + " Hz : mesuree "
                        + juce::String (measured, 2) + " dB vs courbe " + juce::String (drawn, 2)
                        + " dB (ecart " + juce::String (error, 2) + " dB)");
        }

        // Et le geste n'est pas un no-op : les deux bandes visees ont bien bouge.
        expect (model.gainOf (1) < -3.0f,
                "le geste a bien creuse le Mid (" + juce::String (model.gainOf (1), 2) + " dB)");
        expect (model.gainOf (3) > 3.0f,
                "le geste a bien leve l'Air (" + juce::String (model.gainOf (3), 2) + " dB)");

        juce::Logger::writeToLog ("Courbe d'EQ apres geste : pire ecart = "
                                  + juce::String (worst, 3) + " dB");
    }

    // =====================================================================
    // Cas limites : un clic dans le vide ne saisit RIEN (sinon la courbe
    // deviendrait un piege), et un geste qui vise trop haut ou trop bas est
    // borne a ±15 dB — le meme contrat que les rotaries.
    // =====================================================================
    void testHitTestingAndClamping()
    {
        ODVoxAudioProcessor p;
        odvox::EqCurveModel model (p, kWidth, kHeight);

        // Coin superieur gauche : aucune poignee n'y est (la plus a gauche, le
        // Low, est a 120 Hz par defaut, gain +0 dB — donc ni au bord ni en haut).
        expect (model.bandAt ({ 1.0f, 1.0f }) < 0,
                "un clic dans le vide ne saisit aucune bande");
        expect (! model.beginGesture ({ 1.0f, 1.0f }),
                "et il n'ouvre aucun geste");

        // Chaque poignee est saisissable, et c'est bien SA bande qui est rendue.
        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        {
            const auto handle = model.handlePosition (b);

            // On decale le point d'un pixel : la saisie doit tolerer la main.
            const auto near = juce::Point<float> (handle.x + 1.0f, handle.y - 1.0f);

            expect (model.bandAt ({ near.x, near.y }) == b,
                    "la bande " + juce::String (b) + " est saisissable a un pixel de sa poignee");
        }

        // Un geste qui vise HORS des bornes : le gain est borne a ±15 dB (les
        // bornes de l'affichage). On force une position extreme pour le verifier.
        expect (model.beginGesture (model.handlePosition (0)), "geste sur le Low");

        model.dragTo ({ -500.0f, -500.0f });   // tres a gauche, tres en haut
        model.endGesture();

        expectWithinAbsoluteError (actualOf (p, kBandGainIds[0]), 15.0f, 0.01f,
                                   "tirer en haut s'arrete a +15 dB");

        expect (model.beginGesture (model.handlePosition (3)), "geste sur l'Air");
        model.dragTo ({ 5000.0f, 5000.0f });   // tres a droite, tres en bas
        model.endGesture();

        expectWithinAbsoluteError (actualOf (p, kBandGainIds[3]), -15.0f, 0.01f,
                                   "tirer en bas s'arrete a -15 dB");
    }

    // =====================================================================
    // Double-clic : une bande revient au defaut du catalogue — meme regle que
    // les rotaries (AC2 de US-02). Le modele lit ce defaut dans le catalogue,
    // et le test l'y relit : deux verites ne peuvent pas diverger ici.
    // =====================================================================
    void testResetBand()
    {
        ODVoxAudioProcessor p;
        odvox::EqCurveModel model (p, kWidth, kHeight);

        constexpr int band = 3;   // Air : le seul dont le defaut de gain n'est pas 0

        setActual (p, kBandGainIds[band], -9.0f);

        expect (model.resetBand (band), "le double-clic remet la bande a son defaut");

        float wanted = 0.0f;

        for (const auto& info : odvox::params::declared())
            if (juce::String (kBandGainIds[band]) == juce::String (info.id))
                wanted = info.defaultValue;

        expectWithinAbsoluteError (actualOf (p, kBandGainIds[band]), wanted, 0.01f,
                                   juce::String (kBandGainIds[band]) + " : defaut du catalogue ("
                                       + juce::String (wanted, 2) + ")");

        // Et le defaut de l'Air est bien +2,5 dB : c'est lui qui porte le contrat
        // de transparence amende du §3.4.
        expectWithinAbsoluteError (model.gainOf (band), 2.5f, 0.01f,
                                   "l'Air revient a +2,5 dB, sa valeur mesuree");

        expect (! model.resetBand (9), "une bande inexistante est refusee");
    }
};

static EqCurveTests eqCurveTests;
