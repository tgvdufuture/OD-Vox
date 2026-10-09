#include "EqCurveView.h"

#include "Palette.h"
#include "Skin.h"

#include <cmath>

/** La palette partagee (src/Palette.h). L'editeur la declare de la meme facon. */
namespace Palette = odvox::palette;

namespace
{
    juce::Colour colourOf (uint32_t argb) noexcept { return juce::Colour (argb); }

    /** L'encre de l'ecran LCD (moyenne mesuree sur la maquette). */
    constexpr uint32_t kLcdInk = 0xff111417;

    /** L'unite LCD : tous les traits de l'export Figma font 2 px. Tout element
        dessine tombe sur la grille paire, sinon le rendu devient flou
        (sous-pixel) — la meme regle que le mockup (tools/mockup_lcd.html). */
    constexpr int   kLcdUnit = 2;
    constexpr float kOn  = 3.0f * kLcdUnit;   // pointilles : 3 unites allumees,
    constexpr float kOff = 2.0f * kLcdUnit;   // 2 eteintes (mesure maquette)

    /** Pointilles uniformes, deux orientations seulement : horizontale (ligne
        0 dB) et verticale (grille des labels 100 / 1k / 10k). */
    void drawDashed (juce::Graphics& g, float x, float y, float length, bool vertical,
                     juce::Colour ink)
    {
        g.setColour (ink);

        for (float t = 0.0f; t < length; t += kOn + kOff)
        {
            const float run = juce::jmin (kOn, length - t);

            if (vertical) g.fillRect (x, y + t, (float) kLcdUnit, run);
            else          g.fillRect (x + t, y, run, (float) kLcdUnit);
        }
    }

    /** Un pas de courbe : trace en ESCALIER, comme le LCD de la maquette — le
        trace ne simule pas un vecteur qu'un ecran pixelise ne sait pas tenir. */
    void staircaseStep (juce::Path& path, float x, float y, float prevY)
    {
        path.lineTo (x, prevY);
        path.lineTo (x, y);
    }
}

EqCurveView::EqCurveView (ODVoxAudioProcessor& p)
    : processor (p), model (p, 1, 1)
{
    setOpaque (true);
    startTimerHz (kRefreshHz);
}

EqCurveView::~EqCurveView()
{
    stopTimer();
}

void EqCurveView::resized()
{
    model.setSize (getWidth(), getHeight());

    // L'ecran est dessine 1:1 (753x419, la taille native de la maquette) :
    // l'axe generique (20 Hz..20 kHz, ±15 dB) vit dans le PLOT mesure, sans
    // mise a l'echelle — les poignees tombent sur les positions de la photo
    // (les ancres 120/700/1750/10000 Hz y sont DEJA, meme loi log).
    if (odvox::skin::hasLcdBezel())
        model.setViewport (odvox::lcd::axisViewport());
}

float EqCurveView::yForDb (float db) const noexcept
{
    const float span = kSpectrumMaxDb - kSpectrumMinDb;
    const float t = juce::jlimit (0.0f, 1.0f, (kSpectrumMaxDb - db) / span);

    return t * (float) getHeight();
}

void EqCurveView::timerCallback()
{
    const int w = getWidth();

    if (w <= 0 || getHeight() <= 0)
        return;

    // Une colonne par pixel : c'est l'affichage qui dicte la resolution utile, et
    // l'analyseur agrege ses raies pour la respecter. Le calcul ne touche jamais
    // le thread audio (contrainte du §7 des risques).
    if (processor.updateSpectrum (w, odvox::eqcurve::kMaxHz))
        repaint();
}

// --- Dessin -----------------------------------------------------------------

void EqCurveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int h = bounds.getHeight();
    const float sr = processor.getSampleRate() > 0.0 ? (float) processor.getSampleRate()
                                                     : 48000.0f;

    const bool lcd = odvox::skin::hasLcdBezel();

    // --- Fond ---------------------------------------------------------------
    // Ecran LCD : le bezel Figma couvre toute la vue (taille native 753x419),
    // la vitre y est deja dessinee. Sans asset, repli : fond uni de la palette.
    if (lcd)
        g.drawImage (odvox::skin::lcdBezel(), bounds.toFloat());
    else
        g.fillAll (colourOf (Palette::track));

    // Le rectangle de l'AXE dans la vue, en pixels : le viewport LCD (en
    // fractions de l'image 753x419) multiplie par la taille reelle — dessinee
    // 1:1, les fractions retombent sur les pixels mesures. En repli (sans
    // asset), l'axe remplit la vue.
    const float vw = (float) getWidth();
    const float vh = (float) getHeight();
    const auto fractions = lcd ? odvox::lcd::axisViewport()
                               : juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f);
    const juce::Rectangle<float> axis (fractions.getX() * vw,
                                       fractions.getY() * vh,
                                       fractions.getWidth() * vw,
                                       fractions.getHeight() * vh);

    // Le PLOT physique : le cadre interieur de l'ecran. L'axe des donnees
    // (20 Hz..20 kHz) est PLUS LARGE que le plot (qui couvre ~51 Hz..19 kHz) :
    // le dessin se confine au plot, les gestes restent sur l'axe.
    const juce::Rectangle<float> plot (odvox::lcd::kPlotX0, odvox::lcd::kPlotY0,
                                       odvox::lcd::kPlotX1 - odvox::lcd::kPlotX0,
                                       odvox::lcd::kPlotY1 - odvox::lcd::kPlotY0);

    // --- Grille verticale ----------------------------------------------------
    // Les trois reperes de la maquette (100 / 1k / 10k), pointilles verticaux
    // alignes grille LCD, sur TOUTE la hauteur du plot (comme la maquette).
    if (lcd)
    {
        for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = std::round (odvox::lcd::xForFrequency (hz) / 2.0f) * 2.0f;

            drawDashed (g, x, plot.getY() + 2.0f, plot.getHeight() - 4.0f, true,
                        colourOf (kLcdInk).withAlpha (0.55f));
        }
    }

    // --- Spectre -------------------------------------------------------------
    // Il se mappe sur la MEME loi log que l'axe : l'analyseur publie ses
    // colonnes en log de 20 Hz a kMaxHz, on les projette dans le viewport.
    const auto& columns = processor.spectrumDb();

    if (! columns.empty())
    {
        juce::Path spectrum;

        const float spectrumTop = lcd ? plot.getY() : 0.0f;
        const float spectrumBottom = lcd ? plot.getBottom() : (float) h;
        const float span = kSpectrumMaxDb - kSpectrumMinDb;

        // Les colonnes de l'analyseur couvrent 20 Hz..kMaxHz en log : chaque
        // colonne est placee a SA frequence sur l'axe de l'ecran (les premieres
        // tombent a gauche du plot et sont simplement clipees).
        const auto columnX = [&] (size_t i)
        {
            const float t = (float) (i + 0.5) / (float) columns.size();
            const float hz = odvox::eqcurve::kMinHz
                             * std::pow (10.0f, t * std::log10 (
                                                   odvox::eqcurve::kMaxHz
                                                       / odvox::eqcurve::kMinHz));
            return odvox::lcd::xForFrequency (hz);
        };

        spectrum.startNewSubPath (columnX (0), spectrumBottom);

        for (size_t i = 0; i < columns.size(); ++i)
        {
            const float dbv = juce::jlimit (kSpectrumMinDb, kSpectrumMaxDb, columns[i]);
            const float y = spectrumTop
                            + (kSpectrumMaxDb - dbv) / span
                                  * (spectrumBottom - spectrumTop);

            spectrum.lineTo (columnX (i), y);
        }

        spectrum.lineTo (columnX (columns.size() - 1), spectrumBottom);
        spectrum.closeSubPath();

        // Le spectre passe DERRIERE la courbe, et volontairement discret : il
        // informe, il ne doit pas noyer le trace qu'on edite. Sur le LCD, une
        // encre grise semi-transparente, comme un tracé sous le verre.
        const auto ink = lcd ? colourOf (kLcdInk).withAlpha (0.18f) : juce::Colour();
        g.setColour (lcd ? ink : colourOf (Palette::accentCold).withAlpha (0.28f));
        g.fillPath (spectrum);

        if (! lcd)
        {
            g.setColour (colourOf (Palette::accentCold).withAlpha (0.55f));
            g.strokePath (spectrum, juce::PathStrokeType (1.0f));
        }
    }

    // --- Ligne 0 dB ----------------------------------------------------------
    // Pointilles HORIZONTAUX uniformes (la maquette les dessinait a la main,
    // irreguliers ; le code les tient proprement — cf. mockup).
    if (lcd)
    {
        drawDashed (g, plot.getX() + 1.0f, odvox::lcd::kZeroY - 1.0f,
                    plot.getWidth() - 2.0f, false,
                    colourOf (kLcdInk).withAlpha (0.8f));
    }

    // --- Courbe de reponse ---------------------------------------------------
    // Elle vient de `Eq::responseDb`, donc des MEMES coefficients que les
    // filtres : ce qui est dessine est ce qui est entendu (AC2 de US-04).
    const auto settings = model.settings();

    bool enabled = true;

    if (auto* eqOn = processor.state().getRawParameterValue ("eq_on"))
        enabled = eqOn->load() >= 0.5f;

    juce::Path curve;

    if (lcd)
    {
        // Escalier sur la grille LCD de 2 px, du bord au bord du PLOT : la
        // courbe couvre la plage affichable (~51 Hz..19 kHz), pas l'axe des
        // donnees (20 Hz..20 kHz) qui deborderait du cadre.
        const int x0 = (int) plot.getX() + 2;
        const int x1 = (int) plot.getRight() - 2;

        float prevY = -1.0f;

        for (int x = x0; x <= x1; x += kLcdUnit)
        {
            const float hz = odvox::lcd::frequencyForX ((float) x);
            const float y = std::round (odvox::lcd::yForGain (
                                            odvox::Eq::responseDb (settings, sr, hz))
                                        / 2.0f) * 2.0f;

            if (prevY < 0.0f)
                curve.startNewSubPath ((float) x, y);
            else
                staircaseStep (curve, (float) x, y, prevY);

            prevY = y;
        }
    }
    else
    {
        const int steps = juce::jmax (2, bounds.getWidth());

        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i;
            const float hz = odvox::eqcurve::frequencyForX (x, (float) bounds.getWidth());
            const float db = odvox::Eq::responseDb (settings, sr, hz);
            const float y = odvox::eqcurve::yForGain (db, (float) h) - (float) bounds.getY();

            if (i == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }
    }

    // Hors cadre, la courbe se prolonge : un passe-haut a 40 Hz sort par le bas
    // de l'echelle, et il vaut mieux le voir sortir que le voir disparaitre.
    g.saveState();
    g.reduceClipRegion (lcd ? plot.getSmallestIntegerContainer().getIntersection (bounds)
                            : bounds);
    g.setColour (lcd ? colourOf (kLcdInk)
                     : colourOf (Palette::accent).withAlpha (enabled ? 1.0f : 0.35f));
    g.strokePath (curve, juce::PathStrokeType (lcd ? 2.0f : 2.0f));
    g.restoreState();

    if (! enabled && ! lcd)
    {
        g.setColour (colourOf (Palette::textDim));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("EQ off", bounds.reduced (6), juce::Justification::topRight, false);
    }

    // --- Poignees ------------------------------------------------------------
    for (int b = 0; b < odvox::Eq::kNumBands; ++b)
    {
        const auto handle = model.handlePosition (b);
        const bool active = b == model.activeBand() || b == hovered;

        if (lcd)
        {
            // Le carre plein de la maquette : 10 unites LCD (20 px), centre sur
            // la poignee, aligne sur la grille paire.
            constexpr float side = 10.0f * kLcdUnit;
            const float cx = std::round (handle.x / 2.0f) * 2.0f;
            const float cy = std::round (handle.y / 2.0f) * 2.0f;

            g.setColour (colourOf (kLcdInk).withAlpha (active ? 1.0f : 0.92f));
            g.fillRect (cx - side * 0.5f, cy - side * 0.5f, side, side);
        }
        else
        {
            const float radius = active ? 7.0f : 5.0f;
            const auto colour = colourOf (odvox::palette::band[(size_t) b]);

            g.setColour (colour.withAlpha (active ? 1.0f : 0.85f));
            g.fillEllipse (handle.x - radius, handle.y - radius, radius * 2.0f, radius * 2.0f);

            g.setColour (colourOf (Palette::text).withAlpha (active ? 0.9f : 0.45f));
            g.drawEllipse (handle.x - radius, handle.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
        }
    }

    // --- Etiquette de la bande saisie ---------------------------------------
    if (const int band = model.activeBand(); band >= 0)
    {
        const auto handle = model.handlePosition (band);
        const auto text = juce::String (juce::roundToInt (model.frequencyOf (band)))
                          + " Hz   "
                          + juce::String (model.gainOf (band), 1) + " dB";

        const juce::Rectangle<int> box ((int) handle.x - 70, (int) handle.y - 26, 140, 18);

        g.setColour (lcd ? colourOf (0xe6f2f2ee)
                         : colourOf (Palette::background).withAlpha (0.85f));
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (colourOf (Palette::text));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (text, box, juce::Justification::centred, false);
    }
}

