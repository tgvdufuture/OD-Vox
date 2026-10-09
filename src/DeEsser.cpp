#include "DeEsser.h"

#include <cmath>

namespace odvox
{
    namespace
    {
        constexpr float kPi = juce::MathConstants<float>::pi;

        // Profondeur max du shelf plosive (a plosive01 = 1) : le corps de la
        // plosive se dompte bien avant -6 dB, et le low cut amont fait deja
        // le menage sur l'energie.
        constexpr float kPlosiveMaxDepthDb = -6.0f;

        // Biquads Butterworth (Q = 0.7071) : les fabriques JUCE posent deja ce
        // Q pour les passe-bas / passe-haut, et l'unite de makeLowShelf est un
        // facteur lineaire (pas des dB).
        constexpr double kShelfQ = 0.7071067811865476;
        juce::IIRCoefficients lowPassCoeffs (double sampleRate, float freq)
        {
            return juce::IIRCoefficients::makeLowPass (sampleRate, (double) freq);
        }

        juce::IIRCoefficients highPassCoeffs (double sampleRate, float freq)
        {
            return juce::IIRCoefficients::makeHighPass (sampleRate, (double) freq);
        }

        juce::IIRCoefficients lowShelfCoeffs (double sampleRate, float freq, float gainDb)
        {
            return juce::IIRCoefficients::makeLowShelf (sampleRate, (double) freq,
                                                        (double) kShelfQ,
                                                        (float) std::pow (10.0, (double) gainDb / 20.0));
        }

