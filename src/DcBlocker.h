#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace odvox
{
    /** Filtre DC de sortie (`output_dc_filter`, module Sortie, defaut On).

        Un passe-haut a UN POLE, FIGE a **10 Hz** — la meme constante que le DC
        blocker du Drive : les deux traitements sont le meme, au meme endroit du
        spectre. Ce n'est pas un reglage tonal, c'est un retrait d'offset
        continu et d'infra-basses : a 40 Hz le filtre ne retire deja plus que
        0,3 dB, a 100 Hz 0,05 dB. Le catalogue ne lui donne donc aucune
        commande de frequence — un interrupteur, comme le low cut.

        **Defaut On, et c'est un choix assume.** C'est par defaut qu'on veut
        qu'un offset de convertisseur ou l'asymetrie d'une saturation ne parte
        pas dans le mix — c'est la raison d'etre de la Sortie, qui portait ce
        filtre a la place d'un parametre `bypass` maison (PRD §3.4, point 1).
        Il place donc la chaine aux defauts HORS du contrat de transparence des
        CURSEURS (US-02 AC2) : c'est un interrupteur de nettoyage, un reglage
        TONAL du defaut, au meme titre que le low cut depuis l'amendement du
        2026-09-29. Interrupteur ouvert, il ne fait **rien**, au bit pres.

        **La commutation est FONDUE sur 20 ms** (`kRampSeconds`), et ce n'est
        pas un luxe : le filtre en regime etabli s'ecarte du signal sec de
        l'ecart d'amplitude ET de phase d'un passe-haut a 10 Hz — soit un saut
        de **−25 dBFS a 100 Hz** et **−18 dBFS a 40 Hz** si l'interrupteur
        coupait net, au-dessus du seuil de clic que G4 fixe a −60 dBFS. Le
        fondu est lineaire sur le MELANGE sec/filtre ; le filtre, lui, tourne
        toujours sur l'entree, sinon il rattraperait son retard d'un coup a la
        fin de la rampe. Le test mesure le cran et le borne.

        **Aucune latence** : un pole, pas de ligne de retard — le contrat de
        latence nulle du chemin sec (US-12) n'est pas touche. */
    class DcBlocker
    {
    public:
        /** Frequence de coupure figee, en Hz (valeur du DC blocker du Drive). */
        static constexpr float kFixedHz = 10.0f;

        /** Duree du fondu de commutation, en secondes. Le contrat de lissage du
            PRD §3.3 demande au moins 10 ms sur chaque parametre. */
        static constexpr float kRampSeconds = 0.02f;

        static constexpr int kMaxChannels = 2;   // mono ou stereo (PRD §3.4)

        void prepare (double newSampleRate, int newNumChannels)
        {
            rate        = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            numChannels = juce::jlimit (1, kMaxChannels, newNumChannels);

            // Un pole : coefficient classique `1 - 2.pi.fc/fs`.
            coef = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi
                                        * (double) kFixedHz / rate);
            rampStep = 1.0f / juce::jmax (1.0f, (float) (kRampSeconds * rate));

            // L'etat du filtre repart de zero : sans signal precedent, le
            // premier echantillon vaut le signal sec, il n'y a donc rien a
            // fondre a l'amorcage.
            mix = target;
            reset();
        }

        void reset() noexcept
        {
            x1.fill (0.0f);
            y1.fill (0.0f);
        }

        /** L'interrupteur. A l'ouverture, l'etat du filtre est remis a zero :
            un filtre qu'on rallume repart propre, pas d'un reste du signal
            precedent (meme regle que le low cut). Court-circuit complet et
            bit-exact une fois le fondu retombe (le melange revient a 0 pile). */
        void setEnabled (bool shouldBeEnabled) noexcept
        {
            const float wanted = shouldBeEnabled ? 1.0f : 0.0f;

            // Rien a faire quand l'interrupteur ne bouge pas : le processeur
            // rappelle cette fonction A CHAQUE BLOC, et remettre l'etat a zero
            // a chaque appel ferait redemarrer le filtre toutes les 512 frames
            // — un transitoire au rythme des blocs, entendu comme un bourdon.
            if (wanted == target)
                return;

            target = wanted;

            if (shouldBeEnabled)
                reset();
        }

        bool isEnabled() const noexcept { return target > 0.0f; }

        /** Vrai quand le filtre est entierement retire du chemin : la sortie EST
            l'entree, bit a bit. */
        bool isInert() const noexcept { return target == 0.0f && mix == 0.0f; }

        void processSample (float* const* channels, int sampleIndex) noexcept
        {
            if (isInert())
                return;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = (size_t) ch;
                const float x = channels[ch][sampleIndex];
                const float y = x - x1[c] + coef * y1[c];

                x1[c] = x;
                y1[c] = y;

                // Le filtre tourne sur l'entree MEME quand il est fondu a zero :
                // le rallumer ne doit pas lui faire rattraper un retard accumule.
                channels[ch][sampleIndex] = mix * y + (1.0f - mix) * x;
            }

            if (mix < target)
                mix = juce::jmin (target, mix + rampStep);
            else if (mix > target)
                mix = juce::jmax (target, mix - rampStep);
        }

    private:
        double rate = 48000.0;
        int numChannels = 2;

        // Inerte avant tout `prepare` (target et mix a zero) : un module non
        // prepare recopie son entree vers sa sortie, comme les autres.
        float target = 0.0f;
        float mix = 0.0f;
        float rampStep = 1.0f;

        float coef = 0.0f;
        std::array<float, (size_t) kMaxChannels> x1 {}, y1 {};
    };
}
