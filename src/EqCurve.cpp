#include "EqCurve.h"

#include <cmath>

namespace odvox
{
    // --- Geometrie de l'ecran LCD (mesuree sur docs/assets/LCD_fige.png) -----

    namespace lcd
    {
        float xForFrequency (float hz) noexcept
        {
            const float clamped = juce::jlimit (20.0f, 20000.0f, hz);
            return kXAt100Hz + std::log10 (clamped / 100.0f) * kDecadePx;
        }

        float frequencyForX (float x) noexcept
        {
            return 100.0f * std::pow (10.0f, (x - kXAt100Hz) / kDecadePx);
        }

        float yForGain (float db) noexcept
        {
            return kZeroY - db * kPxPerDb;
        }

        float gainForY (float y) noexcept
        {
            return (kZeroY - y) / kPxPerDb;
        }

        juce::Rectangle<float> axisViewport() noexcept
        {
            // La projection de l'axe GENERIQUE (20 Hz..20 kHz, ±15 dB) dans
            // l'ecran, en FRACTIONS de l'image 753x419 : le modele multiplie
            // par la taille reelle de la vue (dessinee 1:1, les fractions
            // retombent sur les pixels mesures).
            const float x0 = xForFrequency (eqcurve::kMinHz) / kWidth;
            const float x1 = xForFrequency (eqcurve::kMaxHz) / kWidth;
            const float y0 = yForGain (eqcurve::kMaxGainDb) / kHeight;
            const float y1 = yForGain (-eqcurve::kMaxGainDb) / kHeight;

            return { x0, y0, x1 - x0, y1 - y0 };
        }
    }

    namespace eqcurve
    {
        const char* const kBandKeys[Eq::kNumBands] = { "low", "mid", "hi", "air" };

        float frequencyForX (float x, float width) noexcept
        {
            if (width <= 0.0f)
                return kMinHz;

            const float t = juce::jlimit (0.0f, 1.0f, x / width);
            const float lo = std::log (kMinHz);
            const float hi = std::log (kMaxHz);

            return std::exp (lo + t * (hi - lo));
        }

        float xForFrequency (float hz, float width) noexcept
        {
            const float lo = std::log (kMinHz);
            const float hi = std::log (kMaxHz);
            const float clamped = juce::jlimit (kMinHz, kMaxHz, hz);
            const float t = (std::log (clamped) - lo) / (hi - lo);

            return t * width;
        }

        float gainForY (float y, float height) noexcept
        {
            if (height <= 0.0f)
                return 0.0f;

            const float t = juce::jlimit (0.0f, 1.0f, y / height);

            // Haut = fort : l'axe est celui de la courbe, pas celui d'un tableau.
            return kMaxGainDb * (1.0f - 2.0f * t);
        }

        float yForGain (float gainDb, float height) noexcept
        {
            const float clamped = juce::jlimit (-kMaxGainDb, kMaxGainDb, gainDb);
            const float t = (kMaxGainDb - clamped) / (2.0f * kMaxGainDb);

            return t * height;
        }
    }

    // --- EqCurveModel -------------------------------------------------------

    EqCurveModel::EqCurveModel (ODVoxAudioProcessor& p, int w, int h)
        : processor (p), width (juce::jmax (1, w)), height (juce::jmax (1, h))
    {
    }

    void EqCurveModel::setSize (int w, int h) noexcept
    {
        width  = juce::jmax (1, w);
        height = juce::jmax (1, h);
    }

    void EqCurveModel::setViewport (juce::Rectangle<float> axisFractions) noexcept
    {
        viewport = axisFractions;
    }

    juce::String EqCurveModel::idFor (int band, const char* setting)
    {
        const int b = juce::jlimit (0, Eq::kNumBands - 1, band);

        return juce::String ("eq_") + eqcurve::kBandKeys[b] + "_" + setting;
    }

    int EqCurveModel::typeOf (int band) const
    {
        // Type FIGE par bande (rev. suppression du mode Avance) : chaque bande
        // est pitch-suiveuse autour de son ancre, les types sont de conception.
        return juce::isPositiveAndBelow (band, Eq::kNumBands)
                   ? Eq::kAnchorType[band] : 0;
    }

    float EqCurveModel::gainOf (int band) const
    {
        if (auto* value = processor.state().getRawParameterValue (idFor (band, "db")))
            return value->load();

        return 0.0f;
    }

    float EqCurveModel::frequencyOf (int band) const
    {
        // Ancre FIGEE (rev. suppression du mode Avance). Le deplacement live
        // (accrochage pitch) reste visible : la vue lit trackedFrequencyHz.
        return juce::isPositiveAndBelow (band, Eq::kNumBands)
                   ? Eq::kAnchorHz[band] : 1000.0f;
    }

    float EqCurveModel::qOf (int band) const
    {
        // Q FIGE par bande (rev. suppression du mode Avance).
        return juce::isPositiveAndBelow (band, Eq::kNumBands)
                   ? Eq::kAnchorQ[band] : 1.0f;
    }

    Eq::Settings EqCurveModel::settings() const
    {
        Eq::Settings s;

        for (int b = 0; b < Eq::kNumBands; ++b)
        {
            auto& band = s.bands[b];
            band.freqHz = frequencyOf (b);
            band.gainDb = gainOf (b);
            band.q      = qOf (b);
            band.type   = typeOf (b);
        }

        s.enabled = true;
        s.inert   = false;

        return s;
    }

