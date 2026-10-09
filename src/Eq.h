#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"
#include "PitchDetector.h"

namespace odvox
{
    /** EQ a **quatre bandes**, a dynamique relative et suivi de pitch (F1.6, revu
        le 2026-09-19).

        **Quatre bandes, la structure retenue**, calibree au signal le
        2026-09-20 : Low shelf ~120 Hz, Mid cloche ~700 Hz
        (Q effectif 1), High shelf coin 1,75 kHz — plein gain a 3,5 kHz, comme
        dont 3,5 k est le centre du plateau —, Air shelf
        > 10 kHz. La cinquieme bande (Low-Mid) etait notre ajout : elle est
        retiree avec ses trois `Advanced`.

        Chaque bande porte un **type**, une **frequence**, un **gain** et un
        **Q** — c'est notre apport : les bandes seraient sinon figees a leurs
        ancres. Et chaque bande porte la **logique de dynamique relative** : trois mecanismes,
        par bande, independants.

        ### 1. Dynamique relative — pilotee par le niveau du PROGRAMME

        Le gain **applique** est une fraction du gain **regle**, et cette fraction
        suit le niveau du **signal qui entre dans le module** (moyenne quadratique
        **symetrique**, 30 ms, canal le plus fort). Sous le plancher l'EQ ne fait
        rien — verifie au signal, dans la bande comme hors d'elle (0,00 dB a
        −60 dBFS, mesure du 2026-09-20) ; au-dessus du plein, il applique les
        reglages entiers. Consequence voulue : **on n'egalise pas du souffle** — un
        passage a −50 dBFS ne recoit pas 6 dB d'air.

        **La meme fraction pour les quatre bandes, et c'est la decision de
        conception la plus importante du module.** La premiere version lisait le
        niveau de CHAQUE bande (sa propre sonde). Mesure faite : sur un sinus a
        16 kHz regle Low +6 / Mid −5 / High −4 / Air +5, les gains reellement
        appliques valaient **0 / 0 / −0,55 / +3,09 dB** — chaque bande etrangere au
        contenu etait eteinte, donc la **forme** de la reponse dependait du signal,
        et la courbe affichee ne decrivait plus ce qui sortait. Un declencheur
        partage laisse au contraire la forme intacte : toutes les bandes sont
        multipliees par la **meme** fraction, donc la reponse du module reste
        **proportionnelle** a la courbe, et celle-ci redevient exacte des que le
        niveau atteint le plein regime.

        Le seuil est unique et porte sur le programme :
        « relatif » veut dire relatif au signal, pas au contenu de chaque bande. Et
        la propriete qu'on voulait garder — *une bande que rien n'alimente ne
        change rien* — est de toute facon vraie **par construction** : un filtre
        sans signal en entree ne peut rien modifier.

        Le gain regle est donc une **profondeur maximale**, pas une constante.

        **Calibration : plancher −55 dBFS, plein a −25 dBFS** (30 dB d'etendue).
        Ce n'est PAS un seuil d'*intention artistique* (−34 dBFS), et l'ecart est
        volontaire : une intention veut agir moins quand
        la voix est faible est le but ; ici c'est un *reglage explicite*, et un
        utilisateur qui met +6 dB d'Air a −30 dBFS s'attend a les entendre. Le
        calibrage retenu mord donc sur les passages faibles et moyens sans toucher
        les passages forts, ce qui laisse la **courbe affichee exacte** au-dessus de
        −25 dBFS et preserve la fidelite de la reponse.

        ### 2. Accrochage harmonique (suivi de pitch), borne

        La bande **garde son ancre**, mais si une harmonique de la fondamentale
        detectee tombe a moins de **150 cents** (~1,5 demi-ton) de cette ancre, la
        bande **glisse dessus**. Exemple : Mid ancree a 700 Hz, voix a 220 Hz — la
        3e harmonique est a 660 Hz (102 cents), la bande s'y pose et agit la ou la
        voix a reellement de l'energie. Au-dela de la fenetre, elle ne bouge pas.
        Chaque bande calcule son propre accrochage, donc **independamment**.

        Le glissement est lisse sur ~50 ms : un accrochage est un *deplacement* de
        bande, donc un saut, et une bande qui claque d'une harmonique a l'autre
        s'entendrait.

        **La borne est reellement appliquee** (`kMaxResolvedHarmonic`) : au-dela de
        la 5e harmonique, la grille est plus fine que la fenetre, donc une
        harmonique y tombe TOUJOURS et la bande glisserait en permanence, commandee
        par la note chantee plutot que par le reglage. Mesure du 2026-09-20 : voix a
        120 Hz sur une ancre de 700 Hz — la 6e harmonique est a 720 Hz, soit
        49 cents, en plein dans la fenetre — la bande reste a 700 Hz. Sans la
        garde, elle partait a 720 (attrape par mutation).

        *Pourquoi borne, et pas « ratio x f0 » :* l'ancre est un choix de
        l'utilisateur ; un EQ qui se deplace librement ne serait plus l'EQ qu'il a
        regle. *Pourquoi pas une ponderation par alignement harmonique a frequence
        constante :* sur une bande large — le Mid fait ~1000 cents, soit QUATRE
        harmoniques a f0 = 100 Hz — le poids vaudrait 1 en permanence, et la ou il
        mordrait il ne traduirait plus qu'un hasard de la note chantee. Mesure
        faite avant d'ecrire le module.

        ### 3. AutoGain d'etage — et il ne compense QUE le dynamique

        Un AutoGain **par bande** se marcherait dessus : quatre boucles poursuivant
        la meme cible pompent et derivent. Il n'y en a donc qu'**un**, pour
        l'etage. Et il ne compense **pas** les reglages de l'utilisateur — remonter
        l'Air doit s'entendre — mais seulement l'**ecart** entre le gain regle et
        le gain applique, c'est-a-dire l'effet de la dynamique relative.

        Propriete qui rend le module verifiable : quand la dynamique est a plein
        (gain applique = gain regle), l'ecart est **nul**, donc le makeup vaut
        **exactement 0** et l'EQ statique est preserve — c'est ce qui garde la
        courbe affichee exacte au-dessus de −25 dBFS. Lisse sur 300 ms.

        **Deux ponderations, et chacune a coute une mesure** (2026-09-20) :

        *Le denominateur est l'ENERGIE DU PROGRAMME*, pas la somme des energies des
        bandes. Un ecart de gain de x dB dans une bande qui porte une fraction s de
        l'energie du signal change la sonie de x.s dB — et de x dB seulement si la
        bande porte TOUT le signal. Diviser par la somme des bandes annulait le
        poids des qu'une seule bande agissait : au banc d'essai, sous le plancher,
        l'engagement vaut 0 (les filtres sont donc PLATS) et le module sortait
        **+3,87 dB** de gain large bande, sans la moindre action d'EQ.

        *Et le makeup est pondere par l'ENGAGEMENT lui-meme.* Corriger le
        denominateur ne suffit pas : compenser la sonie du reglage la ou le module
        ne fait rien reste un gain plat — mesure a −60 dBFS, un reglage de +6 dB au
        Mid sortait encore **+3,38 dB**. Un niveau qui bouge sans que rien n'agisse
        est un pompage, pas un AutoGain. Pondere, il tend vers 0 avec l'action :
        **0,00 dB sous le plancher, 0,00 dB a plein regime**, et une part du manque
        au milieu (mesure : 0,73 dB pour un manque de 3,00, Mid +6 / Q 1).

        Consequence a assumer, et c'est la contrepartie de la propriete ci-dessus :
        **le niveau suit le REGLAGE, la forme suit la MATIERE.** Une bande qui
        porte une part de l'energie et dont la dynamique ne recoit que la moitie du
        gain voit le makeup en restituer une part — le niveau ne depend donc pas du
        niveau de la matiere, seulement du reglage.

        ### 4. Reproductibilite

        `reset()` rend un etat qui ne depend QUE du reglage : historiques effaces,
        frequence accrochee ramenee sur son ancre, gain de depart egal au reglage
        entier (la courbe affichee), coefficients reconstruits. Sans cela la
        frequence accrochee survivait au reset et deux rendus du MEME signal
        n'etaient plus identiques au bit pres (mesure du 2026-09-20 : ecart 6,2e-4
        des l'echantillon 4) — un bounce hors ligne ne rendait pas ce que la
        lecture avait fait entendre.

        ### Transparence

        Court-circuit complet — sortie **BIT-EXACTE** — quand `eq_on` est Off, ou
        quand les quatre bandes sont a 0 dB et qu'aucune n'est un passe-haut. Aux
        defauts du catalogue l'EQ n'est PAS neutre : `eq_air_db` vaut
        volontairement **+2,5 dB**, le defaut retenu. */
    class Eq
    {
    public:
        static constexpr int kNumBands = 4;

