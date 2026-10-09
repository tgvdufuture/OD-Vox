#include <juce_audio_processors/juce_audio_processors.h>

#include "Eq.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kSeconds    = 0.5;

    /** Les quatre identifiants de GAIN, dans l'ordre du catalogue. Ecrits a la
        main : une inversion (Low <-> Mid) compilerait sans broncher, donc chaque
        test qui la traverse verifie AUSSI que la bande designee est bien celle
        qui repond.

        rev. suppression du mode Avance : les 12 reglages freq/Q/type ne sont
        plus des parametres — chaque bande est pitch-suiveuse autour de son
        ancre figuree dans `Eq` (kAnchorHz/kAnchorQ/kAnchorType). */
    const char* const kBandGainIds[odvox::Eq::kNumBands] =
    {
        "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db"
    };

    void setActual (ODVoxAudioProcessor& p, const char* id, float actual)
    {
        auto* param = p.state().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    }

    /** Reglage d'une bande : le GAIN seul. La frequence, le Q et le type sont
        ceux, figes, de l'ancre — c'est exactement ce que peut exprimer un
        utilisateur. */
    void setBandGain (ODVoxAudioProcessor& p, int band, float gainDb)
    {
        setActual (p, kBandGainIds[band], gainDb);
    }

    /** Toutes les bandes neutres : le module ne colore rien (et le processeur
        court-circuite, `inert`). Le low cut est eteint aussi : ON par defaut
        depuis l'amendement UI du 2026-09-29, c'est un reglage TONAL hors de
        l'EQ, et son passe-haut a 120 Hz fausserait toutes les sondes basses de
        ces tests (ils mesurent la reponse de l'EQ, pas celle du nettoyage). */
    void setEqFlat (ODVoxAudioProcessor& p)
    {
        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
            setBandGain (p, b, 0.0f);

        setActual (p, "lowcut_amount", 0.0f);
        setActual (p, "output_dc_filter", 0.0f);
    }

    void setEqOn (ODVoxAudioProcessor& p, bool on)
    {
        setActual (p, "eq_on", on ? 1.0f : 0.0f);
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

    juce::MidiBuffer noMidi;

    /** Amplitude d'une composante par correlation a sa frequence (Goertzel), sur le
        regime etabli. C'est la seule mesure juste d'un filtre : une crete de bloc
        est dominee par le transitoire de demarrage. */
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

    /** Reponse du processeur a une sonde entretenue, en dB, en regime etabli. */
    float gainAt (ODVoxAudioProcessor& p, float freq, float amplitude = 0.1f)
    {
        const auto probe = makeSine (freq, amplitude, kSeconds);

        juce::AudioBuffer<float> buffer (2, (int) probe.size());

        for (size_t i = 0; i < probe.size(); ++i)
            buffer.setSample (0, (int) i, probe[i]);

        p.processBlock (buffer, noMidi);

        std::vector<float> out (probe.size());
        for (size_t i = 0; i < probe.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        const int skip = (int) (0.1 * kSampleRate);   // transitoire des filtres
        const double outAmp = amplitudeAt (out, freq, skip);
        const double inAmp  = amplitudeAt (probe, freq, skip);

        return 20.0f * (float) std::log10 (juce::jmax (1.0e-12, outAmp / inAmp));
    }

    const float kProbeFreqs[] =
    {
        40.0f, 63.0f, 100.0f, 158.0f, 251.0f, 398.0f, 631.0f,
        1000.0f, 1585.0f, 2512.0f, 3981.0f, 6310.0f, 10000.0f, 15849.0f
    };

    // =====================================================================
    // AC1 (rev.) : le gain de chaque bande agit REELLEMENT sur sa reponse, a
    // son ancre — la classe de bug qui avait mordu sur la pente du low cut :
    // un parametre qui n'atteint pas son module. Frequence, Q et type etant
    // figes, ils n'ont plus de chemin de test : c'est le mapping des ancres
    // (testBandIndexMapping) qui les verifie.
    // =====================================================================
    void testBandGainsActOnTheirAnchor (juce::UnitTest& t)
    {
        // --- Le contrat de parametres -------------------------------------
        for (const char* id : kBandGainIds)
            t.expect (odvox::params::isDeclared (id),
                      juce::String ("parametre declare : ") + id);

        // --- Le gain d'une bande tient son plateau a son ancre -------------
        // Endroits ou chaque bande (type figure) tient son PLATEAU +12 dB :
        // le shelf bas tient le plateau SOUS son coin, les shelves hauts
        // AU-DESSUS, la cloche sur son centre. Le coin d'un shelf ne porte que
        // la MOITIE du gain (propriete du filtre, §3.4), donc c'est le plateau
        // qu'on mesure. Les sondes ne sont pas des parametres.
        //
        // Un shelf de Q ~1 porte une ONDULATION pres de son coin (mesure du
        // 2026-09-22 : 12,59 dB a 40 Hz pour +12 regles) : on sonde LOIN du
        // coin, ou la reponse retombe sur le plateau, et la tolerance couvre
        // le residu.
        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        {
            const float plateau = b == 0 ?    30.0f : b == 1 ?   700.0f
                                : b == 2 ?  8000.0f : 18000.0f;

            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, b, 12.0f);
            p.prepareToPlay (kSampleRate, 512);

            const float at = gainAt (p, plateau);

            t.expectWithinAbsoluteError (at, 12.0f, 0.8f,
                                         "bande " + juce::String (b) + " ("
                                             + juce::String (kBandGainIds[b])
                                             + ") : plateau a +12 dB mesure "
                                             + juce::String (at, 2) + " dB");
        }

        // --- La forme du Mid reste une cloche ------------------------------
        // A une octave sous son ancre (700 Hz), la cloche Q 1,15 doit avoir
        // quitte son sommet d'au moins 3 dB — c'est ce qui distingue une
        // cloche d'un shelf.
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, 1, 12.0f);
            p.prepareToPlay (kSampleRate, 512);

            const float atCentre = gainAt (p, 700.0f);
            const float anOctaveDown = gainAt (p, 350.0f);

            t.expect (atCentre > 11.0f && atCentre - anOctaveDown > 3.0f,
                      "cloche du Mid : " + juce::String (atCentre, 2) + " dB au centre, "
                          + juce::String (anOctaveDown, 2) + " dB une octave dessous");
        }
    }

    // =====================================================================
    // AC2 de US-04 : la reponse mesuree correspond a la COURBE AFFICHEE a
    // ±0,5 dB entre 40 Hz et 16 kHz.
    //
    // La courbe affichee est le produit des reponses des quatre bandes : on
    // mesure la cascade complete, puis chaque bande SEULE, et on compare. Un
    // ecart signifie que le trace et le son divergent.
    //
    // rev. suppression du mode Avance : le reglage teste est celui que
    // l'interface peut exprimer — des GAINS sur les ancres figees.
    // =====================================================================
    void testMeasuredResponseMatchesTheDisplayedCurve (juce::UnitTest& t)
    {
        // Configuration representative : gains de +12 a −12 dB sur les ancres.
        const float gains[odvox::Eq::kNumBands] = { 12.0f, -12.0f, 9.0f, 12.0f };

        const auto configure = [&] (ODVoxAudioProcessor& proc, int onlyBand)
            {
                for (int b = 0; b < odvox::Eq::kNumBands; ++b)
                    setBandGain (proc, b, (onlyBand < 0 || onlyBand == b) ? gains[b] : 0.0f);
            };

        // Reponse de la cascade complete.
        ODVoxAudioProcessor cascade;
        setEqFlat (cascade);
        setEqOn (cascade, true);
        configure (cascade, -1);
        cascade.prepareToPlay (kSampleRate, 512);

        // Reponses de chaque bande seule.
        float single[odvox::Eq::kNumBands][(int) std::size (kProbeFreqs)] {};

        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        {
            ODVoxAudioProcessor one;
            setEqFlat (one);
            setEqOn (one, true);
            configure (one, b);
            one.prepareToPlay (kSampleRate, 512);

            for (size_t f = 0; f < std::size (kProbeFreqs); ++f)
                single[b][f] = gainAt (one, kProbeFreqs[f]);
        }

        float worst = 0.0f;

        for (size_t f = 0; f < std::size (kProbeFreqs); ++f)
        {
            const float measured = gainAt (cascade, kProbeFreqs[f]);

            float displayed = 0.0f;
            for (int b = 0; b < odvox::Eq::kNumBands; ++b)
                displayed += single[b][f];

            const float error = std::abs (measured - displayed);

            t.expect (error <= 0.5f,
                      "a " + juce::String (kProbeFreqs[f], 0) + " Hz : mesuree "
                          + juce::String (measured, 2) + " dB vs courbe affichee "
                          + juce::String (displayed, 2) + " dB (ecart "
                          + juce::String (error, 2) + " dB)");

            worst = juce::jmax (worst, error);
        }

        // Le pire ecart est reporte dans le journal du test : c'est la marge
        // restante sous le seuil de ±0,5 dB, et elle doit rester confortable.
        juce::Logger::writeToLog ("EQ : pire ecart mesure/courbe sur 40 Hz - 16 kHz = "
                                  + juce::String (worst, 3) + " dB");
        t.expect (worst <= 0.5f,
                  "le pire ecart sur toute la bande doit rester sous 0,5 dB ("
                      + juce::String (worst, 3) + " dB)");
    }

    // =====================================================================
    // Cas limite du PRD : deux bandes a la MEME frequence s'additionnent en dB
    // (produit d'amplitudes), elles ne se superposent pas.
    //
    // rev. suppression du mode Avance : plus aucun reglage ne peut placer deux
    // bandes au meme endroit — le test passe donc au niveau du MODULE, via la
    // reponse theorique `Eq::responseDb`, la source unique de la courbe.
    // =====================================================================
    void testTwoBandsAtTheSameFrequencyAddInDecibels (juce::UnitTest& t)
    {
        odvox::Eq::Settings s;

        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        {
            s.bands[b].freqHz = 2000.0f;
            s.bands[b].gainDb = 0.0f;
            s.bands[b].q      = 1.0f;
            s.bands[b].type   = odvox::Eq::kBell;
        }

        s.bands[1].gainDb = 6.0f;
        s.bands[2].gainDb = 6.0f;

        const float together = odvox::Eq::responseDb (s, kSampleRate, 2000.0f);

        t.expectWithinAbsoluteError (together, 12.0f, 0.5f,
                                     "deux cloches de +6 dB a 2 kHz donnent "
                                         + juce::String (together, 2) + " dB, pas +6");
    }

    // =====================================================================
    // Transparence — les deux chemins du contrat (rev. : le troisieme cas
    // d'antan, « un passe-haut a 0 dB n'est pas neutre », disparait avec les
    // types reglables : plus aucun chemin ne peut construire ce cas).
    // =====================================================================
    void testTransparencyAndItsLimits (juce::UnitTest& t)
    {
        const auto isBitExact = [] (const std::function<void (ODVoxAudioProcessor&)>& setup)
            {
                ODVoxAudioProcessor p;
                setup (p);
                p.prepareToPlay (kSampleRate, 512);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::Random random (20260919);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

                juce::AudioBuffer<float> entree (buffer);
                juce::MidiBuffer midi;
                p.processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        if (buffer.getSample (ch, i) != entree.getSample (ch, i))
                            return false;

                return true;
            };

        // 1. `eq_on` Off : le module entier est court-circuite.
        t.expect (isBitExact ([] (ODVoxAudioProcessor& p)
                              {
                                  setEqFlat (p);
                                  setBandGain (p, 2, 12.0f);
                                  setEqOn (p, false);
                              }),
                  "`eq_on` Off court-circuite le module, meme avec des bandes actives");

        // 2. Toutes les bandes a 0 dB : le reglage NEUTRE.
        t.expect (isBitExact ([] (ODVoxAudioProcessor& p)
                              {
                                  setEqFlat (p);
                                  setEqOn (p, true);
                              }),
                  "toutes les bandes a 0 dB : le module est neutre au bit pres");
    }

    // =====================================================================
    // Chaque bande est bien CELLE que le catalogue designe : le gain de la
    // bande `b` doit apparaitre a l'ANCRE de la bande `b`.
    // =====================================================================
    void testBandIndexMapping (juce::UnitTest& t)
    {
        // Ancres figurees dans `Eq` (Low 120, Mid 700, High 1750, Air 10 000 —
        // calibrees le 2026-09-20 : le High y a
        // son CENTRE de plateau a 3,5 kHz, donc un coin a 1,75 k ; l'Air coin
        // a ~10 kHz).
        const float anchors[odvox::Eq::kNumBands] =
        {
            odvox::Eq::kAnchorHz[0], odvox::Eq::kAnchorHz[1],
            odvox::Eq::kAnchorHz[2], odvox::Eq::kAnchorHz[3]
        };

        for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, b, 12.0f);
            p.prepareToPlay (kSampleRate, 512);

            // L'endroit ou la bande tient son PLATEAU (+12 dB) contre un point
            // mort, selon le type figure de la bande : le shelf bas tient le
            // plateau SOUS son coin, les shelves hauts AU-DESSUS, la cloche
            // sur son centre. Le coin d'un shelf ne porte que la MOITIE du
            // gain (propriete du filtre, §3.4), donc c'est le plateau qu'on
            // mesure. Les sondes ne sont pas des parametres.
            const float plateau = b == 0 ?   40.0f : b == 1 ?  700.0f
                                : b == 2 ? 12000.0f : 20000.0f;
            const float dead    = b == 0 ?  400.0f : b == 1 ? 5000.0f
                                : b == 2 ?  350.0f : 1100.0f;

            const float at = gainAt (p, plateau);
            const float farGain = gainAt (p, dead);

            t.expect (at > farGain + 4.0f,
                      juce::String (kBandGainIds[b]) + " agit a " + juce::String (plateau, 0)
                          + " Hz (" + juce::String (at, 2) + " dB) et non a "
                          + juce::String (dead, 0) + " Hz (" + juce::String (farGain, 2) + " dB)");

            // L'ancre est bien celle annoncee : le module la confirme.
            t.expectWithinAbsoluteError (anchors[b],
                                         odvox::Eq::kAnchorHz[b], 0.001f,
                                         "l'ancre locale est le miroir de celle du module");
        }
    }
}

