#include "Spectrum.h"

#include <cmath>
#include <cstring>

namespace odvox
{
    namespace
    {
        /** Le nombre de FFT qu'un seul appel d'`update` peut calculer. Un hote a
            192 kHz remplit l'anneau plus vite que l'affichage ne le vide ; sans
            plafond, une image pourrait en calculer dix d'un coup et faire
            hoqueter l'interface. Quatre suffisent a rattraper 8x le retard a
            48 kHz. */
        constexpr int kMaxFftsPerUpdate = 4;

        /** L'echelle d'affichage d'un spectre d'analyseur est logarithmique : des
            bandes de 1/12 d'octave sont ce qu'un oeil attend, et c'est aussi ce
            qui rend la plage 20 Hz - 20 kHz lisible sur quelques centaines de
            pixels. */
        float logFrequency (float x01, float maxHz)
        {
            const float lo = std::log (SpectrumAnalyzer::kMinHz);
            const float hi = std::log (juce::jmax (SpectrumAnalyzer::kMinHz * 1.01f, maxHz));
            return std::exp (lo + x01 * (hi - lo));
        }
    }

    // --- SpectrumTap --------------------------------------------------------

    SpectrumTap::SpectrumTap()
    {
        prepare();
    }

    void SpectrumTap::prepare()
    {
        buffer.assign ((size_t) kCapacity, 0.0f);
        fifo.reset();
    }

    void SpectrumTap::reset()
    {
        fifo.reset();
    }

    void SpectrumTap::push (const float* samples, int numSamples) noexcept
    {
        if (numSamples <= 0 || samples == nullptr || buffer.empty())
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        const int written = size1 + size2;

        if (written < numSamples)
            dropped.fetch_add (numSamples - written, std::memory_order_relaxed);

        if (size1 > 0)
            std::memcpy (buffer.data() + start1, samples, (size_t) size1 * sizeof (float));

        if (size2 > 0)
            std::memcpy (buffer.data() + start2, samples + size1, (size_t) size2 * sizeof (float));

        fifo.finishedWrite (written);
    }

    int SpectrumTap::read (float* dest, int maxSamples) noexcept
    {
        if (maxSamples <= 0 || dest == nullptr || buffer.empty())
            return 0;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (maxSamples, start1, size1, start2, size2);

        if (size1 > 0)
            std::memcpy (dest, buffer.data() + start1, (size_t) size1 * sizeof (float));

        if (size2 > 0)
            std::memcpy (dest + size1, buffer.data() + start2, (size_t) size2 * sizeof (float));

        fifo.finishedRead (size1 + size2);

        return size1 + size2;
    }

    // --- SpectrumAnalyzer ---------------------------------------------------

    void SpectrumAnalyzer::prepare (double sampleRate)
    {
        sampleRateD = sampleRate > 0.0 ? sampleRate : 48000.0;
        binWidthHz = sampleRateD / (double) kFftSize;

        fftData.assign ((size_t) kFftSize * 2, 0.0f);
        staging.clear();
        staging.reserve ((size_t) kFftSize * 2);

        // Calibration, pour une fenetre de Hann NON normalisee : la somme des
        // puissances sur TOUTES les raies d'un sinus d'amplitude A vaut
        // 3/16 * N^2 * A^2 (Parseval). L'analyseur ne somme que les raies
        // POSITIVES (0 a N/2) : un signal reel partage son energie entre les deux
        // moities du plan, donc la moitie nous echappe — d'ou le 3/32 et non le
        // 3/16. La mesure l'a dit avant le raisonnement : la premiere version
        // sortait 3,01 dB trop bas, exactement le facteur 2 en puissance.
        //
        // C'est cette constante qui fait qu'un sinus a -12 dBFS se lit a -12 dB,
        // et elle est verifiee a la mesure par les tests plutot que supposee.
        windowPower = 10.0 * std::log10 (3.0 / 32.0 * (double) kFftSize * (double) kFftSize);

        columns.clear();
        firstBin.clear();
        lastBin.clear();

        prepared = true;
    }

    void SpectrumAnalyzer::reset()
    {
        staging.clear();
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        if (! columns.empty())
        {
            // Le spectre retombe : on le remet a son plancher plutot que de
            // laisser une image figee d'un signal qui n'existe plus.
            std::fill (columns.begin(), columns.end(), kMinDb);
        }
    }

    float SpectrumAnalyzer::columnFrequency (int column, int numColumns, float maxHz)
    {
        if (numColumns <= 0)
            return kMinHz;

        const float x01 = ((float) column + 0.5f) / (float) numColumns;
        return juce::jlimit (kMinHz, juce::jmax (kMinHz, maxHz),
                             logFrequency (x01, maxHz));
    }