        // Ordre des choix du catalogue : "Bell|Low Shelf|High Shelf|High Pass".
        enum Type { kBell = 0, kLowShelf = 1, kHighShelf = 2, kHighPass = 3 };

        /** Plancher de la dynamique relative, en dBFS, sur le niveau de la BANDE. */
        static constexpr float kLevelFloorDb = -55.0f;

        /** Etendue au-dessus du plancher : plein regime a `floor + range`. */
        static constexpr float kLevelRangeDb = 30.0f;

        /** Fenetre de mesure du niveau du programme, en ms. **Symetrique** : un
            suiveur asymetrique applique a une puissance se verrouille
            sur les cretes de sa propre ondulation. */
        static constexpr float kLevelWindowMs = 30.0f;

        /** Fenetre d'accrochage : une harmonique hors de cette distance en cents
            de l'ancre ne deplace pas la bande. */
        static constexpr float kSnapWindowCents = 150.0f;

        /** Nombre d'harmoniques le plus grand pour lequel l'accrochage a un sens.

            Avec une fondamentale basse, la grille harmonique devient *plus fine
            que la fenetre* : a 4 kHz et f0 = 150 Hz, deux harmoniques voisines
            sont a ~55 cents, donc il y a TOUJOURS une harmonique dans la fenetre
            et la bande glisserait en permanence — un deplacement commande par la
            note chantee plutot que par la musique. La regle retenue : la grille
            doit etre au moins aussi large que la fenetre, c'est-a-dire qu'**au
            plus une** harmonique puisse y tomber. Cela borne l'accrochage aux
            harmoniques resolues (n <= 5 a ±150 cents). */
        static constexpr int kMaxResolvedHarmonic = 5;

