#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"

namespace odvox
{
    /** De-esser et plosives (F1.8, US-10) : **UN SEUL curseur**, cible de
        conception (retour utilisateur du 2026-09-21 — la simplicite d'emploi
        fait partie de la fidelite).

        **Fidelite a la cible** : chez elle,
        la plage utile est etroite et sature presque immediatement (50 % →
        −3,12 dB sur 6–9 kHz ; 100 % → −3,64 dB ; presets d'usine 24–58 %). Nous
        reproduisons ce caractere : la reduction plafonne tot (−3,6 dB des
        ~55 %, avec une epaule douce), et une profondeur maximale du meme ordre.

        **Ce qui est PREREGLÉ en interne** (retiré du catalogue) :
        - croisement FIXE a 5000 Hz — sous la zone de sifflance (6–9 kHz). Un
          croisement place dans cette zone decape l'ecart entre les deux voies :
          mesure a 6500 Hz sur un « S » a 7 kHz, il ne restait plus que −0,55 dB
          d'exces — aucune reduction (voir §3.4 du PRD) ;
        - demouillage de plosives LIE au curseur : plosive01 = min(0,20,
          0,6·amount01) — l'esprit du produit, ou de-esser et demouillage
          vivent dans le meme reglage « propre » ; le plafond 20 % est celui
          qu'appliquaient deja nos presets d'usine ;
        - shelf de plosives a 120 Hz (valeur du catalogue d'origine).
        Le mode « listen » (ancien AC2) est supprime : absent de la cible.

        **Architecture split-band** (contrainte du PRD) : deux voies de
        Linkwitz-Riley ordre 4 (2 x Butterworth Q = 0.7071), croisement a
        5000 Hz.
        - voie HAUTE (sifflantes) : son gain est conduit par un detecteur
          d'enveloppe (attaque 2 ms, recouvrement 60 ms) qui ne lit QUE cette
          voie ;
        - la **reference du seuil est adaptative** : la cible est l'enveloppe de
          la voie BASSE — le corps de la voix sans ses sifflantes. On ne tue pas
          la sifflante, on la ramene vers le niveau du reste de la voix : plus
          la voix est douce, moins la reduction est agressive (l'esprit
          du detecteur adaptatif retenu). La cible est un SUIVEUR DE
          NIVEAU (attaque immediate, recouvrement 25 ms) : un lissage lent
          produirait une reduction a vide au demarrage, le detecteur attaquant
          avant que la cible ne se soit equipee ;
        - voie BASSE (plosives) : low-shelf STATIQUE — une plosive est un
          evenement grave et fort. Le shelf agit sur le CORPS de la
          plosive, pas sur son niveau (le low cut, lui, l'energie).

        **Detection partagee entre canaux** : un seul detecteur pour les deux
        canaux (la voix y est traitee en mono-like) — l'enveloppe
        suit le canal le plus fort, comme une somme d'energie.

        **Recombinaison** : `out = g·(voie haute) + shelf(voie basse)`. La somme
        LP4 + HP4 de deux Butterworth 2 en cascade est un passe-tout EXACT une
        fois passe par la transformee bilineaire (mesure numerique : ±0,000 dB
        de 20 Hz a 16 kHz) — le module ne colore donc rien quand g = 1, et le
        gain applique a la voie haute est EXACTEMENT la reduction. C'est ce qui
        rend le vumetre verifiable ; une recombinaison par soustraction
        de residu, a l'inverse, amplifierait pres du croisement (la phase du
        passe-haut y transforme la soustraction en addition).

        La transparence BIT-EXACTE aux defauts (AC2 de US-02) est obtenue par
        court-circuit, pas par la recombinaison : voir `inert` dans
        `processSample`. */
    class DeEsser
    {
    public:
        static constexpr int   kMaxChannels = Biquad::kMaxChannels;
        // Attaque du detecteur : elle laisse passer les transitoires plus courts
        // qu'elle (demarrage du signal, clics) au lieu de les reduire.
        static constexpr float kDetectionAttackMs  = 2.0f;
        static constexpr float kDetectionReleaseMs = 60.0f;   // recouvrement detection
        static constexpr float kVoiceReleaseMs     = 25.0f;   // recouvrement reference vocale
        static constexpr float kMinDetectorDb      = -90.0f;  // plancher log
        // Saturation (caractere de la cible : plage utile etroite). Mesure
        // chez lui : −3,12 dB a 50 %, −3,64 dB a 100 % — le plafond est atteint
        // tot et n'augmente plus guere ensuite.
        static constexpr float kCeilingDb     = 3.6f;   // plafond a amount = 1
        static constexpr float kCeilingAmount = 0.55f;  // amount ou il est atteint
        static constexpr float kSatKneeDb     = 0.8f;   // epaule douce au plafond

