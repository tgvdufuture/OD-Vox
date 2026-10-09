#include "Drive.h"

#include <cmath>

namespace odvox
{
    namespace
    {
        constexpr double kShelfQ = 0.7071067811865476;   // Butterworth

        float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

        // Plafond du facteur d'oversampling (edge case du PRD, G3) : au-dela
        // de 96 kHz, 4x signifierait traiter a 768 kHz. 2x suffit : le Nyquist
        // elargi est deja a 2 x la bande utile.
        constexpr double kHqPlafondHz = 96000.0;
    }

    void Drive::prepare (double sampleRate, int numChannels, int maxBlock)
    {
        sampleRateD = sampleRate;
        numChannelsToProcess = juce::jmax (1, numChannels);

        // --- Oversampleurs pre-alloues (F2.2) --------------------------------
        // Deux instances, pour ne JAMAIS allouer dans le thread audio au
        // changement de mode : 4x (le regime nominal), et 2x pour les taux
        // d'echantillonnage eleves (plafond du PRD).
        const int channels = juce::jmax (1, numChannels);

        oversampler4x = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversampler2x = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

        oversampler4x->initProcessing ((size_t) juce::jmax (1, maxBlock));
        oversampler2x->initProcessing ((size_t) juce::jmax (1, maxBlock));

        // Tampon du chemin HQ, dimensionne a la taille de bloc max : alloue
        // ICI, jamais dans `processBlock`.
        hqBuffer.setSize (channels, juce::jmax (1, maxBlock), false, false, true);

        hqActive = false;
        hqLatency = 0.0f;
        applyHqMode();

        // DC blocker : un pole a 10 Hz. Coef classique `1 - 2.pi.fc/fs`.
        dcCoef = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * (double) kDcBlockerHz / sampleRate);

        const auto lp = juce::IIRCoefficients::makeLowPass (sampleRate, (double) kTapeLowPassHz,
                                                           kShelfQ);
        tapeLowPass.set (lp.coefficients[0], lp.coefficients[1], lp.coefficients[2],
                         lp.coefficients[3], lp.coefficients[4]);

