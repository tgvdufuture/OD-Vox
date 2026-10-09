#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"

#include <array>

namespace odvox
{
    /** Low cut : passe-haut FIGE, commande par un interrupteur (F1.5, revu le
        2026-09-19).

        **Un interrupteur, aucune valeur reglable.** C'est le comportement de la
        cible, etabli au signal :
        un passe-haut dont le coin −3 dB tombe vers **110-120 Hz**, de pente
        mesuree **~19,8 dB/oct**, **sans frequence ni pente exposees**. Les trois
        parametres `Advanced` qui accompagnaient le module — frequence, pente,
        profondeur dynamique — sont donc retires du catalogue : regler un module
        dont la raison d'etre est precisement de ne pas se regler n'apportait rien
        a l'utilisateur, et faisait porter a l'interface des commandes que la
        cible n'a jamais eues.

        **Le son : un Butterworth d'ordre 4 a 120 Hz** — deux biquads, Q =
        0,5412 puis 1,3066 — soit −3 dB exactement au coin, une reponse
        maximalement plate, et **24 dB/oct** en asymptote. Pourquoi 24 et pas 18
        ou 20 : c'est le filtre propre le plus proche des 19,8 dB/oct mesures, et
        une pente non entiere n'existe pas en cascades de biquads.

        **La partie dynamique est retiree avec ses reglages.** Elle appliquait un
        low-shelf dont la profondeur suivait le niveau du signal. La garder active
        sans la moindre commande reproduirait exactement ce qu'on reproche a la
        cible — un traitement que l'utilisateur ne peut ni voir ni corriger,
        comme son compresseur qui comprime deja a 1,5:1 quand son curseur est a
        0 %. Ici : interrupteur ouvert, il coupe ; ferme, il ne fait **rien**, au
        bit pres.

        **Defaut Off**, et c'est un choix assume. Le contrat de transparence du
        PRD (§3.4) exige une chaine neutre **au bit pres** aux defauts du
        catalogue : un coupe-bas actif par defaut le violerait. */

    class LowCut
    {
    public:
        /** Frequence de coupure figee, en Hz. */
        static constexpr float kFixedHz = 120.0f;

        /** Pente figee, en dB par octave. 24 = ordre 4 = deux biquads. */
        static constexpr int kFixedSlopeDbOct = 24;

        /** Nombre de biquads de la pente figee. */
        static constexpr int kNumStages = 2;

        static constexpr int kMaxChannels = 2;   // mono ou stereo (PRD §3.4)

        struct Settings
        {
            /** L'interrupteur. `false` = court-circuit complet, bit-exact. */
            bool enabled = false;
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        /** A appeler une fois par bloc. Reconstruit les coefficients. */
        void setSettings (const Settings&);

        void processSample (float* const* channels, int sampleIndex);

        /** Biquads actifs : 0 quand l'interrupteur est ferme. */
        int numActiveStages() const noexcept { return stagesUsed; }

        /** Frequence de coupure REELLEMENT utilisee, apres bornage a Nyquist.
            Egal a `kFixedHz` aux taux d'echantillonnage usuels ; la borne n'existe
            que pour les taux tres bas (32 kHz et moins), ou 120 Hz reste de toute
            facon atteignable — la valeur est exposee pour que la mesure puisse
            verifier le coin plutot que de le supposer. */
        float cutoffHz() const noexcept { return activeHz; }

    private:
        using Biquad = odvox::Biquad;

        void updateHighPass (Biquad&, float freqHz, float q) noexcept;

        double sampleRate = 48000.0;
        int numChannels = 2;
        int stagesUsed = 0;
        float activeHz = kFixedHz;
        bool inert = true;

        std::array<Biquad, (size_t) kNumStages> stages;
    };
}