        /** Seuil de confiance du suivi : sous ce seuil, aucune bande ne
            s'accroche et toutes restent sur leur ancre. */
        static constexpr float kConfidenceThreshold = 0.5f;

        /** Lissage du deplacement d'une bande, en ms. */
        static constexpr float kSnapGlideMs = 50.0f;

        /** Constante de temps de l'AutoGain d'etage, en ms. */
        static constexpr float kAutoGainMs = 300.0f;

        /** Bornes du makeup : on corrige un ecart de dynamique, on ne rattrape pas
            n'importe quoi. */
        static constexpr float kAutoGainMaxDb = 12.0f;

        /** Periode de la decision (accrochage, engagement, coefficients), en
            echantillons. Une reconstruction par echantillon couterait des sinus et
            des racines a chaque pas pour un gain qui bouge de quelques centiemes
            de dB. */
        static constexpr int kControlInterval = 32;

        // --- Ancres figees des bandes (rev. suppression du mode Avance) ------
        // Ancres retenues (120/700/1750/10 000 Hz) : chaque bande est
        // pitch-suiveuse autour de son ancre, donc frequence, Q et type ne sont
        // PAS des parametres — le catalogue n'expose que les quatre gains.
        static constexpr float kAnchorHz[kNumBands]    = { 120.0f, 700.0f, 1750.0f, 10000.0f };
        static constexpr float kAnchorQ[kNumBands]     = { 1.00f, 1.15f, 1.20f, 1.00f };
        static constexpr int   kAnchorType[kNumBands]  = { kLowShelf, kBell, kHighShelf, kHighShelf };

        struct Band
        {
            float freqHz = 1000.0f;
            float gainDb = 0.0f;
            float q      = 1.0f;
            int   type   = kBell;
        };

        struct Settings
        {
            Band bands[kNumBands];
            bool enabled = true;

            /** Court-circuit complet (voir le commentaire de classe). Le
                PROCESSEUR le pose : l'utilisateur n'a pas a le calculer. */
            bool inert = true;

            /** Frequences REELLEMENT utilisees, apres accrochage harmonique.
                `0` = pas de suivi, la bande reste sur son ancre — c'est ce que
                pose un modele qui ne connait pas la fondamentale (la courbe lue
                sur les parametres, les tests unitaires). Le processeur, lui, les
                remplit depuis `trackedFrequencyHz`, pour que **la courbe tracee et
                le son sortent de la meme valeur**. */
            float trackedHz[kNumBands] {};
        };

        /** Frequence d'une bande : `trackedHz` si renseignee et non nulle, l'ancre
            sinon. **L'unique definition** de « la frequence d'une bande » — le
            filtre, la sonde de mesure et la courbe passent tous par ici, donc ils
            ne peuvent pas diverger. */
        static float effectiveFreqHz (const Settings&, int band) noexcept;

        /** Reponse en amplitude de l'EQ COMPLET, en dB, a une frequence donnee.

            C'est **la source unique de la courbe affichee** : le calcul part des
            MEMES fabriques de coefficients que les filtres qui traitent le son.
            Les bandes etant en serie, leurs reponses s'additionnent en dB.

            Attention a ce qu'elle montre : c'est la reponse **pleinement engagee**,
            celle des gains regles. La dynamique relative ne fait que REDUIRE
            l'action, jamais l'augmenter — la courbe est donc une enveloppe
            superieure, et ce qui sort du module y est inferieur ou egal en
            amplitude. */
        static float responseDb (const Settings&, double sampleRate, float freqHz);

