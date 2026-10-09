#include "Delay.h"

#include <algorithm>
#include <cmath>

namespace odvox
{
    namespace
    {
        // Les 21 divisions, en NOIRS (1 noir = une noire). Une double-croche
        // vaut 1/8 de noir (sa triole 1/12), une mesure vaut 4 noirs, `2/1`
        // en vaut 8. Verifie contre la mesure a 120 BPM
        // (noir = 0,5 s) : 1/4T -> 0,333 s, 1/4 -> 0,500 s, 1/4D -> 0,750 s,
        // 1/2T -> 0,667 s, 1/2 -> 1,000 s, 1/1T -> 1,333 s, 1/2D -> 1,500 s :
        // tous exacts.
        constexpr float kDivisionBeats[Delay::kNumDivisions] =
        {
            1.0f / 12.0f,   // 1/32T
            1.0f / 8.0f,    // 1/32
            3.0f / 16.0f,   // 1/32D
            1.0f / 6.0f,    // 1/16T
            1.0f / 4.0f,    // 1/16
            3.0f / 8.0f,    // 1/16D
            1.0f / 3.0f,    // 1/8T
            1.0f / 2.0f,    // 1/8
            3.0f / 4.0f,    // 1/8D
            2.0f / 3.0f,    // 1/4T
            1.0f,           // 1/4
            3.0f / 2.0f,    // 1/4D
            4.0f / 3.0f,    // 1/2T
            2.0f,           // 1/2
            3.0f,           // 1/2D
            8.0f / 3.0f,    // 1/1T
            4.0f,           // 1/1
            6.0f,           // 1/1D
            16.0f / 3.0f,   // 2/1T
            8.0f,           // 2/1
            12.0f,          // 2/1D
        };

        float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

        float onePoleCoef (float ms, double sampleRate)
        {
            return ms <= 0.0f ? 0.0f
                              : std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
        }

        // Le tilt humide : duo shelf bas / shelf haut croises a 1 kHz, NEUTRE
        // a kNeutralFilter (39 %, valeur retenue) : en
        // dessous, le shelf bas descend jusqu'a -6 dB (echo sombre) ; au-
        // dessus, le shelf haut monte jusqu'a +6 dB (echo clair) ; entre les
        // deux, le tilt passe par un point exactement plat.
        void setWetTilt (Biquad& lowShelf, Biquad& highShelf,
                         double sampleRate, float filter01)
        {
            const float f = juce::jlimit (0.0f, 1.0f, filter01);
            const float lowGainDb  = f < Delay::kNeutralFilter
                                         ? -6.0f * (1.0f - f / Delay::kNeutralFilter)
                                         : 0.0f;
            const float highGainDb = f > Delay::kNeutralFilter
                                         ? 6.0f * (f - Delay::kNeutralFilter)
                                                       / (1.0f - Delay::kNeutralFilter)
                                         : 0.0f;

            const auto l = juce::IIRCoefficients::makeLowShelf (
                sampleRate, 1000.0, 0.7071067811865476, dbToGain (lowGainDb));
            lowShelf.set (l.coefficients[0], l.coefficients[1], l.coefficients[2],
                          l.coefficients[3], l.coefficients[4]);

            const auto h = juce::IIRCoefficients::makeHighShelf (
                sampleRate, 1000.0, 0.7071067811865476, dbToGain (highGainDb));
            highShelf.set (h.coefficients[0], h.coefficients[1], h.coefficients[2],
                           h.coefficients[3], h.coefficients[4]);
        }
    }

    float Delay::divisionBeats (int index) noexcept
    {
        return kDivisionBeats[juce::jlimit (0, kNumDivisions - 1, index)];
    }