        float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
        float gainToDb (float g)  { return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f)); }

        // Adaptateur : IIRCoefficients -> Biquad::set (5 coefficients directs).
        void setBiquad (Biquad& b, const juce::IIRCoefficients& c) noexcept
        {
            b.set (c.coefficients[0], c.coefficients[1], c.coefficients[2],
                   c.coefficients[3], c.coefficients[4]);
        }
    }

    void DeEsser::prepare (double sampleRate, int numChannels)
    {
        sampleRateD = sampleRate;
        attackCoef        = std::exp (-1.0f / (kDetectionAttackMs * 0.001f * (float) sampleRate));
        releaseCoef       = std::exp (-1.0f / (kDetectionReleaseMs * 0.001f * (float) sampleRate));
        targetReleaseCoef = std::exp (-1.0f / (kVoiceReleaseMs * 0.001f * (float) sampleRate));

        numChannelsToProcess = juce::jmax (1, numChannels);
        reset();
    }

    void DeEsser::reset()
    {
        highStage1.clearState(); highStage2.clearState();
        lowStage1.clearState();  lowStage2.clearState();
        plosiveShelf.clearState();
        // Les deux suiveurs partent du PLANCHER (silence) : leur attaque est
        // immediate, ils se chargent donc des le premier echantillon et une
        // cible a 0 dB ferait un excedent negatif fictif au demarrage.
        detectorEnvelopeDb = kMinDetectorDb;
        smoothTargetDb     = kMinDetectorDb;
        lastReductionDb    = 0.0f;
        worstReductionDb   = 0.0f;
    }

    void DeEsser::setSettings (const Settings& s)
    {
        settings = s;

        // Le demouillage de plosives est LIE au curseur unique : un seul reglage
        // « propre » porte le de-esser et le demouillage,
        // plafonne a 20 % — la valeur qu'appliquaient deja les presets
        // d'usine. Recalcule ici pour que buildFilters voie la bonne valeur.
        plosive01Effective = juce::jmin (DeEsser::kPlosiveCap,
                                         DeEsser::kPlosiveFollowGain * settings.amount01);

        // Recalcul seulement si necessaire : setSettings arrive a chaque bloc.
        if (sampleRateD > 0.0
            && (DeEsser::kFixedCrossoverHz != builtCrossoverHz
                || plosive01Effective != builtPlosive01))
        {
            buildFilters();
        }
    }

    void DeEsser::buildFilters()
    {
        builtCrossoverHz = DeEsser::kFixedCrossoverHz;
        builtPlosive01   = plosive01Effective;
        builtPlosiveHz   = DeEsser::kFixedPlosiveHz;

        setBiquad (highStage1, highPassCoeffs (sampleRateD, DeEsser::kFixedCrossoverHz));
        setBiquad (highStage2, highPassCoeffs (sampleRateD, DeEsser::kFixedCrossoverHz));
        setBiquad (lowStage1, lowPassCoeffs (sampleRateD, DeEsser::kFixedCrossoverHz));
        setBiquad (lowStage2, lowPassCoeffs (sampleRateD, DeEsser::kFixedCrossoverHz));

        plosiveDepthDb = kPlosiveMaxDepthDb * plosive01Effective;
        plosiveShelf.identity();
        // L'etat est purge a chaque reconstruction : changer les coefficients
        // sous un etat charge ferait claquer le filtre (le shelf est statique,
        // il n'a aucune raison de garder sa memoire d'un reglage a l'autre).
        plosiveShelf.clearState();
        if (plosiveDepthDb < -0.05f)
            setBiquad (plosiveShelf, lowShelfCoeffs (sampleRateD, DeEsser::kFixedPlosiveHz, plosiveDepthDb));
    }

    float DeEsser::processSample (float* const* channels, int sampleIndex)
    {
        const int n = numChannelsToProcess;

        // --- Transparence bit-exacte (AC2 de US-02) -------------------------
        // Inerte (macro a 0 %) : les filtres sont court-circuites, pas
        // seulement le gain. La plosive liee suit amount, donc inertie
        // implique plosive muette.
        if (settings.inert)
        {
            lastReductionDb = 0.0f;
            return channels[0][sampleIndex];
        }

        // --- Deux voies LR4 par canal (2 x Butterworth 2 en cascade) --------
        float highIn[kMaxChannels];
        float lowIn[kMaxChannels];

        for (int ch = 0; ch < n; ++ch)
        {
            const float x = channels[ch][sampleIndex];
            highIn[ch] = highStage2.process (highStage1.process (x, ch), ch);
            lowIn[ch]  = lowStage2.process  (lowStage1.process  (x, ch), ch);
        }

        // --- Detection ADAPTATIVE, voie haute uniquement (AC1) --------------
        // Un seul detecteur pour tous les canaux : l'enveloppe suit le canal
        // le plus fort (attaque immediate, release 60 ms).
        float detectorDb = kMinDetectorDb;
        float voiceDb    = kMinDetectorDb;

        for (int ch = 0; ch < n; ++ch)
        {
            detectorDb = juce::jmax (detectorDb, gainToDb (std::abs (highIn[ch])));
            voiceDb    = juce::jmax (voiceDb,    gainToDb (std::abs (lowIn[ch])));
        }

        // Attaque LENTE (2 ms) et non instantanee : au demarrage du signal, la
        // bande haute (portee par la derivee) monte en un dixieme de ms alors
        // que la bande basse met quelques echantillons a se remplir. Un
        // detecteur instantane gobait ce transitoire et produisait une
        // reduction A VIDE de 3,6 dB sur la seule premiere ms d'un signal sans
        // aucune sifflante (et a chaque attaque apres un silence). En laissant
        // le transitoire passer sous l'attaque, le detecteur ne retient que le
        // contenu reellement durable — une sifflante de 50 a 200 ms, elle, est
        // vue en entier.
        if (detectorDb > detectorEnvelopeDb)
            detectorEnvelopeDb = attackCoef * detectorEnvelopeDb
                               + (1.0f - attackCoef) * detectorDb;
        else
            detectorEnvelopeDb = releaseCoef * detectorEnvelopeDb
                               + (1.0f - releaseCoef) * detectorDb;

        // --- Niveau cible ADAPTATIF : la reference vocale -------------------
        // Le corps grave (la voix sans ses sifflantes) sert de consigne : on
        // ne TUE pas la sifflante, on la ramene vers le niveau du reste de la
        // voix. Plus la voix est douce, moins la reduction est agressive.
        //
        // La cible est un SUIVEUR DE NIVEAU (attaque immediate, recouvrement
        // 25 ms), pas un lissage lent. Un lissage lent creait une reduction A
        // VIDE au demarrage : le detecteur attaque instantanement alors que la
        // cible partait encore du plancher, si bien qu'une voix SANS aucune
        // sifflante etait reduite pendant une vingtaine de ms (le cas limite du
        // PRD : pas de contenu dans la bande detectee, pas de reduction).
        if (voiceDb > smoothTargetDb)
            smoothTargetDb = voiceDb;
        else
            smoothTargetDb = targetReleaseCoef * smoothTargetDb
                           + (1.0f - targetReleaseCoef) * voiceDb;

        // --- Reduction ------------------------------------------------------
        float reductionDb = 0.0f;
        if (settings.enabled && ! settings.inert && settings.amount01 > 0.0f)
        {
            const float excess = detectorEnvelopeDb - smoothTargetDb;  // sifflante au-dessus de la voix
            if (excess > 0.0f)
                reductionDb = settings.amount01 * excess;

            // Saturation : le plafond suit l'amount puis s'arrete tot (la
            // cible de conception : −3,12 dB a 50 %, −3,64 dB a 100 %).
            // Softplus : continu a l'entree de la zone et asymptote au plafond
            // (jamais au-dela — le plafond de la cible ne descend pas plus bas).
            const float ceiling = DeEsser::kCeilingDb
                                  * juce::jmin (1.0f, settings.amount01 / DeEsser::kCeilingAmount);
            const float transition = ceiling - 3.0f * DeEsser::kSatKneeDb;
            if (reductionDb > transition)
                reductionDb = ceiling
                              - DeEsser::kSatKneeDb * std::log (
                                  1.0f + std::exp (- (reductionDb - ceiling) / DeEsser::kSatKneeDb));
        }

        lastReductionDb  = reductionDb;
        worstReductionDb = juce::jmax (worstReductionDb, reductionDb);

        // --- Application par SOMME des voies --------------------------------
        // out = g·(voie haute) + shelf(voie basse).
        //
        // La somme HP4 + LP4 est un passe-tout EXACT en biquads numeriques
        // (verifie numeriquement : ±0,000 dB de 20 Hz a 16 kHz), donc a g = 1
        // le module ne colore RIEN — c'est le split-band canonique. Et a
        // l'inverse, la "soustraction de residu" (out = x − (1−g)·HP) que nous
        // avions ecrite AMPLIFIAIT de +1,4 dB pres du croisement : la phase du
        // passe-haut y transforme la soustraction en addition. Le gain de
        // reduction vaut donc exactement la reduction de la voie haute, ce qui
        // rend le vumetre verifiable sur une sifflante franche.
        const float gainHigh = dbToGain (-reductionDb);
        const bool shelfActive = plosiveDepthDb < -0.05f;

        for (int ch = 0; ch < n; ++ch)
        {
            // Le shelf de plosives n'agit QUE sur la voie basse : la voie
            // sifflante ne le voit jamais (independance des deux bandes).
            const float low = shelfActive ? plosiveShelf.process (lowIn[ch], ch) : lowIn[ch];

            channels[ch][sampleIndex] = gainHigh * highIn[ch] + low;
        }

        return channels[0][sampleIndex];
    }

}