        reset();
    }

    void Drive::reset()
    {
        tapeLowPass.clearState();
        dcX1.fill (0.0f);
        dcY1.fill (0.0f);

        if (oversampler4x != nullptr)
            oversampler4x->reset();

        if (oversampler2x != nullptr)
            oversampler2x->reset();
    }

    void Drive::refreshGains()
    {
        builtAmount01 = settings.amount01;
        builtFlavor   = Drive::kConsole;

        driveGain = dbToGain (kMaxDriveDb * juce::jlimit (0.0f, 1.0f, settings.amount01));

        // Normalisation PRISE SUR LA PARTIE SANS MEMOIRE, au niveau nominal :
        // une entree au niveau nominal ressort au meme niveau. La
        // prendre apres le DC blocker diviserait par zero (son gain est nul en
        // continu).
        const float atNominal = saturate (driveGain * kNominalLevel);
        normGain = std::abs (atNominal) > 1.0e-6f ? kNominalLevel / atNominal : 1.0f;
    }

    void Drive::setSettings (const Settings& s)
    {
        settings = s;

        if (settings.amount01 != builtAmount01 || Drive::kConsole != builtFlavor)
            refreshGains();

        // Le mode HQ est porte par le PROCESSEUR (cf. `applyHqModeFromHost`) :
        // les Settings du module ne le portent pas, pour ne pas faire doubler
        // l'etat. Le processeur decide du moment — entre deux blocs.
    }

    void Drive::applyHqMode()
    {
        // Plafond a 2x au-dela de 96 kHz (edge case du PRD) : traiter a 768 kHz
        // ne sert a rien — le Nyquist elargi couvre deja deux fois la bande —
        // et couterait deux fois trop (G3).
        const bool plafonne = sampleRateD > kHqPlafondHz;
        const int facteurVoulu = plafonne ? 2 : 4;

        // La bascule est un changement de POINTEUR entre deux blocs : aucun
        // etat du chemin precedent n'est lu par le nouveau, aucun clic ne peut
        // venir d'ici (AC3 de US-09). Le reset des oversampleurs evite de
        // replier l'historique d'un chemin dans l'autre.
        //
        // Garde-fou : avant le premier `prepare`, les oversampleurs n'existent
        // pas encore — le constructeur du processeur appelle
        // `refreshDerivedSettings()`, qui appelle cette fonction. Dans ce cas
        // le chemin HQ reste inactif (latence 0), ce qui est aussi l'AC2.
        if (oversampler4x == nullptr || oversampler2x == nullptr)
        {
            hqActive = false;
            hqLatency = 0.0f;
            return;
        }

        if (hqRequested != hqActive || facteurVoulu != hqFactor)
        {
            hqActive = hqRequested;
            hqFactor = facteurVoulu;
            hqLatency = hqActive
                            ? (float) (facteurVoulu == 4 ? oversampler4x->getLatencyInSamples()
                                                         : oversampler2x->getLatencyInSamples())
                            : 0.0f;

            if (hqRequested)
            {
                oversampler4x->reset();
                oversampler2x->reset();
            }
        }
    }

    float Drive::saturate (float x) const noexcept
    {
        switch (Drive::kConsole)
        {
            case kTape:
            {
                // Genou plus doux que tanh : sature plus tard, distorsion plus
                // douce, et le passe-bas qui suit (applique hors de cette
                // fonction) retire le haut.
                const float s = kTapeDriveKnee * x;
                return s / (1.0f + std::abs (s));
            }

            case kTube:
            {
                // Terme carre : asymetrie -> harmoniques PAIRES (le 2f que ni
                // Console ni Tape ne produisent). L'offset qu'il cree est
                // retire par le DC blocker, hors de cette fonction.
                const float t = std::tanh (x);
                return t * (1.0f + kTubeSecondHarm * t);
            }

            case kFuzz:
                // Clip dur polarise : la polarisation est ce qui distingue un
                // fuzz d'un simple ecrêtage symetrique.
                return juce::jlimit (-1.0f, 1.0f, x + kFuzzBias);

            case kConsole:
            default:
                // `tanh` nu : l'engin retenu (impairs seuls).
                return std::tanh (x);
        }
    }

    float Drive::processSample (float* const* channels, int sampleIndex)
    {
        const int n = numChannelsToProcess;

        // --- Transparence BIT-EXACTE (AC2 : mix 0 %, ou amount 0 %) ----------
        if (settings.inert)
            return channels[0][sampleIndex];

        const bool needsLowPass = Drive::kConsole == kTape;
        const bool needsDcBlock = Drive::kConsole == kTube || Drive::kConsole == kFuzz;
        const float mix = 1.0f;   // mix interne a 100 % : le curseur est le seul reglage

        for (int ch = 0; ch < n; ++ch)
        {
            const float dry = channels[ch][sampleIndex];

            float wet = normGain * saturate (driveGain * dry);

            if (needsLowPass)
                wet = tapeLowPass.process (wet, ch);

            if (needsDcBlock)
            {
                const auto c = (size_t) ch;
                const float y = wet - dcX1[c] + dcCoef * dcY1[c];
                dcX1[c] = wet;
                dcY1[c] = y;
                wet = y;
            }

            channels[ch][sampleIndex] = dry + mix * (wet - dry);
        }

        return channels[0][sampleIndex];
    }

    void Drive::processBlock (float* const* channels, int numChannels, int numSamples)
    {
        if (numSamples <= 0)
            return;

        // --- Chemin HORS HQ : le traitement historique, echantillon par
        //     echantillon, latency 0. C'est aussi le chemin du module inerte
        //     (transparence bit-exacte, AC2 de US-02).
        if (! hqActive || settings.inert)
        {
            for (int i = 0; i < numSamples; ++i)
                processSample (channels, i);

            return;
        }

        // --- Chemin HQ (F2.2) -------------------------------------------------
        // L'entree du bloc est copiee dans le tampon HQ, upsamplee, saturee a
        // N x fs, re-downsamplee, puis le resultat est remonte sur place.
        const int numCh = juce::jmin (numChannels, numChannelsToProcess);

        // Decoupage interne en tranches de la capacite preparee (maxBlock) :
        // un hote honnete ne depasse pas maxBlock, mais on ne fait JAMAIS
        // reposer l'invariant sur la discipline de l'appelant — le tampon HQ
        // est dimensionne a maxBlock, un bloc plus grand serait un debordement
        // memoire. Les oversampleurs sont des filtres a etat traverses en
        // flux : trancher est STRICTEMENT equivalent au bloc unique.
        const int capacity = juce::jmax (1, hqBuffer.getNumSamples());

        for (int start = 0; start < numSamples; start += capacity)
            processHqChunk (channels, numCh, start, juce::jmin (capacity, numSamples - start));
    }

    void Drive::processHqChunk (float* const* channels, int numCh, int start, int count)
    {
        if (count <= 0)
            return;

        auto* os = (hqFactor == 4) ? oversampler4x.get() : oversampler2x.get();
        jassert (os != nullptr);

        // 1. Copie de l'entree dans le tampon HQ : la source de l'upsampling.
        //    (Le dry du mix interne est l'ENTREE du bloc, encore en place dans
        //    `channels` — le mix se fera a l'etape 5 contre cette trame.)
        for (int ch = 0; ch < numCh; ++ch)
            hqBuffer.copyFrom (ch, 0, channels[ch] + start, count);

        // 2. Upsample : le bloc retourne reference les buffers internes a N x fs.
        auto upBlock = os->processSamplesUp (
            juce::dsp::AudioBlock<const float> (hqBuffer.getArrayOfReadPointers(),
                                                (size_t) numCh, 0, (size_t) count));

        // 3. La partie SANS MEMOIRE du saturateur, a N x fs. Pas de passe-bas
        //    de Tape, pas de DC blocker ici : ce sont des filtres lineaires,
        //    ils ne replient rien et restent a fs (apres le downsample).
        const float gain = driveGain;
        const float norm = normGain;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* up = upBlock.getChannelPointer ((size_t) ch);

            for (int i = 0; i < count * hqFactor; ++i)
                up[i] = norm * saturate (gain * up[i]);
        }

        // 4. Downsample dans le tampon HQ. NOTE : le dry du mix interne est
        //    encore disponible — l'entree originale n'a pas bouge dans
        //    `channels` (le traitement HQ n'y ecrit qu'a l'etape 5).
        auto outBlock = juce::dsp::AudioBlock<float> (hqBuffer.getArrayOfWritePointers(),
                                                      (size_t) numCh, 0, (size_t) count);
        os->processSamplesDown (outBlock);

        // 5. Remontee sur place avec le mix interne : `out = dry + wet - dry`
        //    a mix 1,0 soit `wet` — mais formule pour rester l'equivalent
        //    exact du chemin par echantillon (`dry + mix*(wet-dry)`).
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* dst = channels[ch] + start;
            const float* wetBlock = hqBuffer.getReadPointer (ch);

            for (int i = 0; i < count; ++i)
                dst[i] = dst[i] + (wetBlock[i] - dst[i]);   // mix = 1.0f
        }

        // 6. Les filtres lineaires de saveur, a fs, sur le resultat. Pour
        //    Console (le moteur fige du produit) il n'y en a AUCUN — cette
        //    boucle ne coute donc rien dans la configuration livree ; elle est
        //    ecrite pour que les saveurs futures restent correctes.
        const bool needsLowPass = Drive::kConsole == kTape;
        const bool needsDcBlock = Drive::kConsole == kTube || Drive::kConsole == kFuzz;

        if (needsLowPass || needsDcBlock)
        {
            for (int i = 0; i < count; ++i)
            {
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float wet = channels[ch][start + i];

                    if (needsLowPass)
                        wet = tapeLowPass.process (wet, ch);

                    if (needsDcBlock)
                    {
                        const auto c = (size_t) ch;
                        const float y = wet - dcX1[c] + dcCoef * dcY1[c];
                        dcX1[c] = wet;
                        dcY1[c] = y;
                        wet = y;
                    }

                    channels[ch][start + i] = wet;
                }
            }
        }
    }
}