    void Delay::prepare (double sampleRate, int numChannels)
    {
        sampleRateD = sampleRate > 0.0 ? sampleRate : 48000.0;
        numChannelsToProcess = juce::jmax (1, numChannels);

        // La ligne couvre le plafond de temps plus la marge d'interpolation.
        lineLength = (int) std::ceil (kMaxDelaySeconds * sampleRateD) + 4;
        lineL.assign ((size_t) lineLength, 0.0f);
        lineR.assign ((size_t) lineLength, 0.0f);
        writePos = 0;

        const auto hp = juce::IIRCoefficients::makeHighPass (sampleRateD, 35.0, 0.7071067811865476);
        loopHighPass.set (hp.coefficients[0], hp.coefficients[1], hp.coefficients[2],
                          hp.coefficients[3], hp.coefficients[4]);

        const auto lp = juce::IIRCoefficients::makeLowPass (sampleRateD, 7500.0, 0.7071067811865476);
        loopLowPass.set (lp.coefficients[0], lp.coefficients[1], lp.coefficients[2],
                         lp.coefficients[3], lp.coefficients[4]);

        duckAttackCoef  = 1.0f - onePoleCoef (kDuckAttackMs, sampleRateD);
        duckReleaseCoef = 1.0f - onePoleCoef (kDuckReleaseMs, sampleRateD);
        timeGlideCoef   = onePoleCoef (kTimeGlideMs, sampleRateD);

        builtFilter01 = -1.0f;

        reset();
    }

    void Delay::reset()
    {
        // Appele AVANT prepare par certains chemins (constructeur du hote) :
        // les lignes n'existent pas encore, il n'y a rien a effacer.
        if (lineLength > 0)
        {
            juce::FloatVectorOperations::clear (lineL.data(), lineLength);
            juce::FloatVectorOperations::clear (lineR.data(), lineLength);
        }
        writePos = 0;

        // Le temps repart a zero : le premier `beginBlock` s'y accroche
        // directement (sans glide) — c'est ce qui rend deux rendus du meme
        // signal reproductibles au bit pres, quel que soit l'etat d'avant.
        delaySamples       = 0.0f;
        delayTargetSamples = 0.0f;
        duckRamp           = 0.0f;
        duckGain           = 1.0f;
    }

    void Delay::setSettings (const Settings& s)
    {
        settings = s;
    }

    void Delay::beginBlock (const float* const* /*channels*/, int /*numChannels*/, int /*numSamples*/)
    {
        if (sampleRateD <= 0.0)
            return;

        // --- Cible de temps (division x tempo, ou temps libre) -------------
        const double beatSeconds = 60.0 / juce::jmax (20.0, settings.bpm);
        const double seconds = settings.sync
                                   ? (double) divisionBeats (settings.division) * beatSeconds
                                   : (double) settings.freeMs * 0.001;

        delayTargetSamples = (float) juce::jlimit (1.0,
                                                   (double) kMaxDelaySeconds * sampleRateD,
                                                   seconds * sampleRateD);

        // Premier bloc apres prepare/reset : on s'accroche a la cible sans
        // glide (le glide partant de zero balaierait un peigne de 15 ms).
        if (delaySamples <= 0.0f)
            delaySamples = delayTargetSamples;

        // --- Filtre humide, reconstruit seulement si le timbre a bouge -----
        if (builtFilter01 != settings.filter01)
        {
            setWetTilt (wetLowShelf, wetHighShelf, sampleRateD, settings.filter01);
            builtFilter01 = settings.filter01;
        }
    }

    float Delay::currentDelayMs() const noexcept
    {
        return delaySamples / (float) sampleRateD * 1000.0f;
    }

