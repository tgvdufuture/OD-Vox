#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"

#include <vector>

namespace odvox
{
    /** Delay ping-pong a divisions rythmiques ou temps libre, avec ducking
        (F1.11, US-15).

        **Structure retenue** : delay **synchronise au tempo** en
        divisions pointees/triples (21 valeurs, defaut `1/4`), **ping-pong**
        actif par defaut, feedback plafonne a **95 %** (protection anti-
        emballement), filtre de timbre des echoes **neutre a 39 %**,
        synchronisation **confirmee au signal** : a 120 BPM, `1/4` mesure
        exactement 0,500 s, `1/4D` 0,750 s, `1/2T` 0,667 s — le mapping est
        musical au centieme. Ce qu'on ajoute, et que le PRD demande
        : le **mode temps libre** (1-2000 ms) et le **ducking** sous la voix.

        ### Ping-pong par retroaction croisee

        Deux lignes, la sortie de l'une alimente l'entree de l'autre :

            ligneL(t) = x(t)            + fb . lectureR(t)
            ligneR(t) = fb . lectureL(t)

        Sur une impulsion, les echoes alternent alors **de canal a chaque
        repetition** (gauche : 1, fb^2, fb^4… ; droite : fb, fb^3, fb^5…) —
        c'est la definition du ping-pong, et elle est verifiee au signal
        (test `echoAlternatesChannels`). Le signal sec n'est jamaisretarde.

        ### L'emballement est impossible PAR CONSTRUCTION (AC3)

        Le filtre de timbre ne tourne **pas dans la boucle** : il est sur le
        chemin humide, apres la lecture. La boucle de retroaction porte un
        **amortissement fixe** (passe-bas a 7,5 kHz + passe-haut a 35 Hz,
        physique d'echo a bande : les repetitions s'assombrissent) dont le
        gain est strictement inferieur a 1 sur tout le spectre. Le gain de
        boucle vaut donc au plus `feedback01` = 0,95 < 1, quelque soit le
        filtre demande : la serie des echoes decroit **toujours**. Le plafond
        de 95 % n'est pas une garde a rajouter, c'est une consequence de la
        topologie — verifie au signal sur 12 s d'impulsion.

        ### Ducking : le delay s'efface pendant la voix (US-15)

        Une enveloppe quadratique de l'ENTREE (le signal sec qui entre dans le
        module, avant l'ajout des echoes) pilote un gain sur le chemin humide
        seul : la voix sounding fait baisser les echoes d'au moins 12 dB a
        100 % (profondeur reelle : 18 dB, plancher −60 dBFS, plein a
        −30 dBFS), et le relache est de **180 ms** — le delay remonte en moins
        de 300 ms apres la fin du mot (AC2). Le niveau sec n'est JAMAIS
        touche : le ducking ne peut pas creer de pompage audible sur la voix
        elle-meme, c'est ce qui le rend utilisable a l'oreille.

        ### Changement de tempo sans clic (cas limite du PRD)

        Le temps de delay est **lisse par echantillon** (15 ms) vers sa cible :
        un changement de BPM ou de division deplace la tete de lecture en
        continu au lieu de la sauter. L'artefact d'un changement de tempo est
        un glissement de pitch bref (comme un turntable), pas un clic —
        mesure < 1 dB sur 20 ms.

        ### Transparence (AC de la chaine)

        A `delay_amount` 0 %, le module est court-circuite : sortie BIT-EXACTE.
        A 100 %, le signal sec traverse sans aucun traitement (pas de gain, pas
        de filtre sur le sec) : le module n'ajoute que des echoes. */
    class Delay
    {
    public:
        // Les 21 divisions du catalogue, dans l'ordre (PRD §3.4) : T = x2/3,
        // D = x1,5. Les valeurs sont en NOIRS (1/4 = 1 noir) — la mesure a
        // 120 BPM les confirme toutes.
        static constexpr int kNumDivisions = 21;
        static float divisionBeats (int index) noexcept;

