#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

namespace odvox
{
    /** Ce que le **thread audio** ecrit, pour l'analyseur de spectre (AC3 de
        US-04).

        Un anneau sans verrou (`juce::AbstractFifo`) : aucune allocation, aucun
        `mutex`, aucun `wait` — donc l'analyse ne peut pas retarder le traitement,
        et c'est exactement ce que « aucun depassement de bloc » veut dire. Si le
        lecteur prend du retard, les echantillons en trop sont **abandonnes et
        comptes** (`droppedSamples`) plutot que d'attendre : un spectre peut
        sauter une fraction de seconde, un bloc audio ne peut pas attendre.

        Le thread audio pousse le melange mono de sa sortie ; le thread de
        messages lit ce qui est disponible au rythme de l'affichage. */
    class SpectrumTap
    {
    public:
        /** Environ 1,4 s a 48 kHz : de quoi absorber plusieurs images d'avance. */
        static constexpr int kCapacity = 1 << 16;

        SpectrumTap();

        /** Alloue l'anneau. Thread de messages (ou `prepareToPlay`). */
        void prepare();

        /** Vide l'anneau sans toucher au compteur d'abandons : il mesure la sante
            du couple audio/messages sur la duree, pas depuis le dernier reset. */
        void reset();

        /** Thread audio : recopie, sans jamais bloquer. */
        void push (const float* samples, int numSamples) noexcept;

        /** Thread de messages : lit au plus `maxSamples`. Renvoie le nombre lu. */
        int read (float* dest, int maxSamples) noexcept;

        /** Echantillons abandonnes faute de place. Doit rester a 0 si le lecteur
            suit — c'est le critere qu'exerce le test des 10 minutes. */
        int droppedSamples() const noexcept { return dropped.load (std::memory_order_relaxed); }

        int numReady() const noexcept { return fifo.getNumReady(); }

    private:
        juce::AbstractFifo fifo { kCapacity };
        std::vector<float> buffer;
        std::atomic<int> dropped { 0 };

        JUCE_DECLARE_NON_COPYABLE (SpectrumTap)
    };

    /** L'analyseur de spectre (AC3 de US-04).

        **Le calcul ne vit PAS dans le thread audio** (§7 des risques) : `update()`
        lit ce que le `SpectrumTap` a capte et calcule la FFT. Il est appele par le
        thread de messages, au rythme de l'affichage.

        - FFT de **16384 points** (la largeur de raie vaut `binHz()`, 2,93 Hz a
          48 kHz), fenetre de **Hann** ;
        - recouvrement de 87,5 % (pas de 2048 echantillons) : 23 mises a jour par
          seconde a 48 kHz, 21 a 44,1 kHz — au-dela des 20 images/s exigees ;
        - agregation par bandes de **1/12 d'octave**, la resolution exigee.

        **La limite, ecrite plutot que maquillee** : a 20 Hz une bande de 1/12
        d'octave fait 1,16 Hz, plus etroite que la raie de 2,93 Hz. La resolution
        de 1/12 d'octave est donc REELLE a partir de ~50 Hz (ou la bande fait
        2,9 Hz, l'egal de la raie) et jusqu'a 20 kHz ; en dessous, c'est la raie
        qui commande. Aucune implementation ne fait mieux sans plusieurs centaines
        de millisecondes de latence : la limite est physique, pas un raccourci. */
    class SpectrumAnalyzer
    {
    public:
        static constexpr int kFftOrder = 14;
        static constexpr int kFftSize  = 1 << kFftOrder;   // 16384
        static constexpr int kHopSize  = kFftSize / 8;     // 2048
        static constexpr float kMinHz  = 20.0f;

        /** Bornes de l'echelle d'affichage, en dB. */
        static constexpr float kMinDb = -90.0f;
        static constexpr float kMaxDb =   6.0f;

        /** **La** reponse a « au moins 1/12 d'octave » : largeur d'une bande
            d'analyse, en octaves. Elle est exposee pour que le test la lise
            plutot que de recopier la constante. */
        static constexpr float kBandOctaves = 1.0f / 12.0f;

        void prepare (double sampleRate);
        void reset();

        /** Thread de messages : consomme le tap et met a jour les colonnes.

            `numColumns` est la largeur utile de l'affichage (une colonne par
            pixel), `maxHz` la frequence du bord droit. Renvoie vrai si de
            nouvelles colonnes ont ete calculees.

            `ffts` renvoie le nombre de FFT calculees, pour que le test puisse
            mesurer la cadence reelle plutot que de croire la constante. */
        bool update (SpectrumTap&, int numColumns, float maxHz, int* ffts = nullptr);

        /** Spectre en dB, une valeur par colonne (la plus recente). Vide tant
            qu'aucune FFT n'a tourne. */
        const std::vector<float>& columnsDb() const noexcept { return columns; }

        /** Largeur de raie a l'echantillonnage prepare, en Hz. */
        double binHz() const noexcept { return binWidthHz; }

        /** Mises a jour par seconde a cet echantillonnage : c'est la cadence de
            donnees neuves, que l'affichage ne peut pas depasser. */
        double updatesPerSecond() const noexcept
        {
            return sampleRateD > 0.0 ? sampleRateD / (double) kHopSize : 0.0;
        }

        /** Frequence au centre de la colonne `column`. Meme loi que l'affichage :
            l'axe est logarithmique, c'est ce qui rend l'echelle d'octaves
            lisible. */
        static float columnFrequency (int column, int numColumns, float maxHz);

        /** Largeur, en octaves, de la bande agregee pour une colonne. C'est la
            mesure de la resolution — le test l'interroge pour les deux bornes de
            la plage affichee. */
        static float columnBandOctaves() noexcept { return kBandOctaves; }

        /** Les bornes, en Hz, de la bande agregee pour une colonne : c'est la
            largeur REELLEMENT moyennee, donc la resolution REELLE de
            l'affichage. Le test la mesure au lieu de croire la constante. */
        static void columnBandHz (int column, int numColumns, float maxHz,
                                  float& lowHz, float& highHz);

    private:
        void rebuildColumns (int numColumns, float maxHz);
        void transformOneWindow();

        double sampleRateD = 48000.0;
        double binWidthHz = 48000.0 / (double) kFftSize;

        juce::dsp::FFT fft { kFftOrder };
        juce::dsp::WindowingFunction<float> window {
            (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, false };

        std::vector<float> fftData;      // 2 * kFftSize, exigence de JUCE
        std::vector<float> staging;      // echantillons recus, pas encore consommes

        std::vector<float> columns;
        std::vector<int>   firstBin;     // premiere raie de chaque colonne
        std::vector<int>   lastBin;      // derniere raie de chaque colonne

        double windowPower = 1.0;        // facteur de calibration, en puissance
        float lastFrequency = 0.0f;      // bord droit des colonnes construites
        bool prepared = false;
    };
}