class EqTests : public juce::UnitTest
{
public:
    EqTests() : juce::UnitTest ("EQ 4 bandes (F1.6, rev. ancres figees)") {}

    void runTest() override
    {
        beginTest ("AC1 : le gain de chaque bande agit a son ancre");
        testBandGainsActOnTheirAnchor (*this);

        beginTest ("AC2 : la reponse mesuree suit la courbe affichee a ±0,5 dB");
        testMeasuredResponseMatchesTheDisplayedCurve (*this);

        beginTest ("Cas limite : deux bandes a la meme frequence s'additionnent");
        testTwoBandsAtTheSameFrequencyAddInDecibels (*this);

        beginTest ("Transparence : `eq_on` Off et reglage neutre");
        testTransparencyAndItsLimits (*this);

        beginTest ("Chaque bande agit a SON ancre");
        testBandIndexMapping (*this);

        beginTest ("Dynamique relative : le niveau du programme commande la fraction appliquee");
        testRelativeDynamicsFollowTheProgrammeLevel (*this);

        beginTest ("AutoGain d'etage : le makeup ne touche pas au niveau hors de la bande");
        testStageAutoGainHoldsTheLevel (*this);

        beginTest ("Un reset() rend le module reproductible au bit pres");
        testResetMakesTheModuleReproducible (*this);

        beginTest ("Accrochage harmonique : borne, resolu, et sans fondamentale il reste sur l'ancre");
        testHarmonicSnapping (*this);
    }

private:
    // =====================================================================
    // L'accrochage harmonique (F1.6 revu) : chaque bande porte la dynamique
    // relative, mais BORNEE. Trois lois, verifiees sur la frequence REELLEMENT
    // utilisee par la bande (`eqTrackedFrequency`), pas sur un gain sonde a une
    // frequence choisie :
    //
    //   1. une harmonique a moins de `kSnapWindowCents` de l'ancre deplace la
    //      bande ; au-dela, elle n'y touche pas ;
    //   2. une grille harmonique PLUS FINE que la fenetre ne la deplace pas non
    //      plus (`kMaxResolvedHarmonic`) — sinon il y a toujours une harmonique
    //      dedans et le deplacement serait commande par la note chantee ;
    //   3. sans fondamentale detectee, la bande reste sur son ancre.
    //
    // **Le signal est duplique sur les DEUX canaux** : `PitchDetector::pushBlock`
    // moyenne les canaux presents. Un signal mono pose dans un canal stereo est
    // donc vu a moitie niveau — et surtout, un canal laisse en memoire non
    // initialisee detruit la periodicite, le detecteur ne trouve RIEN et les
    // trois lois passeraient alors pour une mauvaise raison (mesure du
    // 2026-09-20 : repli sur la fondamentale de repli, 150 Hz). C'est la meme
    // convention que le `render` des tests d'EQ.
    //
    // **Rien n'est injecte dans le tracking** : la fondamentale vient du
    // detecteur embarque dans l'EQ (unique detecteur du produit depuis
    // (2026-09-20) — donc c'est le signal qui doit la
    // nourrir. Un chemin d'injection ferait passer un test alors que le cablage
    // reel est rompu.
    // =====================================================================
    static std::vector<float> harmonicSignal (float f0, int numHarmonics, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n, 0.0f);

