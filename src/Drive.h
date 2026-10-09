#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

#include <memory>

namespace odvox
{
    /** Drive / saturation (F1.9, US-02).

        **Fidelite a la cible** : la
        saturation est **symetrique** — 3f a −10,1 dB et 5f a −15,8 dB sous la
        fondamentale a 100 %, harmoniques PAIRES au plancher de mesure (−75 dB),
        soit un `tanh` / soft-clip, sans la dissymetrie d'un etage a lampe. C'est
        exactement ce que fait notre saveur `Console`, et c'est pour cela qu'elle
        est le **defaut** : un `tanh` de gain 10^(24·amount/20) sur un niveau
        nominal de −12 dBFS donne 3f −11,3 dB et 5f −18,9 dB a 100 % — a 1,2 et
        3,1 dB de la mesure.
        Reserve honnete : le protocole de mesure de la cible (niveau d'entree)
        n'est pas documente. Le produit `gain x niveau` est donc le seul parametre
        identifiable : l'ENGIN et sa signature sont reproduits, la COURBE exacte
        du curseur ne peut pas l'etre point par point.

        **Quatre saveurs** (le PRD n'en demandait qu'une ; AC1 en exige quatre
        spectralement distinctes). Mesurees a 80 % sur une sonde a 1 kHz, chaque
        paire se separe de plus de 3 dB dans au moins une bande de 1/3 d'octave :
        - `Tube`   : asymetrique (`t·(1+0.45t)`) -> 2f a −16 dB, la ou Console et
          Tape sont au plancher ;
        - `Tape`   : genou plus doux (`s/(1+|s|)`, 0,6x) et perte de haut
          (2 pôles a 6 kHz) -> 3f 3,7 dB sous Console, 15f 5 dB sous Console ;
        - `Console`: `tanh` nu — la saveur de la cible ;
        - `Fuzz`   : clip dur polarise (+0,35) -> richesse harmonique maximale.

        **Normalisation.** Le saturateur ne change PAS le niveau : le gain
        statique est calibre pour qu'une entree au niveau nominal (−12 dBFS, la
        valeur d'AC3) ressorte au meme niveau. Consequence utile : un signal
        au-dessus du nominal est comprime, un signal plus faible est remonte —
        c'est le comportement attendu d'un drive, et il borne la sortie. La
        normalisation est prise sur la partie SANS MEMOIRE du saturateur : les
        filtres qui suivent (DC blocker, passe-bas de Tape) ont un gain unitaire
        en basse frequence, et le DC blocker aurait un gain NUL sur une reference
        continue — la prendre apres aurait divise par zero.

        **Oversampling (F2.2, US-09)** : la partie SANS MEMOIRE du saturateur —
        `saturate()` — peut tourner sur echantillonnee (4x, plafonnee a 2x au-
dela de 96 kHz, edge case du PRD) : les harmoniques d'ordre eleve rentrent
        dans la bande de Nyquist elargie au lieu de se replier dans l'audible.
        Les filtres lineaires (passe-bas de Tape, DC blocker) restent a fs : les
        sur-echantillonner ne changerait rien, ils ne replient rien. Deux
        oversampleurs (4x et 2x) sont PRE-ALLOUES en preparation et le chemin HQ
        dispose de son propre tampon : la bascule HQ on/off est un changement de
        pointeur dans le thread audio, jamais une allocation. La latence des
        filtres half-band est reportee au DAW par le processeur — le mode
        zero-latency reste a 0 (AC2 de US-09).

        **Mix** (AC2) : `out = sec + mix·(wet − sec)`. A mix 0 % le module est
        court-circuite, pas fondu : la transparence est alors BIT-EXACTE, donc
        tres en dessous des −120 dBFS exigees. */
    class Drive
    {
    public:
        // Ordre des choix du catalogue (§3.4 du PRD) : "Tube|Tape|Console|Fuzz".
        enum Flavor { kTube = 0, kTape = 1, kConsole = 2, kFuzz = 3 };

        static constexpr float kMaxDriveDb     = 24.0f;   // drive a amount = 1
        static constexpr float kNominalLevel   = 0.25f;   // −12 dBFS crete (AC3)
        static constexpr float kTapeDriveKnee  = 0.6f;    // Tape sature plus tard
        static constexpr float kTapeLowPassHz  = 6000.0f; // perte de haut de la bande
        static constexpr float kTubeSecondHarm = 0.45f;   // poids du terme asymetrique
        static constexpr float kFuzzBias       = 0.35f;   // polarisation du clip dur
        static constexpr float kDcBlockerHz    = 10.0f;   // retire l'offset des asymetriques

        struct Settings
        {
            // UN SEUL reglage public (rev du 2026-09-21 : le `drive` de la
            // le module est son seul reglage). Le PROCESSEUR assigne toujours
            // `inert` explicitement (amount <= 0), et le defaut du catalogue
            // est amount 0 % — AC2 de US-02 (transparence).
            float amount01 = 1.0f;
            bool  inert    = false;
        };