    float Delay::processSample (float* const* channels, int sampleIndex)
    {
        const int n = numChannelsToProcess;

        // --- Transparence : court-circuit complet a 0 % (bit-exact) --------
        if (settings.inert || settings.amount01 <= 0.0f)
            return channels[0][sampleIndex];

        // --- Sec : jamais touche -------------------------------------------
        const float dryL = channels[0][sampleIndex];
        const float dryR = n > 1 ? channels[1][sampleIndex] : dryL;

        // --- Niveau d'entree du ducking (puissance moyenne stéréo) ---------
        const float inSq = 0.5f * (dryL * dryL + dryR * dryR);

        // --- Lecture des deux lignes (interpolation lineaire) --------------
        const float readPos     = (float) writePos - delaySamples;
        const float readWrapped = readPos < 0.0f ? readPos + (float) lineLength : readPos;
        const int   i0   = (int) readWrapped;
        const int   i1   = (i0 + 1) % lineLength;
        const float frac = readWrapped - (float) i0;

        const float tapL = lineL[(size_t) i0] * (1.0f - frac) + lineL[(size_t) i1] * frac;
        const float tapR = lineR[(size_t) i0] * (1.0f - frac) + lineR[(size_t) i1] * frac;

        // --- Filtre humide (HORS boucle : colore l'echo, ne peut pas diverger)
        const float wetL0 = wetLowShelf.process (wetHighShelf.process (tapL, 0), 0);
        const float wetR0 = n > 1 ? wetLowShelf.process (wetHighShelf.process (tapR, 1), 1)
                                  : wetL0;

        // --- Ducking : rampe LISSEE sur le chemin HUMIDE seul ---------------
        // La rampe (0..1) est la grandeur lissée (attaque 5 ms, release
        // 180 ms) : c'est elle qui pilote le gain, pas l'enveloppe. C'est ce
        // qui garantit AC2 — 300 ms apres la fin du mot, la rampe a decru de
        // e^(-300/180) = 19 %, soit un duck residual de −1,5 dB, inaudible.
        // Lisser le niveau PUIS recalculer la rampe ferait persister le duck
        // des secondes (defaut corrige 2026-09-20, trouve en reecrivant le
        // test). Le niveau sec n'est JAMAIS touche.
        const float levelDb    = 10.0f * std::log10 (juce::jmax (1.0e-20f, inSq));
        const float rampTarget = juce::jlimit (0.0f, 1.0f,
                                               (levelDb - kDuckFloorDb)
                                                   / (kDuckFullDb - kDuckFloorDb));
        const float rampCoef = rampTarget > duckRamp ? duckAttackCoef : duckReleaseCoef;
        duckRamp += rampCoef * (rampTarget - duckRamp);

        const float duckTarget = dbToGain (-kDuckDepthDb
                                           * juce::jlimit (0.0f, 1.0f, settings.duck01)
                                           * duckRamp);
        duckGain += 0.25f * (duckTarget - duckGain);

        const float wetL = wetL0 * duckGain * settings.amount01;
        const float wetR = wetR0 * duckGain * settings.amount01;

        // --- Ecriture : le sec (somme mono) entre dans L seule, la
        // retroaction CROISEE fait alterner les echoes de canal a canal
        // (ping-pong mesure au signal). Defaut corrige le
        // 2026-09-20 : le sec etait ecrit dans LES DEUX lignes, les echoes
        // sortaient donc identiques en L et R — un double delay, pas un
        // ping-pong. Boucle AMORTIE (passe-haut 35 Hz + passe-bas 7,5 kHz,
        // gain < 1 sur tout le spectre) : le gain de boucle vaut au plus
        // feedback01 < 1, la serie decroit toujours — l'emballement est
        // impossible PAR CONSTRUCTION (AC3).
        const float fb    = juce::jlimit (0.0f, kMaxFeedback, settings.feedback01);
        const float fbL   = loopHighPass.process (loopLowPass.process (tapL, 0), 0);
        const float fbR   = loopHighPass.process (loopLowPass.process (tapR, 1), 1);
        const float inputL = n > 1 ? 0.5f * (dryL + dryR) : dryL;

        lineL[(size_t) writePos] = inputL + fb * fbR;
        lineR[(size_t) writePos] = fb * fbL;

        // --- Lissage du temps (changement de tempo sans clic) --------------
        writePos = (writePos + 1) % lineLength;
        delaySamples += timeGlideCoef * (delayTargetSamples - delaySamples);

        // --- Somme sec + echoes --------------------------------------------
        channels[0][sampleIndex] = dryL + wetL;
        if (n > 1)
            channels[1][sampleIndex] = dryR + wetR;

        return channels[0][sampleIndex];
    }
}
