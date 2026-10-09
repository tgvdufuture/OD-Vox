#include "Eq.h"

#include <cmath>
#include <complex>

namespace odvox
{
    namespace
    {
        // Les fabriques JUCE prennent un FACTEUR de gain lineaire, pas des dB.
        float gainFactorOf (float gainDb)
        {
            return std::pow (10.0f, gainDb / 20.0f);
        }

        void setBiquad (Biquad& b, const juce::IIRCoefficients& c) noexcept
        {
            b.set (c.coefficients[0], c.coefficients[1], c.coefficients[2],
                   c.coefficients[3], c.coefficients[4]);
        }

        // Les bornes de securite sont celles du traitement : la courbe et le son
        // doivent clipper au MEME endroit, sinon la courbe mentirait dans les cas
        // extremes (frequence au-dela de Nyquist, Q nul).
        double safeFreq (float freqHz, double sampleRate)
        {
            return (double) juce::jlimit (10.0f, (float) (sampleRate * 0.45), freqHz);
        }

        double safeQ (float q)
        {
            return (double) juce::jmax (0.05f, q);
        }

        /** Les coefficients d'une bande — **la source unique**. Le filtre les pose
            pour traiter, et la courbe affichee les interroge pour se tracer. Le
            gain passe en parametre plutot que d'etre lu dans la bande : c'est ce
            qui permet au filtre audible de porter le gain EFFECTIF (dynamique)
            pendant que la courbe trace le gain REGLE. */
        juce::IIRCoefficients coefficientsFor (float freqHz, float gainDb, float q,
                                               int type, double sampleRate)
        {
            const double freq = safeFreq (freqHz, sampleRate);
            const double qq = safeQ (q);
            const float gainFactor = gainFactorOf (gainDb);

            switch (type)
            {
                case Eq::kLowShelf:
                    return juce::IIRCoefficients::makeLowShelf (sampleRate, freq, qq, gainFactor);

                case Eq::kHighShelf:
                    return juce::IIRCoefficients::makeHighShelf (sampleRate, freq, qq, gainFactor);

                case Eq::kHighPass:
                    return juce::IIRCoefficients::makeHighPass (sampleRate, freq, qq);

                case Eq::kBell:
                default:
                    return juce::IIRCoefficients::makePeakFilter (sampleRate, freq, qq, gainFactor);
            }
        }

        /** |H(e^jw)| en dB, evalue depuis les coefficients (b0 b1 b2 a1 a2). */
        float magnitudeDb (const juce::IIRCoefficients& c, double sampleRate, float freqHz)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi
                             * (double) freqHz / sampleRate;
            // Coefficients en double : `IIRCoefficients` les porte en float, et
            // melanger les deux types dans une expression complexe n'est pas
            // garanti par la bibliotheque standard.
            const double b0 = (double) c.coefficients[0], b1 = (double) c.coefficients[1];
            const double b2 = (double) c.coefficients[2];
            const double a1 = (double) c.coefficients[3], a2 = (double) c.coefficients[4];

            const std::complex<double> z1 (std::cos (-w), std::sin (-w)); // z^-1
            const std::complex<double> z2 = z1 * z1;
            const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
            const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;

            const double mag = std::abs (num) / juce::jmax (1.0e-12, std::abs (den));
            return (float) (20.0 * std::log10 (juce::jmax (1.0e-12, mag)));
        }

        /** Passe-bande RBJ a gain de crete EXACTEMENT 1 — la sonde de mesure.

            Celle de JUCE (`makeBandPass`) n'a pas un gain de crete de 1 : mesuree
            a 300 Hz / 48 kHz / Q = 1, elle vaut 0,59. Une sonde 40 % trop
            basse ferait croire a une bande non alimentee alors qu'elle l'est. */
        void setUnitPeakBandPass (Biquad& b, double sampleRate, double freq, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
            const double alpha = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + alpha;

            b.set ((float) (alpha / a0), 0.0f, (float) (-alpha / a0),
                   (float) (-2.0 * std::cos (w) / a0), (float) ((1.0 - alpha) / a0));
        }