        /** Reponse d'une seule bande (celle de son ancre), en dB. */
        static float bandResponseDb (const Band&, double sampleRate, float freqHz);

        void prepare (double sampleRate, int numChannels);
        void reset();

        /** Appele a CHAQUE BLOC : enregistre les reglages et reconstruit les
            coefficients des bandes qui ont bouge. Ne touche jamais a l'etat des
            filtres. */
        void setSettings (const Settings&);

        /** Un bloc : le detecteur de fondamentale consomme le signal d'entree et
            la fondamentale lisse est mise a jour (glide/hold/repli). A appeler
            AVANT la boucle d'echantillons, sur le
            meme point de la chaine que la mesure du niveau du programme. */
        void beginBlock (const float* const* channels, int numChannels, int numSamples);

        /** Le detecteur de fondamentale de l'EQ. C'est le composant PARTAGE du produit :
            le processeur n'en possede qu'un, via ce module. */
        PitchDetector& detector() noexcept { return pitch; }

        /** Fondamentale reellement utilisee pour l'accrochage, en Hz. */
        float trackingHz() const noexcept { return stableHz; }

        /** Traite un echantillon de chaque canal, sur place. */
        float processSample (float* const* channels, int sampleIndex);

        /** Frequence reellement utilisee par une bande, en Hz (ancre si la bande
            ne suit pas). Lu par la courbe. */
        float trackedFrequencyHz (int band) const noexcept;

        // Ni le gain applique par bande ni le makeup courant n'ont d'accesseur :
        // les tests de la dynamique et de l'AutoGain mesurent l'EFFET au signal
        // (une sonde a deux raies), pas l'etat interne. Un accesseur qui ne sert
        // qu'a confirmer une variable ne prouve rien sur ce qui sort du module.

    private:
        struct BandState
        {
            Biquad filter;               // le filtre audible, gain effectif
            Biquad probe;                // passe-bande de mesure, gain de crete 1
            float  meanSquare = 0.0f;    // part d'energie de la bande (ponderation)
            float  trackedHz = 0.0f;     // frequence accrochee
            float  effectiveGainDb = 0.0f;
            int    builtType = -1;
            float  builtHz = 0.0f;
            float  builtQ = 0.0f;
            float  builtGainDb = 0.0f;
            float  probeBuiltHz = 0.0f;
        };

        /** Reconstruit le filtre audible d'une bande avec sa frequence accrochee
            et son gain EFFECTIF. */
        void rebuildFilter (int band);

        /** Reconstruit la sonde de mesure (passe-bande a gain de crete 1). */
        void rebuildProbe (int band);

        /** Une passe de decision : accrochage, engagement, coefficients, makeup.
            Appelee tous les `kControlInterval` echantillons. */
        void controlTick();

        double sampleRateD = 0.0;
        int numChannelsToProcess = 1;

        Settings settings;
        BandState state[kNumBands];

        float levelCoef = 1.0f;
        float snapCoef = 1.0f;
        float makeupCoef = 1.0f;

        int controlCounter = 0;

        /** Niveau du PROGRAMME, moyenne quadratique lissee du canal le plus fort.
            Le plus fort, et non la somme des canaux : un signal mono pose dans un
            buffer stereo ne doit pas lire 3 dB de moins qu'en mono, sinon le
            meme son declencherait la dynamique a deux endroits differents. */
        float programmeMeanSquare = 0.0f;

        bool trackingValid = false;
        float fundamentalHz = 0.0f;

        // --- Suivi de fondamentale --------------------------------------------
        // Un etage de mise en forme autonome a ete supprime (double emploi),
        // mais sa machine de suivi reste : c'est elle qui nourrit l'accrochage
        // harmonique des bandes.
        PitchDetector pitch;

        /** Fondamentale de repli quand aucune n'est detectee (cas limite du PRD). */
        static constexpr float kFallbackHz = 150.0f;

        /** Apres ce temps sans detection, la fondamentale revient a kFallbackHz. */
        static constexpr float kHoldMs = 500.0f;

        /** Constante de temps du lissage du placement de la fondamentale, en ms.
            Exprimee en temps (pas par bloc) pour un son independant de l'hote. */
        static constexpr float kTrackGlideMs = 30.0f;

        // Fondamentale lisse, figee sous le seuil de confiance.
        float stableHz = kFallbackHz;
        float lastConfidentHz = kFallbackHz;
        int samplesSinceDetection = 0;

        float currentMakeupDb = 0.0f;
        float makeupGain = 1.0f;
        bool makeupActive = false;
    };
}