// --- Gestes -----------------------------------------------------------------

void EqCurveView::mouseDown (const juce::MouseEvent& e)
{
    const auto point = e.position;

    if (e.mods.isPopupMenu())
    {
        const int band = model.bandAt ({ point.x, point.y });

        if (band >= 0)
            showTypeMenu (band, e.getScreenPosition());

        return;
    }

    if (model.beginGesture ({ point.x, point.y }))
        repaint();
}

void EqCurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (model.dragTo ({ e.position.x, e.position.y }))
        repaint();
}

void EqCurveView::mouseUp (const juce::MouseEvent&)
{
    model.endGesture();
    repaint();
}

void EqCurveView::mouseMove (const juce::MouseEvent& e)
{
    const int band = model.bandAt ({ e.position.x, e.position.y });

    if (band != hovered)
    {
        hovered = band;
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor
                                  : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqCurveView::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void EqCurveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = hovered >= 0 ? hovered
                                  : model.bandAt ({ e.position.x, e.position.y });

    if (band < 0)
        return;

    // La largeur de bande se regle a la molette : c'est le geste qu'exige F1.6c,
    // et il n'entre pas en conflit avec le glisser-deposer, qui tient deja
    // frequence et gain. (No-op depuis le figeage des ancres : le modele
    // refuse, la vue ne se repeint pas.)
    if (model.adjustQ (band, wheel.deltaY))
        repaint();
}

void EqCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = model.bandAt ({ e.position.x, e.position.y });

    if (band >= 0 && model.resetBand (band))
        repaint();
}

void EqCurveView::showTypeMenu (int band, juce::Point<int> where)
{
    juce::PopupMenu menu;

    static const char* const types[] = { "Bell", "Low Shelf", "High Shelf", "High Pass" };
    const int current = model.typeOf (band);

    for (int i = 0; i < 4; ++i)
        menu.addItem (i + 1, types[i], true, i == current);

    // La fermeture du menu est asynchrone : on capture l'index de bande par
    // valeur, sinon il dependrait de l'etat au moment du clic.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ where.x, where.y, 1, 1 }),
                        [this, band] (int result)
                        {
                            if (result > 0 && model.setBandType (band, result - 1))
                                repaint();
                        });
}