        /** Coefficient d'un pôle unique, exprime en TEMPS pour que le module sonne
            pareil quel que soit le format de bloc de l'hote. `interval` permet de
            le calculer pour une decision prise tous les N echantillons. */
        float onePoleCoef (float ms, double sampleRate, int interval)
        {
            if (ms <= 0.0f || sampleRate <= 0.0)
                return 1.0f;

            return 1.0f - std::exp (-(float) interval / (ms * 0.001f * (float) sampleRate));
        }
    }

    // --- Geometrie : la seule definition de « la frequence d'une bande » -------

    float Eq::effectiveFreqHz (const Settings& s, int band) noexcept
    {
        const int b = juce::jlimit (0, kNumBands - 1, band);
        const float tracked = s.trackedHz[(size_t) b];

        return tracked > 0.0f ? tracked : s.bands[(size_t) b].freqHz;
    }

    float Eq::bandResponseDb (const Band& band, double sampleRate, float freqHz)
    {
        if (sampleRate <= 0.0)
            return 0.0f;

        // Au-dela de Nyquist la reponse n'existe pas : on prolonge celle du point
        // le plus haut, sinon la courbe partirait dans des valeurs imaginaires.
        const float f = juce::jlimit (1.0f, (float) (sampleRate * 0.5 - 1.0), freqHz);

        return magnitudeDb (coefficientsFor (band.freqHz, band.gainDb, band.q, band.type, sampleRate),
                            sampleRate, f);
    }

    float Eq::responseDb (const Settings& s, double sampleRate, float freqHz)
    {
        if (sampleRate <= 0.0)
            return 0.0f;

        // Bandes en SERIE : la reponse du module est le PRODUIT des amplitudes,
        // donc la SOMME des dB. Deux bandes reglees sur la meme frequence
        // s'additionnent (critere du PRD), et la courbe le montre sans le savoir.
        const float f = juce::jlimit (1.0f, (float) (sampleRate * 0.5 - 1.0), freqHz);

        float total = 0.0f;

        for (int b = 0; b < kNumBands; ++b)
            total += magnitudeDb (coefficientsFor (effectiveFreqHz (s, b), s.bands[(size_t) b].gainDb,
                                                   s.bands[(size_t) b].q, s.bands[(size_t) b].type,
                                                   sampleRate),
                                  sampleRate, f);

        return total;
    }

    // --- Cycle de vie ---------------------------------------------------------

    void Eq::prepare (double sampleRate, int numChannels)
    {
        sampleRateD = sampleRate > 0.0 ? sampleRate : 48000.0;
        numChannelsToProcess = juce::jmax (1, numChannels);

        levelCoef  = onePoleCoef (kLevelWindowMs, sampleRateD, 1);
        snapCoef   = onePoleCoef (kSnapGlideMs, sampleRateD, kControlInterval);
        makeupCoef = onePoleCoef (kAutoGainMs, sampleRateD, kControlInterval);

        // Le detecteur vit desormais dans ce module : sans cet appel, il n'a
        // jamais de taux d'analyse et ne detecte rien (mesure du 2026-09-20 :
        // les bandes restaient sur leur ancre, aucun accrochage).
        pitch.prepare (sampleRateD);

        reset();
    }
    void Eq::reset()
    {
        controlCounter   = 0;
        currentMakeupDb  = 0.0f;
        makeupGain       = 1.0f;
        makeupActive     = false;

        programmeMeanSquare = 0.0f;

        // --- Suivi de fondamentale : etat ENTIEREMENT defini ici -------------
        // Le detecteur et sa machine de suivi sont dans le module depuis
        // le 2026-09-20 : le reset les rend au silence,
        // sinon le second rendu du meme signal repartirait de la fondamentale
        // de la fin du premier (c'est exactement la classe de bug du
        // 2026-09-20, ou la frequence accrochee survivait au reset).
        pitch.reset();
        stableHz              = kFallbackHz;
        lastConfidentHz       = kFallbackHz;
        samplesSinceDetection = juce::jmax (1000000, (int) (kHoldMs + 1.0f));   // → repli immediat
        trackingValid         = false;
        fundamentalHz         = kFallbackHz;

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& st = state[(size_t) b];

            st.filter.clearState();
            st.probe.clearState();
            st.meanSquare   = 0.0f;
            st.builtType    = -1;
            st.probeBuiltHz = 0.0f;

            // --- L'etat audible doit etre ENTIEREMENT defini ici -------------
            // Ce qui a REELLEMENT casse la reproductibilite (mesure du 2026-09-20
            // au banc d'essai : « le traitement est deterministe » etait le SEUL
            // controle en echec, ecart 6,2e-4 des l'echantillon 4) : la frequence
            // ACCROCHEE survivait au `reset()`. Le deuxieme rendu du meme signal
            // repartait donc de l'ancienne position — une note deja accrochee — au
            // lieu de l'ancre, et le glissement de 50 ms qui suit differait du
            // premier rendu. Un bounce hors ligne ne rendait pas ce que la lecture
            // avait fait entendre.
            //
            // Les deux appels qui suivent ne sont PAS decisifs pour l'audio, et
            // c'est verifie par mutation : le premier echantillon de chaque bloc
            // declenche de toute facon une decision complete (`setSettings` remet
            // `controlCounter` a `kControlInterval`), donc AUCUN echantillon n'est
            // filtre avec des coefficients perimes. Ils definissent l'etat pour la
            // lecture (`trackedFrequencyHz`, `bandGainDb`) et pour ce premier tick,
            // qui n'a alors rien a rattraper.
            //
            // Le gain de depart est le reglage ENTIER — la courbe affichee — et la
            // dynamique le reprend des la premiere decision. Un changement de
            // REGLAGE, lui, garde la position acquise : c'est le role de
            // `setSettings`, pas celui de `reset`.
            st.trackedHz       = settings.bands[(size_t) b].freqHz;
            st.effectiveGainDb = settings.bands[(size_t) b].gainDb;

            rebuildFilter (b);
            rebuildProbe (b);
        }
    }

    void Eq::setSettings (const Settings& s)
    {
        const bool wasInert = settings.inert;

        settings = s;

        if (settings.inert)
            return;

        // Sortie du court-circuit : les filtres n'ont pas tourne, donc leur etat
        // date d'avant. On repart propre plutot que de rendre un residu audible.
        if (wasInert)
            reset();

        for (int b = 0; b < kNumBands; ++b)
            if (state[b].trackedHz <= 0.0f)
                state[b].trackedHz = s.bands[(size_t) b].freqHz;   // depart sur l'ancre

        // `Settings::trackedHz` est IGNORE ici : il ne sert qu'a `responseDb`,
        // c'est-a-dire a la courbe. Le module calcule sa propre frequence
        // accrochee, et le processeur la lui relit ensuite pour la tracer — une
        // seule source, jamais deux.
        //
        // La decision complete (accrochage, engagement, coefficients, makeup) est
        // prise au prochain tick de controle : c'est exactement le chemin qui suit
        // un changement de note, donc il n'existe qu'une facon de calculer un gain.
        controlCounter = kControlInterval;
    }

    void Eq::beginBlock (const float* const* channels, int numChannels, int numSamples)
    {
        if (sampleRateD <= 0.0 || numSamples <= 0)
            return;

        // Le detecteur consomme TOUJOURS le signal, meme quand l'EQ est inerte :
        // l'accrochage suit la hauteur des l'ouverture du module, sans delai.
        pitch.pushBlock (channels, juce::jmin (numChannels, numChannelsToProcess), numSamples);

        const float hz   = pitch.frequencyHz();
        const float conf = pitch.confidence();

        // Validite par bloc : sous le seuil de confiance, les bandes restent sur
        // leur ancre (comportement de l'EQ, invariant du lot F1.6 revu).
        trackingValid = hz > 0.0f && conf >= kConfidenceThreshold;

        if (trackingValid)
        {
            lastConfidentHz       = hz;
            samplesSinceDetection = 0;
        }
        else
        {
            samplesSinceDetection += numSamples;
        }

        // La fondamentale LISSEE suit la derniere detection pendant kHoldMs,
        // puis retombe sur le repli. Elle ne sert qu'a la reprise d'accrochage
        // (pas de saut depuis une valeur perimee) — quand trackingValid est
        // faux, les bandes sont de toute facon sur leur ancre.
        const int holdSamples = (int) (kHoldMs * 0.001f * (float) sampleRateD);
        const float target = samplesSinceDetection < holdSamples ? lastConfidentHz : kFallbackHz;

        // Lissage en TEMPS, pas par bloc : un hote a blocs de 4096 doit suivre
        // aussi vite qu'un hote a blocs de 512.
        const float blockMs = 1000.0f * (float) numSamples / (float) sampleRateD;
        const float glide   = 1.0f - std::exp (-blockMs / kTrackGlideMs);
        stableHz = stableHz + glide * (target - stableHz);

        fundamentalHz = juce::jlimit (60.0f, 500.0f, stableHz);
    }

    // --- Interne ---------------------------------------------------------------

    void Eq::rebuildFilter (int band)
    {
        auto& st = state[(size_t) band];
        const auto& b = settings.bands[(size_t) band];

        st.builtHz       = st.trackedHz;
        st.builtQ        = b.q;
        st.builtType     = b.type;
        st.builtGainDb   = st.effectiveGainDb;

        if (sampleRateD <= 0.0)
            return;

        setBiquad (st.filter, coefficientsFor (st.builtHz, st.effectiveGainDb, b.q, b.type, sampleRateD));
    }

    void Eq::rebuildProbe (int band)
    {
        auto& st = state[(size_t) band];

        if (sampleRateD <= 0.0)
            return;

        st.probeBuiltHz = st.trackedHz;

        setUnitPeakBandPass (st.probe, sampleRateD,
                             safeFreq (st.trackedHz, sampleRateD),
                             safeQ (settings.bands[(size_t) band].q));
    }

    void Eq::controlTick()
    {
        // --- 0. Engagement : UNE seule fraction, prise sur le niveau du
        //    PROGRAMME. Les quatre bandes la partagent, donc la reponse du module
        //    reste proportionnelle a la courbe affichee (voir le commentaire de
        //    classe : l'inverse a ete mesure, et il faisait mentir la courbe).
        const float levelDb = juce::Decibels::gainToDecibels (std::sqrt (programmeMeanSquare),
                                                              -120.0f);
        const float engagement = juce::jlimit (0.0f, 1.0f,
                                               (levelDb - kLevelFloorDb) / kLevelRangeDb);

        float deviationNum = 0.0f;

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& st = state[(size_t) b];
            const auto& band = settings.bands[(size_t) b];

            // --- 1. Accrochage harmonique, borne -----------------------------
            // La bande garde son ancre ; si une harmonique de la fondamentale
            // tombe dans la fenetre, elle s'y pose. Chaque bande fait ce calcul
            // pour elle-meme, donc independamment des autres.
            float targetHz = band.freqHz;

            if (trackingValid && fundamentalHz > 0.0f)
            {
                const float n = std::round (band.freqHz / fundamentalHz);

                // Voir `kMaxResolvedHarmonic` : au-dela, la grille harmonique
                // devient PLUS FINE que la fenetre, donc il y a toujours une
                // harmonique dedans — l'accrochage serait permanent et commande
                // par la note chantee plutot que par le reglage.
                //
                // `n >= 1` : sous la fondamentale, aucun rang a viser (n = 0
                // donnerait une harmonique a 0 Hz).
                if (n >= 1.0f && n <= (float) kMaxResolvedHarmonic)
                {
                    const float harmonic = n * fundamentalHz;
                    const float cents = 1200.0f * std::log2 (band.freqHz / harmonic);

                    if (std::abs (cents) <= kSnapWindowCents)
                        targetHz = harmonic;
                }
            }

            if (st.trackedHz <= 0.0f)
                st.trackedHz = targetHz;
            else
                st.trackedHz += (targetHz - st.trackedHz) * snapCoef;

            if (std::abs (st.trackedHz - st.probeBuiltHz) > 0.05f)
                rebuildProbe (b);

            // --- 2. Engagement : la fraction commune ---------------------------
            st.effectiveGainDb = band.gainDb * engagement;

            // --- 3. Coefficients, seulement s'ils ont bouge -------------------
            const bool changed = st.builtType != band.type
                                 || std::abs (st.builtHz - st.trackedHz) > 0.01f
                                 || std::abs (st.builtQ - band.q) > 1.0e-6f
                                 || std::abs (st.builtGainDb - st.effectiveGainDb) > 0.02f;

            if (changed)
                rebuildFilter (b);

            // --- 4. Ecart a compenser, pondere par l'energie de la bande ------
            // Le numerateur est en dB x energie : c'est la contribution de sonie
            // que la dynamique a retiree.
            deviationNum += st.meanSquare * (st.effectiveGainDb - band.gainDb);
        }

        // **Denominateur : l'ENERGIE DU PROGRAMME**, pas la somme des energies des
        // bandes. C'est la difference entre une sonie et une moyenne : un ecart de
        // gain de x dB dans une bande qui porte une fraction s de l'energie change
        // la sonie de x.s dB, et de x dB seulement si la bande porte TOUT le
        // signal. Diviser par la somme des bandes annulait le poids d'une bande
        // seule : mesure du 2026-09-20 au banc d'essai — sous le plancher, ou
        // l'engagement vaut 0 (les filtres sont donc PLATS), le module sortait
        // **+3,87 dB** de gain large bande, sans la moindre action d'EQ. Un
        // niveau qui bouge sans que rien n'agisse est un pompage, pas un AutoGain.
        //
        // L'AutoGain ne compense que l'effet de la dynamique relative : l'ecart
        // est nul quand le gain applique egale le gain regle, donc le makeup vaut
        // alors EXACTEMENT 0 et l'EQ statique est preserve.
        const float deviationDb = programmeMeanSquare > 1.0e-12f
                                      ? deviationNum / programmeMeanSquare
                                      : 0.0f;

        // **Et il est pondere par l'engagement lui-meme.** Sous le plancher,
        // l'engagement vaut 0 et les filtres sont donc PLATS : compenser la sonie
        // du reglage y produirait un gain large bande SANS la moindre action d'EQ.
        // Mesure du 2026-09-20 au banc d'essai : +3,87 dB de gain plat a −60 dBFS
        // avec un reglage de +6 dB au Mid, l'EQ ne filtrant rien. Un niveau qui
        // bouge sans que rien n'agisse est un pompage, pas un AutoGain. Pondere,
        // le makeup tend vers 0 avec l'action, et le niveau suit le reglage a
        // mesure que celui-ci s'applique.
        const float targetMakeupDb = juce::jlimit (-kAutoGainMaxDb, kAutoGainMaxDb,
                                                   -deviationDb * engagement);

        currentMakeupDb += (targetMakeupDb - currentMakeupDb) * makeupCoef;

        // Sans ce cran, un pole unique n'atteint jamais sa cible : il s'en
        // approche asymptotiquement, et « le makeup vaut exactement 0 » ne serait
        // vrai qu'a 1e-9 dB pres.
        if (std::abs (targetMakeupDb - currentMakeupDb) < 1.0e-3f)
            currentMakeupDb = targetMakeupDb;

        makeupGain   = gainFactorOf (currentMakeupDb);
        makeupActive = currentMakeupDb != 0.0f;
    }

    // --- Traitement ------------------------------------------------------------

    float Eq::processSample (float* const* channels, int sampleIndex)
    {
        if (settings.inert)
            return channels[0][sampleIndex];

        const int n = numChannelsToProcess;

        // 1) Mesures, sur l'ENTREE du module. Sur la sortie, une coupe
        //    s'auto-annulerait : plus elle coupe, moins elle mesurerait, donc
        //    moins elle couperait.
        //    - le niveau du PROGRAMME, qui commande l'engagement (canal le plus
        //      fort : un signal mono dans un buffer stereo doit lire la meme chose
        //      qu'en mono) ;
        //    - le contenu de chaque bande, qui ne sert plus qu'a PONDERER
        //      l'AutoGain d'etage.
        float programmeEnergy = 0.0f;
        float probeEnergy[kNumBands] {};

        for (int ch = 0; ch < n; ++ch)
        {
            const float x = channels[ch][sampleIndex];
            programmeEnergy = juce::jmax (programmeEnergy, x * x);

            for (int b = 0; b < kNumBands; ++b)
            {
                const float p = state[(size_t) b].probe.process (x, ch);
                probeEnergy[b] += p * p;
            }
        }

        // 2) Filtrage audible, bandes en serie.
        for (int ch = 0; ch < n; ++ch)
        {
            float x = channels[ch][sampleIndex];

            for (int b = 0; b < kNumBands; ++b)
                x = state[(size_t) b].filter.process (x, ch);

            if (makeupActive)
                x *= makeupGain;

            channels[ch][sampleIndex] = x;
        }

        // 3) Enveloppes, a chaque echantillon ; decision, tous les
        //    `kControlInterval`.
        const float scale = 1.0f / (float) n;

        programmeMeanSquare += (programmeEnergy - programmeMeanSquare) * levelCoef;

        for (int b = 0; b < kNumBands; ++b)
            state[(size_t) b].meanSquare += (probeEnergy[b] * scale - state[(size_t) b].meanSquare) * levelCoef;

        if (++controlCounter >= kControlInterval)
        {
            controlCounter = 0;
            controlTick();
        }

        return channels[0][sampleIndex];
    }

    float Eq::trackedFrequencyHz (int band) const noexcept
    {
        const int b = juce::jlimit (0, kNumBands - 1, band);
        const float tracked = state[(size_t) b].trackedHz;

        return tracked > 0.0f ? tracked : settings.bands[(size_t) b].freqHz;
    }

}