        // Pre-reglages INTERNES (retirés du catalogue le 2026-09-21) : le
        // curseur unique pilote la reduction, le reste est fixe.
        static constexpr float kFixedCrossoverHz  = 5000.0f;  // sous la zone 6–9 kHz
        static constexpr float kFixedPlosiveHz    = 120.0f;
        static constexpr float kPlosiveFollowGain = 0.6f;     // plosive01 = 0,6·amount01
        static constexpr float kPlosiveCap        = 0.20f;    // plafond des presets d'usine

        struct Settings
        {
            // UN SEUL reglage public. Le PROCESSEUR assigne toujours
            // enabled/inert explicitement depuis `deess_amount`, dont le
            // defaut au catalogue est 0 % (AC2 de US-02 : transparence
            // bit-exacte aux defauts).
            float amount01    = 1.0f;     // curseur Essential (0..1)
            bool  enabled     = true;
            bool  inert       = false;    // deess_amount <= 0
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        /** Appele a CHAQUE BLOC par le processeur : reconstruit les
            coefficients SI les frequences ont bouge (coute des sinus, une fois
            par bloc comme le LowCut) — ne touche JAMAIS a l'etat dynamique. */
        void setSettings (const Settings&);

        /** Traite un echantillon sur place, sur tous les canaux (convention des
            modules de la chaine). Retourne l'echantillon de sortie du canal 0. */
        float processSample (float* const* channels, int sampleIndex);

        /** Reduction instantanee de la bande sifflante, en dB (valeur positive)
            — vumetre. */
        float currentReductionDb() const noexcept { return lastReductionDb; }

        /** Reduction maximale depuis le dernier `reset()`, en dB. */
        float maxReductionDb() const noexcept { return worstReductionDb; }

    private:
        void buildFilters();

        double sampleRateD = 0.0;
        int numChannelsToProcess = 1;

        Settings settings;

        // Coefficients reconstruits seulement quand les reglages bougent.
        float builtCrossoverHz = -1.0f;
        float builtPlosive01   = -1.0f;
        float builtPlosiveHz   = -1.0f;

        // Plosive effective, derivee du curseur dans setSettings :
        // min(cap 0,20 ; 0,6 x amount01). L'indépendance des bandes reste
        // verifiee au signal : le shelf n'agit que sur la voie basse.
        float plosive01Effective = 0.0f;

        // Chaque voie = 2 biquads (Butterworth en cascade) = LR4.
        Biquad highStage1, highStage2;   // voie haute (sifflantes)
        Biquad lowStage1, lowStage2;     // voie basse (corps grave)

        // Plosives : low-shelf STATIQUE applique a la voie basse seulement —
        // aucune voie croisee avec la detection (AC1 : independance).
        Biquad plosiveShelf;
        float plosiveDepthDb = 0.0f;   // <= 0 ; 0 = identity

        // Detection (partagee entre canaux) et reference vocale : deux suiveurs
        // de niveau. Le detecteur a une attaque lente (2 ms), la reference une
        // attaque immediate — c'est cette dissymetrie qui blanchit les
        // transitoires d'attaque sans retarder la reduction d'une sifflante.
        float detectorEnvelopeDb = kMinDetectorDb;
        float smoothTargetDb     = kMinDetectorDb;
        float attackCoef         = 0.0f;
        float releaseCoef        = 0.0f;
        float targetReleaseCoef  = 0.0f;

        float lastReductionDb  = 0.0f;   // vumetre instantane
        float worstReductionDb = 0.0f;   // vumetre max depuis reset()
    };
}