        void prepare (double sampleRate, int numChannels, int maxBlock);
        void reset();

        /** Appele a CHAQUE BLOC : recalcule les gains SI le curseur ou la saveur
            ont bouge — ne touche JAMAIS a l'etat des filtres. */
        void setSettings (const Settings&);

        /** Traite un BLOC sur place, sur tous les canaux (convention des
            modules de la chaine).

            Le Drive est le premier module qui consomme un BLOC et plus un
            echantillon : l'oversampling 4x (F2.2) ne peut pas se faire
            echantillon par echantillon — les filtres half-band travaillent sur
            des blocs. La chaine du processeur est donc scindee en trois passes
            (avant-Drive, Drive au bloc, apres-Drive) ; elle est strictement
            serie, donc l'equivalence est exacte.

            HQ OFF (ou module inerte) : chemin par echantillon, latency 0,
            comportement historique inchange. HQ ON : upsample -> saturate a
            N x fs -> downsample. */
        void processBlock (float* const* channels, int numChannels, int numSamples);

        /** Latence introduite par le chemin HQ, en echantillons (filtres
            half-band). ZERO quand HQ est off OU QUAND LE MODULE EST INERT
            (amount 0 : le chemin est bit-exact, il n'applique AUCUN retard —
            reporter la latence des oversampleurs dans ce cas serait mentir a
            l'hote, qui compenserait un retard qui n'existe pas). Le processeur
            la reporte au DAW a chaque changement. */
        float hqLatencySamples() const noexcept { return (hqActive && ! settings.inert) ? hqLatency : 0.0f; }

        /** Pose la DEMANDE de mode HQ (le parametre hote). Le chemin audio ne
            bascule qu'a l'appel suivant de `applyHqMode()`, que le processeur
            fait entre deux blocs — jamais dans le thread audio. */
        void setHqRequested (bool requested) noexcept { hqRequested = requested; }

        /** Applique la demande HQ : changement de pointeur d'oversampleur,
            recalcul de la latence du chemin actif. A appeler HORS du thread
            audio (le processeur l'appelle depuis `refreshDerivedSettings`,
            c'est-a-dire entre deux blocs). */
        void applyHqMode();

        /** Traite un echantillon sur place, sur tous les canaux (convention des
            modules de la chaine). Retourne l'echantillon de sortie du canal 0.

            DEPRECIE pour le chemin HQ : la variante sur echantillonnee passe
            par `processBlock`. Ce point d'entree reste la voie HORS HQ — c'est
            lui que le processeur appelait avant F2.2 et que les tests de
            module continuent d'exercer. */
        float processSample (float* const* channels, int sampleIndex);

    private:
        float saturate (float x) const noexcept;
        void  refreshGains();

        /** Une tranche du chemin HQ : au plus la capacite preparee. Le
            decoupage interne est STRICTEMENT equivalent au bloc unique — les
            oversampleurs sont des filtres a etat, traverses en flux. */
        void processHqChunk (float* const* channels, int numCh, int start, int count);

        double sampleRateD = 0.0;
        int numChannelsToProcess = 1;

        Settings settings;

        // --- Oversampling (F2.2) -----------------------------------------------
        // Pre-alloues en preparation : la bascule HQ est un changement de
        // pointeur, jamais une allocation dans le thread audio. `std::unique_ptr`
        // a taille fixe : la classe reste copiable nulle part, mais declarable
        // partout (les tests construisent un Drive par pile).
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;

        /** Le tampon du chemin HQ, prepare a la taille du bloc max : l'entree
            du bloc y est copiee avant traitement, la sortie re-ecrite dedans.
            Alloue en `prepare`, pas dans le thread audio. */
        juce::AudioBuffer<float> hqBuffer;

        bool hqRequested = false;   // le parametre, brut
        bool hqActive    = false;   // ce que le chemin AUDIO fait maintenant
        float hqLatency  = 0.0f;    // latence du chemin actif, en echantillons
        int   hqFactor   = 1;       // facteur reel (2 ou 4), pour la lisibilite

        // Gains reconstruits seulement quand le curseur ou la saveur bougent.
        float builtAmount01 = -1.0f;
        int   builtFlavor   = -1;
        float driveGain = 1.0f;   // 10^(kMaxDriveDb x amount / 20)
        float normGain  = 1.0f;   // ramene le niveau nominal a l'unite

        // Perte de haut de `Tape` (2 pôles) et anti-offset des asymetriques.
        Biquad tapeLowPass;
        float dcCoef = 0.0f;
        std::array<float, (size_t) Biquad::kMaxChannels> dcX1 {}, dcY1 {};
    };
}