        static constexpr float kMaxFeedback     = 0.95f;  // jamais 1 (§5.2)
        static constexpr float kNeutralFilter   = 0.39f;  // valeur retenue
        static constexpr float kFallbackBpm     = 120.0f; // tempo par defaut de l'hote
        static constexpr float kMaxDelaySeconds = 8.0f;   // plafond de la ligne (voir prepare)

        // Ducking. Profondeur 18 dB a 100 % : la marge au-dessus des 12 dB
        // d'AC2 laisse les reglages partiels audibles (50 % = 9 dB).
        static constexpr float kDuckDepthDb  = 18.0f;
        static constexpr float kDuckFloorDb  = -60.0f;    // silence : aucun duck
        static constexpr float kDuckFullDb   = -30.0f;    // voix normale : duck plein
        static constexpr float kDuckAttackMs = 5.0f;      // la voix ferme vite
        static constexpr float kDuckReleaseMs = 180.0f;   // < 300 ms d'AC2

        // Lissage du temps de delay (changement de tempo sans clic).
        static constexpr float kTimeGlideMs = 15.0f;

        struct Settings
        {
            float  amount01  = 1.0f;   // niveau des ECHOS ; 0 = module inerte
            int    division  = 10;     // index du choix (10 = "1/4", defaut mesure)
            double bpm       = kFallbackBpm;
            bool   sync      = true;   // sinon : temps libre en ms
            float  freeMs    = 500.0f;
            float  feedback01 = 0.20f; // 0 -> 0,95
            float  filter01  = kNeutralFilter;
            float  duck01    = 0.0f;   // 0 = pas de ducking
            bool   inert     = false;  // court-circuit du module ENTIER
        };

        void prepare (double sampleRate, int numChannels);
        void reset();

        void setSettings (const Settings&);

        /** Derniere Settings posee (lecture de controle des tests). */
        const Settings& getSettingsForTests() const noexcept { return settings; }

        /** Un bloc : recalcule la cible de temps (division x tempo ou ms),
            les coefficients du filtre humide si le timbre a bouge. A appeler
            AVANT la boucle d'echantillons. */
        void beginBlock (const float* const* channels, int numChannels, int numSamples);

        /** Traite un echantillon de chaque canal sur place, et retourne la
            valeur du canal 0 (convention de la chaine). */
        float processSample (float* const* channels, int sampleIndex);

        /** Temps de delay reellement utilise, en ms (lecture de controle). */
        float currentDelayMs() const noexcept;
    private:
        void rebuildWetFilters();

        double sampleRateD = 0.0;
        int    numChannelsToProcess = 1;

        Settings settings;

        // --- Lignes a delai (allouees une fois dans prepare) ---------------
        // La taille couvre le plus long temps utile (2/1D a 60 BPM = 12 s,
        // plafonne a kMaxDelaySeconds) plus la marge d'interpolation.
        std::vector<float> lineL, lineR;
        int   lineLength = 0;
        int   writePos   = 0;

        // Temps de delay LISSE, en echantillons (cible recalculsee par bloc).
        float delaySamples      = 0.0f;
        float delayTargetSamples = 0.0f;
        float timeGlideCoef     = 0.0f;

        // --- Boucle de retroaction : amortissement fixe --------------------
        Biquad loopHighPass;   // 35 Hz : pas d'accumulation de composante DC
        Biquad loopLowPass;    // 7,5 kHz : les repetitions s'assombrissent

        // --- Filtre humide (tilt, neutre a 39 %) ---------------------------
        // Sous 39 % le shelf bas descend (echo plus sombre), au-dessus le
        // shelf haut monte (echo plus clair). Hors de la boucle : le filtre
        // colore ce qu'on ENTEND, il ne peut pas faire diverger la boucle.
        Biquad wetLowShelf;
        Biquad wetHighShelf;
        float builtFilter01 = -1.0f;

        // --- Ducking -------------------------------------------------------
        float duckRamp        = 0.0f;  // rampe 0..1 LISSEE (attaque 5 ms / release 180 ms)
        float duckAttackCoef  = 0.0f;
        float duckReleaseCoef = 0.0f;
        float duckGain        = 1.0f;  // gain applique au chemin HUMIDE
    };
}