    void SpectrumAnalyzer::columnBandHz (int column, int numColumns, float maxHz,
                                         float& lowHz, float& highHz)
    {
        const double centre = (double) columnFrequency (column, numColumns, maxHz);
        // Demi-bande de part et d'autre du centre : la bande agregee fait bien
        // 1/12 d'octave en tout.
        const double step = std::pow (2.0, (double) kBandOctaves * 0.5);

        lowHz  = (float) (centre / step);
        highHz = (float) (centre * step);
    }

    void SpectrumAnalyzer::rebuildColumns (int numColumns, float maxHz)
    {
        columns.assign ((size_t) numColumns, kMinDb);
        firstBin.assign ((size_t) numColumns, 0);
        lastBin.assign ((size_t) numColumns, 0);

        const int numBins = kFftSize / 2 + 1;

        for (int i = 0; i < numColumns; ++i)
        {
            const double centre = (double) columnFrequency (i, numColumns, maxHz);

            float lowHz = 0.0f, highHz = 0.0f;
            columnBandHz (i, numColumns, maxHz, lowHz, highHz);

            int first = (int) std::ceil ((double) lowHz / binWidthHz);
            int last  = (int) std::floor ((double) highHz / binWidthHz);

            // Une bande plus etroite qu'une raie (le grave) doit quand meme
            // lire quelque chose : on retombe alors sur la raie la plus proche.
            if (last < first)
            {
                first = last = juce::jlimit (1, numBins - 1,
                                             (int) std::lround (centre / binWidthHz));
            }

            firstBin[(size_t) i] = juce::jlimit (1, numBins - 1, first);
            lastBin[(size_t) i]  = juce::jlimit (firstBin[(size_t) i], numBins - 1, last);
        }
    }

    void SpectrumAnalyzer::transformOneWindow()
    {
        if (fftData.size() < (size_t) kFftSize * 2 || staging.size() < (size_t) kFftSize)
            return;

        std::memcpy (fftData.data(), staging.data(), (size_t) kFftSize * sizeof (float));
        std::fill (fftData.begin() + kFftSize, fftData.begin() + 2 * kFftSize, 0.0f);

        window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const int numBins = kFftSize / 2 + 1;

        for (size_t i = 0; i < columns.size(); ++i)
        {
            const int first = firstBin[i];
            const int last  = lastBin[i];

            double power = 0.0;
            int count = 0;

            for (int b = first; b <= last && b < numBins; ++b)
            {
                const double m = (double) fftData[(size_t) b];
                power += m * m;
                ++count;
            }

            if (count == 0 || power <= 0.0)
            {
                columns[i] = kMinDb;
                continue;
            }

            // PUISSANCE de la bande (et non sa densite) : c'est ce qu'un
            // analyseur par bandes affiche, et c'est ce qui fait qu'un sinus se
            // lit a son propre niveau quelle que soit la largeur de la bande.
            // Consequence assumee : un bruit blanc monte de 3 dB par octave —
            // c'est le comportement d'un RTA, pas un defaut.
            const float db = (float) (10.0 * std::log10 (power) - windowPower);

            columns[i] = juce::jlimit (kMinDb, kMaxDb, db);
        }
    }

    bool SpectrumAnalyzer::update (SpectrumTap& tap, int numColumns, float maxHz, int* ffts)
    {
        if (ffts != nullptr)
            *ffts = 0;

        if (! prepared || numColumns <= 0 || sampleRateD <= 0.0)
            return false;

        if ((int) columns.size() != numColumns
            || (int) firstBin.size() != numColumns
            || std::abs (lastFrequency - maxHz) > 0.5f)
        {
            rebuildColumns (numColumns, maxHz);
            lastFrequency = maxHz;
        }

        // Tout ce qui est disponible, borne par ce qui tient dans la fenetre plus
        // un tour de tampon : au-dela, le retard ne se rattrape pas image par
        // image, et l'analyseur n'a pas a courir apres un passe.
        const int room = (int) ((size_t) kFftSize * 2 - staging.size());
        const int maxRead = juce::jmax (0, room);

        if (maxRead > 0)
        {
            const size_t before = staging.size();
            staging.resize (before + (size_t) maxRead);

            const int got = tap.read (staging.data() + before, maxRead);
            staging.resize (before + (size_t) got);
        }

        int computed = 0;

        while (staging.size() >= (size_t) kFftSize && computed < kMaxFftsPerUpdate)
        {
            transformOneWindow();
            ++computed;

            staging.erase (staging.begin(), staging.begin() + kHopSize);
        }

        // Si meme la fenetre entiere ne tient plus, on ne garde que la fin :
        // c'est le signal le plus recent qui compte, pas l'historique.
        if (staging.size() > (size_t) kFftSize)
            staging.erase (staging.begin(), staging.begin() + (int) staging.size() - kFftSize);

        if (ffts != nullptr)
            *ffts = computed;

        return computed > 0;
    }
}