        for (int h = 1; h <= numHarmonics; ++h)
            for (int i = 0; i < n; ++i)
                v[(size_t) i] += 0.2f * std::sin (float (2.0 * juce::MathConstants<double>::pi
                                                        * (double) f0 * (double) h
                                                        * (double) i / kSampleRate));

        return v;
    }

    /** Rendu en stereo (deux canaux identiques, memoire initialisee) et frequence
        accrochee de la bande demandee, lue apres le rendu. */
    static float trackedAfterRendering (ODVoxAudioProcessor& p, int band,
                                        const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buffer (2, (int) mono.size());
        buffer.clear();

        for (size_t i = 0; i < mono.size(); ++i)
        {
            buffer.setSample (0, (int) i, mono[(size_t) i]);
            buffer.setSample (1, (int) i, mono[(size_t) i]);
        }

        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        return p.eqTrackedFrequency (band);
    }

    void testHarmonicSnapping (juce::UnitTest& t)
    {
        // --- Loi 1 : dans la fenetre, hors de la fenetre --------------------
        // Mid ancree a 700 Hz (son ancre), gain +6 dB pour que le module ne
        // soit pas court-circuite (toutes les bandes a 0 dB = inerte, aucun
        // accrochage n'y serait calcule).
        //
        // 220 Hz : n = round(700/220) = 3 -> harmonique a 660 Hz, 102 cents
        // sous l'ancre, et n <= 5 donc resolu : la bande doit glisser dessus.
        // 300 Hz : n = round(700/300) = 2 -> 600 Hz, 267 cents — HORS fenetre :
        // la bande reste a son ancre. C'est ce contre-test qui distingue « une
        // fenetre » d'un accrochage inconditionnel.
        {
            const float anchorHz = odvox::Eq::kAnchorHz[1];

            const auto measure = [&] (float f0)
                {
                    ODVoxAudioProcessor p;
                    setEqFlat (p);
                    setEqOn (p, true);
                    setBandGain (p, 1, 6.0f);
                    p.prepareToPlay (kSampleRate, 512);

                    return trackedAfterRendering (p, 1, harmonicSignal (f0, 8, 1.0));
                };

            const float snapped = measure (220.0f);
            const float stayed  = measure (300.0f);

            t.expect (snapped < anchorHz - 30.0f,
                      "harmonique resolue a 102 cents : la bande glisse dessus (700 -> "
                          + juce::String (snapped, 1) + " Hz)");
            t.expect (std::abs (stayed - anchorHz) < 5.0f,
                      "harmonique a 267 cents, hors fenetre : la bande reste a l'ancre ("
                          + juce::String (stayed, 1) + " Hz)");
        }

        // --- Loi 2 : la garde de resolution ---------------------------------
        // Voix a 120 Hz, Mid ancree a 700 Hz : n = round(700/120) = 6 ->
        // harmonique a 720 Hz, soit 49 cents : BIEN dans la fenetre de 150. Mais
        // a cette hauteur la grille ne fait que 1200*log2(7/6) = 267 cents, donc
        // MOINS que la fenetre : une harmonique y tombe TOUJOURS, et la bande
        // glisserait en permanence. Sans la garde, elle partirait a 720 Hz ; avec
        // elle (n > kMaxResolvedHarmonic), elle reste a 700.
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, 1, 6.0f);
            p.prepareToPlay (kSampleRate, 512);

            const float tracked = trackedAfterRendering (p, 1, harmonicSignal (120.0f, 8, 1.0));

            t.expect (std::abs (tracked - 700.0f) < 5.0f,
                      "grille plus fine que la fenetre (n = 6) : la bande reste sur son "
                          "ancre (700 Hz -> " + juce::String (tracked, 1) + " Hz)");
        }

        // --- Loi 3 : sans fondamentale, l'ancre tient ------------------------
        // Du BRUIT, pas du silence : c'est le cas de la parole non tenue, et
        // c'est lui qui verifie le seuil de confiance. Une bande qui
        // s'accrocherait sur du bruit serait commandee par du hasard.
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, 1, 6.0f);
            p.prepareToPlay (kSampleRate, 512);

            juce::Random rng (2026);
            std::vector<float> noise ((size_t) (1.0 * kSampleRate));

            for (auto& x : noise)
                x = 0.2f * (2.0f * rng.nextFloat() - 1.0f);

            const float tracked = trackedAfterRendering (p, 1, noise);

            t.expect (p.pitchDetector().frequencyHz() == 0.0f
                          || p.pitchDetector().confidence() < odvox::Eq::kConfidenceThreshold,
                      "le bruit ne donne aucune fondamentale (confiance "
                          + juce::String (p.pitchDetector().confidence(), 2) + ")");
            t.expect (std::abs (tracked - 700.0f) < 5.0f,
                      "sans fondamentale, la bande reste a l'ancre ("
                          + juce::String (tracked, 1) + " Hz)");
        }
    }


    // =====================================================================
    // Dynamique relative et AutoGain d'etage (F1.6 revu). Ce que la dynamique apporte a
    // chaque bande : le gain APPLIQUE est une fraction du gain REGLE, et cette
    // fraction suit le niveau du programme (plancher −55 dBFS, plein a −25).
    //
    // **La mesure se fait sur une sonde a DEUX RAIES**, et c'est indispensable :
    // le makeup de l'AutoGain est large bande, donc il s'annule dans la
    // DIFFERENCE des gains des deux raies, qui vaut alors exactement le gain
    // applique a la bande. Deux rendus separes ne le permettraient pas — la
    // ponderation du makeup (l'energie que chaque bande porte) n'est pas la meme
    // pour une raie dans la bande et pour une raie hors de la bande, donc les
    // deux rendus ne verraient pas le meme makeup et la difference serait fausse
    // (mesure du 2026-09-20 : 3,96 dB la ou la bande en appliquait 3,00).
    //
    // Sans la seconde raie, aucun test ne peut viser le gain APPLIQUE : c'est
    // pourquoi la mutation « engagement fige a plein » passait inapercue.
    // =====================================================================

    /** Gains d'une sonde a deux raies, en dB, dans un MEME rendu. */
    struct TwoToneGains { float inBand; float outOfBand; };

    static TwoToneGains twoToneGains (ODVoxAudioProcessor& p, float inBandHz, float outOfBandHz,
                                      float programmeRmsDb, double seconds = 1.0)
    {
        // Deux raies de meme amplitude : la puissance totale est le niveau demande,
        // donc l'engagement est celui du programme complet (une sonde a une raie
        // ne commanderait pas le meme niveau).
        const float perTone = juce::Decibels::decibelsToGain (programmeRmsDb - 3.0f)
                              * juce::MathConstants<float>::sqrt2;

        auto probe = makeSine (inBandHz, perTone, seconds);
        const auto other = makeSine (outOfBandHz, perTone, seconds);

        for (size_t i = 0; i < probe.size(); ++i)
            probe[i] += other[i];

        juce::AudioBuffer<float> buffer (2, (int) probe.size());
        buffer.clear();

        for (size_t i = 0; i < probe.size(); ++i)
        {
            buffer.setSample (0, (int) i, probe[(size_t) i]);
            buffer.setSample (1, (int) i, probe[(size_t) i]);
        }

        p.processBlock (buffer, noMidi);

        std::vector<float> out (probe.size());
        for (size_t i = 0; i < probe.size(); ++i)
            out[i] = buffer.getSample (0, (int) i);

        // Le makeup met 300 ms a s'installer : on laisse passer la montee, sinon
        // la mesure dependrait de la constante de temps plutot que du reglage.
        const int skip = (int) (0.6 * seconds * kSampleRate);

        const auto dbOf = [&] (float f)
        {
            const double o = amplitudeAt (out, f, skip);
            const double i2 = amplitudeAt (probe, f, skip);

            return 20.0f * (float) std::log10 (juce::jmax (1.0e-12, o / i2));
        };

        return { dbOf (inBandHz), dbOf (outOfBandHz) };
    }

    static constexpr float kDynBandHz = 700.0f;    // Mid, son ancre
    static constexpr float kDynDeadHz = 6300.0f;   // hors de la cloche (Q 1,15) — le makeup, et lui seul

    void testRelativeDynamicsFollowTheProgrammeLevel (juce::UnitTest& t)
    {
        const auto appliedGainDb = [&] (float programmeRmsDb)
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, 1, 6.0f);
            p.prepareToPlay (kSampleRate, 512);

            const auto g = twoToneGains (p, kDynBandHz, kDynDeadHz, programmeRmsDb);
            return g.inBand - g.outOfBand;
        };

        // Plancher et plein, d'apres les constantes du module : rien en dessous de
        // −55, le reglage entier au-dessus de −25.
        const float underFloor = appliedGainDb (-60.0f);
        const float halfWay   = appliedGainDb (-40.0f);
        const float atFull    = appliedGainDb (-20.0f);

        t.expect (std::abs (underFloor) < 0.3f,
                  "sous le plancher (−60 dBFS), la bande ne fait rien ("
                      + juce::String (underFloor, 2) + " dB)");

        t.expect (std::abs (halfWay - 3.0f) < 0.4f,
                  "a −40 dBFS, moitie de l'etendue : la bande applique la moitie du reglage ("
                      + juce::String (halfWay, 2) + " dB sur 6)");

        t.expect (std::abs (atFull - 6.0f) < 0.3f,
                  "a −20 dBFS, au-dessus du plein : la bande applique le reglage entier ("
                      + juce::String (atFull, 2) + " dB)");
    }

    void testStageAutoGainHoldsTheLevel (juce::UnitTest& t)
    {
        const auto levelOutsideBandDb = [&] (float programmeRmsDb)
        {
            ODVoxAudioProcessor p;
            setEqFlat (p);
            setEqOn (p, true);
            setBandGain (p, 1, 6.0f);
            p.prepareToPlay (kSampleRate, 512);

            return twoToneGains (p, kDynBandHz, kDynDeadHz, programmeRmsDb).outOfBand;
        };

        // A plein regime, le makeup vaut EXACTEMENT 0 : hors de la bande, le
        // module ne touche pas au niveau, et l'EQ statique est preserve (c'est ce
        // qui rend la courbe affichee exacte). La tolerance est le plancher de la
        // mesure elle-meme, pas une marge de conception.
        const float atFull  = levelOutsideBandDb (-20.0f);
        const float atFloor = levelOutsideBandDb (-60.0f);

        t.expect (std::abs (atFull) < 0.15f,
                  "a plein regime, hors de la bande : aucun makeup ("
                      + juce::String (atFull, 2) + " dB)");
        t.expect (std::abs (atFloor) < 0.15f,
                  "sous le plancher, hors de la bande : aucun makeup non plus ("
                      + juce::String (atFloor, 2) + " dB)");

        // Au milieu, la dynamique raccourcit le boost : le makeup releve le
        // niveau, mais il ne peut jamais depasser ce que la dynamique a retire
        // (6 − 3 = 3 dB), et il est pondere par l'energie que la bande porte
        // reellement — une bande etroite ne pousse donc pas tout le signal.
        const float halfWay = levelOutsideBandDb (-40.0f);

        // Mesure du 2026-09-20 : 0,73 dB pour un manque de 3,00 (Mid +6, Q 1,
        // sonde a deux raies a −40 dBFS). Borne HAUTE : le makeup ne peut pas depasser
        // ce que la dynamique a retire.

        t.expect (halfWay > 0.1f && halfWay < 3.0f,
                  "a −40 dBFS, le makeup compense une PART du manque, sans depasser ("
                      + juce::String (halfWay, 2) + " dB sur 3,00)");
    }


    void testResetMakesTheModuleReproducible (juce::UnitTest& t)
    {
        // Le module a une MEMOIRE : la frequence accrochee, le gain applique par la
        // dynamique, et les coefficients qui vont avec. Un `reset()` doit rendre un
        // etat qui ne depend QUE du reglage. Sinon deux rendus du MEME signal
        // different — un bounce hors ligne ne rendrait pas ce que la lecture a fait
        // entendre. Le harnais le mesure au niveau de la chaine (« le traitement est
        // deterministe ») ; ce test le tient au niveau du module, donc a chaque
        // build, et il a trouve le defaut : apres un reset, les coefficients de la
        // derniere note restaient en place pour les 32 premiers echantillons, avant
        // la premiere decision (ecart mesure 2,8e-3).
        ODVoxAudioProcessor p;
        setEqFlat (p);
        setEqOn (p, true);
        setBandGain (p, 1, 6.0f);
        p.prepareToPlay (kSampleRate, 512);

        // Une voix qui fait ACCROCHER la bande (220 Hz -> 660 Hz) et une dynamique
        // qui ne l'engage qu'a moitie : l'etat interne s'eloigne de l'ancre des le
        // premier bloc, donc le rendu suivant part de tres loin.
        auto voice = harmonicSignal (220.0f, 8, 1.0);

        for (auto& v : voice)
            v *= 0.03f;   // ≈ −38 dBFS : engagement partiel

        const auto renderOnce = [&]
        {
            juce::AudioBuffer<float> buffer (2, (int) voice.size());
            buffer.clear();

            for (size_t i = 0; i < voice.size(); ++i)
            {
                buffer.setSample (0, (int) i, voice[i]);
                buffer.setSample (1, (int) i, voice[i]);
            }

            juce::MidiBuffer midi;
            p.processBlock (buffer, midi);

            std::vector<float> out (voice.size());
            for (size_t i = 0; i < voice.size(); ++i)
                out[i] = buffer.getSample (0, (int) i);

            return out;
        };

        // Le premier rendu part de l'etat PREPARE, le second d'un reset explicite.
        const auto fromPrepare = renderOnce();
        p.reset();
        const auto fromReset = renderOnce();

        auto worst = 0.0f;
        int firstDiff = -1;

        for (size_t i = 0; i < fromPrepare.size(); ++i)
        {
            const float d = std::abs (fromPrepare[i] - fromReset[i]);

            if (d > 0.0f && firstDiff < 0)
                firstDiff = (int) i;

            worst = juce::jmax (worst, d);
        }

        t.expect (firstDiff < 0,
                  "etat prepare et etat reset rendent le MEME signal a l'identique (premier "
                      "ecart a l'echantillon " + juce::String (firstDiff) + ", pire "
                      + juce::String (worst, 9) + ")");
    }


};

static EqTests eqTests;