    EqCurveModel::Point EqCurveModel::handlePosition (int band) const
    {
        return mappedHandle (band);
    }

    EqCurveModel::Point EqCurveModel::mappedHandle (int band) const
    {
        // Position GENERIQUE -> pixel de la vue : les mappages eqcurve
        // retournent des PIXELS (t * taille), on les ramene en FRACTIONS
        // d'axe, on les place DANS le viewport (lui-meme en fractions de la
        // vue), puis on multiplie par la taille de la vue. Sur l'ecran LCD, le
        // viewport est le plot mesure : les ancres figees du DSP
        // (120/700/1750/10000 Hz) tombent sur les positions de la maquette.
        const float nx = eqcurve::xForFrequency (frequencyOf (band), (float) width)
                         / juce::jmax (1.0f, (float) width);
        const float ny = eqcurve::yForGain (gainOf (band), (float) height)
                         / juce::jmax (1.0f, (float) height);

        const float fx = viewport.getX() + nx * viewport.getWidth();
        const float fy = viewport.getY() + ny * viewport.getHeight();

        return { fx * (float) width, fy * (float) height };
    }

    int EqCurveModel::bandAt (Point p) const
    {
        int best = -1;
        float bestDistance = eqcurve::kHitRadius;

        for (int b = 0; b < Eq::kNumBands; ++b)
        {
            const auto h = mappedHandle (b);
            const float dx = h.x - p.x;
            const float dy = h.y - p.y;
            const float distance = std::sqrt (dx * dx + dy * dy);

            // A egalite de distance, la bande la plus GRAVE l'emporte : deux
            // poignees empilees (meme gain) sont departagees de facon stable,
            // sinon la saisie dependrait de l'ordre de parcours.
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = b;
            }
        }

        return best;
    }

    void EqCurveModel::write (int band, const char* setting, float actual) const
    {
        const juce::String id = idFor (band, setting);

        for (const auto& info : params::declared())
            if (id == juce::String (info.id))
            {
                processor.setParameterActual (id, juce::jlimit (info.min, info.max, actual));
                return;
            }
    }

    bool EqCurveModel::beginGesture (Point p)
    {
        const int band = bandAt (p);

        if (band < 0)
            return false;

        active = band;
        dragging = true;

        // L'hote enregistre une automation par geste : sans ces bornes, un
        // glisser-deposer se perdrait dans un flux de valeurs sans debut ni fin.
        if (auto* param = processor.state().getParameter (idFor (band, "db")))
            param->beginChangeGesture();

        return true;
    }

    bool EqCurveModel::dragTo (Point p)
    {
        if (! dragging || active < 0)
            return false;

        // Le geste ecrit la position ABSOLUE du pointeur : la poignee suit la
        // souris. C'est ce qu'un glisser-deposer veut dire sur une courbe — une
        // fois saisie, la bande se place ou l'on vise.
        //
        // rev. suppression du mode Avance : l'axe X ne s'ECRIT plus — les
        // bandes sont pitch-suiveuses autour d'ancres figees, un drag de
        // frequence contredirait le suivi. Le geste ne porte que le gain.
        //
        // Le pointeur (pixels de la vue) est ramene en FRACTION DE LA VUE
        // (0..1), puis en fraction d'axe via le viewport, et enfin lu en gain.
        // Hors du viewport, la valeur est bornee par le clamp du catalogue
        // (meme contrat que les rotaries).
        const float vy = p.y / juce::jmax (1.0f, (float) height);
        const float ny = (vy - viewport.getY()) / juce::jmax (1.0e-6f, viewport.getHeight());
        write (active, "db", eqcurve::gainForY (ny, 1.0f));

        return true;
    }

    void EqCurveModel::endGesture()
    {
        if (active >= 0)
        {
            // L'ordre n'importe pas, mais l'equilibre si : chaque geste ouvert
            // doit etre referme, sinon l'hote garde une automation en cours.
            if (auto* param = processor.state().getParameter (idFor (active, "db")))
                param->endChangeGesture();
        }

        dragging = false;
        active = -1;
    }

    bool EqCurveModel::adjustQ (int band, float wheelDeltaY)
    {
        // Le Q n'est plus un reglage (rev. suppression du mode Avance) : Q fige
        // par bande, la molette ne fait plus rien.
        (void) band;
        (void) wheelDeltaY;
        return false;
    }

    bool EqCurveModel::resetBand (int band)
    {
        if (! juce::isPositiveAndBelow (band, Eq::kNumBands))
            return false;

        // Seul le GAIN se reinitialise (0 dB) : frequence, Q et type sont figes
        // par conception (rev. suppression du mode Avance).
        const juce::String id = idFor (band, "db");

        for (const auto& info : params::declared())
            if (id == juce::String (info.id))
            {
                if (auto* param = processor.state().getParameter (id))
                    param->beginChangeGesture();

                write (band, "db", info.defaultValue);

                if (auto* param = processor.state().getParameter (id))
                    param->endChangeGesture();

                break;
            }

        return true;
    }

    bool EqCurveModel::setBandType (int band, int typeIndex)
    {
        // Le type n'est plus un reglage (rev. suppression du mode Avance) :
        // fige par bande, ce clavier ne fait plus rien.
        (void) band;
        (void) typeIndex;
        return false;
    }
}
