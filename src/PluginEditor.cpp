#include "PluginEditor.h"

#include "EqCurve.h"
#include "Metal.h"
#include "Skin.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace metal = odvox::metal;   // la classe editeur est au niveau global
namespace skin  = odvox::skin;    // sprites Figma (src/Skin.h)

namespace
{
    // =====================================================================
    // DISPOSITION « panneau unique » — ESSAI du 2026-10-02
    // =====================================================================
    // Demande utilisateur : masquer le LCD, mettre le knob COMP a GAUCHE au
    // centre, et empiler TOUTES les autres bandes les unes sur les autres a
    // DROITE (colonnes empilees).
    //
    // L'ANCIENNE disposition (« cartes V6 » : heros 805x196 avec le LCD a
    // droite, grille 2x2 de bandes en dessous) est consignee en commentaire
    // sous chaque constante : revenir en arriere = remettre les anciennes
    // valeurs ET kLcdEnabled = true. Aucun code n'a ete supprime.
    //
    // La photo de la pedale vit a x75..890, y30..465 (echelle editeur 980x516) :
    // la colonne de gauche occupe x85..215, la colonne de bandes x228..890, et
    // les deux partagent la meme hauteur y138..438 (les 4 bandes de 69 px
    // empilees, gap 8 : 4*69 + 3*8 = 300).
    constexpr int kBarX    = 95;
    constexpr int kBarY    = 44;
    constexpr int kBarH    = 54;
    constexpr int kCapH    = 34;
    constexpr int kLcdScaleNum = 44;    // LCD a 0,44 : 753x419 -> ~331x184
    constexpr int kLcdScaleDen = 100;

    /** Le LCD (ecran EQ) est-il affiche ? L'essai du 2026-10-02 le masque :
        la place est prise par les bandes empilees de droite. Le composant
        reste construit et cable (aucune vue desordonnee) — seul son affichage
        change, donc le retour arriere est une seule ligne. */
    constexpr bool kLcdEnabled = false;

    // MOITIE / MOITIE (retour utilisateur du 2026-10-02 : « 1/2 du plugin pour
    // comp et l'autre sur les bandes »). La zone utile fait 805 px (x85..890) :
    // deux sections de 398 px et un trait de 8 px entre elles — 398 + 8 + 398
    // = 804, il reste 1 px au bord droit. Le heros passe donc de 467 a 398 px
    // de large, les bandes de 330 a 398.
    constexpr int kHeroX      = 85;    // ancien : 85
    constexpr int kHeroY      = 138;   // ancien : 112
    constexpr int kHeroW      = 398;   // ancien : 467 (puis 130, puis 805)
    constexpr int kHeroH      = 300;   // ancien : 196
    // Le COMP du heros : disque 200 px centre dans la colonne (284,252) ;
    // l'ecran de valeur 200x22 CENTRE DESSOUS, le nom grave sous l'ecran, les
    // deux toggles sur UNE ligne en dessous. Les origins de COMPOSANT valent
    // origin de disque - 8 (padding interne du Rotary).
    constexpr int kCompKnobX  = 176;   // ancien : 210, puis 94 (disque 96), puis 257
    constexpr int kCompKnobY  = 151;   // ancien : 155, puis 200
    constexpr int kCompKnobSz = 200;   // ancien : 96
    constexpr int kCompValX   = 184;   // ancien : 210, puis 287, puis 124
    constexpr int kCompValY   = 357;   // ancien : 255, puis 300
    constexpr int kCompValW   = 200;   // ancien : 52
    constexpr int kCompValH   = 22;    // ancien : 15
    constexpr int kCompNameY  = 383;   // ancien : 272, puis 317
    // Le LCD, s'il est reactive : pose VISUELLEMENT a (379,118), 331x184
    // visibles (753 x 419 a l'echelle 0,44 — la transform ne bouge pas
    // l'origine, la position des bounds EST la position visuelle).
    // (pose de l'essai cartes V6, conservee telle quelle pour le retour)
    constexpr int kLcdVisualX = 379;
    constexpr int kLcdVisualY = 118;
    // Les toggles du heros : UNE LIGNE sous le nom du COMP (le disque de
    // 200 px ne laissait plus la place d'une pile). Ancienne pose « cartes V6 » :
    // bas a DROITE du heros (kHeroTogY = 286, x = 718 / 803).
    constexpr int kHeroTogY   = 407;   // ancien : 286
    constexpr int kHeroTog2Y  = 407;   // DC FILTER : meme ligne, a cote
    constexpr int kHeroTogH   = 15;
    constexpr int kLowCutTogX = 208;   // ancien : 718, puis 115, puis 242
    constexpr int kLowCutTogW = 71;
    constexpr int kDcFilterTogX = 287; // ancien : 803, puis 113, puis 321
    constexpr int kDcFilterTogW = 74;
    // Les mini-knobs des niveaux : disque 24 px, centre a (62,501) pour IN et
    // (918,501) pour OUT (entre label et fenetre, cotes DOM).
    constexpr int kInMeterKnobOriginX  = 42;
    constexpr int kOutMeterKnobOriginX = 898;

    // Les bandes empilees a DROITE, l'autre MOITIE (essai 2026-10-02) :
    // x491, y138, 398x300, gap 8, quatre rangees de 69 px. La largeur des
    // sections est une MOITIE de la zone utile, pas la largeur du contenu :
    // celle-ci (330 pour FX, la plus chargee) est plus etroite que la moitie,
// le contenu est donc ALIGN A GAUCHE et le vide se tient a droite (choix
    // utilisateur du 2026-10-02). Ancienne grille 2x2 (« cartes V6 ») :
    // x85, y316, 805x146, kBandW = (kGridW - kBandGap)/2 = 398.
    constexpr int kGridX     = 491;   // ancien : 85, puis 228, puis 560
    constexpr int kGridY     = 138;   // ancien : 316
    constexpr int kGridW     = 398;   // ancien : 805, puis 662, puis 330
    constexpr int kGridH     = 300;   // ancien : 146
    constexpr int kBandGap   = 8;
    constexpr int kBandW     = kGridW;    // une seule colonne : pleine largeur
                                           // ancien : (kGridW - kBandGap)/2
    constexpr int kBandH     = 69;

    // Dans une bande : LED en (13,30), knobs de x35 (origin composant, le
    // disque 44 px est centre dans les 60 px), pas 60, pose a y7 (le nom grave
    // dessous ferme le bloc centre) ; le groupe division du DELAY a x223
    // (vitre x241, largeur 41), pose a y18, avec le SYNC DESSOUS (y37).
    constexpr int kLedX        = 13;
    constexpr int kLedY        = 30;
    constexpr int kLedSz       = 8;
    constexpr int kBandCtrlX0  = 35;
    constexpr int kBandCtrlStep = 60;
    constexpr int kBandKnobTop = 7;
    constexpr int kBandKnobSz  = 44;
    constexpr int kDivX        = 223;
    constexpr int kDivBoxX     = 241;
    constexpr int kDivNextX    = 282;
    constexpr int kDivY        = 18;
    constexpr int kDivH        = 16;
    constexpr int kDivBoxW     = 41;
    constexpr int kSyncX       = 234;
    constexpr int kSyncY       = 37;
    constexpr int kSyncW       = 56;
    constexpr int kSyncH       = 15;
    constexpr int kMiniCapW    = 18;

    // Niveaux (CSS .hmeter) : 450x24, ancrés a y489, a 6 px des bords.
    constexpr int kMeterW    = 450;
    constexpr int kMeterH    = 24;
    constexpr int kMeterY    = 489;
    constexpr int kMeterInset = 6;
    constexpr int kMeterKnobSz = 24;

    juce::Colour colour (uint32_t argb) noexcept
    {
        return juce::Colour (argb);
    }

    // Formatage de la valeur affichee (ecran de valeur du heros, infobulles),
    // selon l'unite du catalogue.
    juce::String valueText (const odvox::params::Info& info, float actual)
    {
        switch (info.unit)
        {
            case odvox::params::Unit::db:
                return juce::String (actual, 1) + " dB";
            case odvox::params::Unit::percent:
                return juce::String ((int) std::lround (actual)) + " %";
            case odvox::params::Unit::ms:
                return juce::String ((int) std::lround (actual)) + " ms";
            case odvox::params::Unit::hz:
                return juce::String ((int) std::lround (actual)) + " Hz";
            case odvox::params::Unit::q:
                return juce::String (actual, 2) + " Q";
            case odvox::params::Unit::choice:
            case odvox::params::Unit::boolean:
            case odvox::params::Unit::action:
            case odvox::params::Unit::cents:
                break;
        }

        return "--";
    }

    /** Les quatre BANDES de la grille (maquette validee), dans l'ordre : la
        rangee du haut TON puis FX, celle du bas REVERB puis DELAY. `ledId`
        designe la LED-interrupteur (eq_on, fx_on, reverb_on, delay_on — meme
        ordre). Les ids sont des parametres Essential du catalogue : tous les
        29 doivent s'y retrouver exactement une fois (les enables de groupe
        sont portes par les LED, pas par les bandes). */
    struct BandDef
    {
        int ledId;
        std::vector<const char*> ids;
    };

    std::vector<BandDef> bandDefs()
    {
        return {
            { 0, { "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db" } },
            { 1, { "gate_amount", "deess_amount", "drive_amount",
                   "doubler_amount", "width_amount" } },
            { 2, { "reverb_short_pct", "reverb_small_pct",
                   "reverb_big_pct", "reverb_lush_pct" } },
            { 3, { "delay_amount", "delay_time_ms", "delay_ducking" } },
        };
    }

    /** Rectangle d'une bande, en coordonnees editeur : les quatre bandes
        EMPILEES en une seule colonne a droite (essai 2026-10-02).

        Ancienne grille 2x2 (« cartes V6 ») :
            return { kGridX + (index % 2) * (kBandW + kBandGap),
                     kGridY + (index / 2) * (kBandH + kBandGap),
                     kBandW, kBandH };
    */
    juce::Rectangle<int> bandRect (int index)
    {
        return { kGridX, kGridY + index * (kBandH + kBandGap), kBandW, kBandH };
    }

    /** Rectangle de l'etage hero. */
    juce::Rectangle<int> heroRect()
    {
        return { kHeroX, kHeroY, kHeroW, kHeroH };
    }

    /** Un trou d'alcove sombre (fond commun heros et bandes, CSS .hero/.band) :
        degrade du fond de pedale vers le bas, ombre en HAUT (la lumiere ne
        vient pas du trou), liseron clair en bas, contour de definition. */
    void paintAlcove (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const float radius = 9.0f;

        // L'ombre portee : l'alcove est CREUSEE, son rebord projette une ombre
        // vers l'exterieur bas (le contraire des caps, qui sortent).
        g.setColour (colour (0x59000000));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), radius);

        juce::ColourGradient body (colour (0xff0b0a09), 0.0f, bounds.getY(),
                                   colour (0xff181512), 0.0f, bounds.getBottom(), false);
        {
            juce::Path clip;
            clip.addRoundedRectangle (bounds, radius);
            g.saveState();
            g.reduceClipRegion (clip);
            g.setGradientFill (body);
            g.fillRect (bounds);
            g.restoreState();
        }

        // Le vocabulaire du trou : ombre DURE en haut, lumiere en bas.
        g.setColour (colour (0x85000000));
        g.drawRoundedRectangle (bounds.translated (0.0f, -0.5f).reduced (0.5f), radius, 1.0f);
        g.setColour (colour (0x12ffffff));
        g.drawRoundedRectangle (bounds.translated (0.0f, 0.5f).reduced (0.5f), radius, 1.0f);
        g.setColour (colour (0xff060505));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    /** Le corps metal des interrupteurs du heros (CSS .sw) : coulisse sombre
        enfoncee, pastille metal a gauche (off) ou violette a droite (on). */
    void paintSwitch (juce::Graphics& g, const juce::Rectangle<float>& track, bool on)
    {
        g.setColour (colour (0xff0d0c0b));
        g.fillRoundedRectangle (track, track.getHeight() * 0.5f);

        g.setColour (colour (0x85000000));
        g.drawRoundedRectangle (track.translated (0.0f, -0.5f).reduced (0.5f),
                                track.getHeight() * 0.5f, 1.0f);
        g.setColour (colour (0xff060505));
        g.drawRoundedRectangle (track.reduced (0.5f), track.getHeight() * 0.5f, 1.0f);

        const float knobSide = track.getHeight() - 2.0f;
        const auto knob = on
            ? juce::Rectangle<float> (track.getRight() - knobSide - 1.0f, track.getY() + 1.0f,
                                      knobSide, knobSide)
            : juce::Rectangle<float> (track.getX() + 1.0f, track.getY() + 1.0f,
                                      knobSide, knobSide);

        if (on)
        {
            juce::ColourGradient glow (colour (Palette::ledHi),
                                       knob.getCentreX() - knobSide * 0.35f,
                                       knob.getCentreY() - knobSide * 0.35f,
                                       colour (Palette::ledLow),
                                       knob.getCentreX() + knobSide * 0.35f,
                                       knob.getCentreY() + knobSide * 0.35f, true);
            g.setGradientFill (glow);
            g.fillEllipse (knob);
        }
        else
        {
            g.setColour (colour (0xff9b978f));
            g.fillEllipse (knob);
        }

        g.setColour (colour (0x66000000));
        g.drawEllipse (knob, 1.0f);
    }

    /** Libelle des interrupteurs (CSS .swlbl) : la coulisse a GAUCHE, le texte
        grave a droite — la position EST l'etat. */
    void paintSwitchLabel (juce::Graphics& g, const juce::Rectangle<float>& bounds,
                           const juce::String& text, bool on)
    {
        const float trackH = juce::jmin (13.0f, bounds.getHeight() - 2.0f);
        const float trackW = 24.0f;
        const auto track = juce::Rectangle<float> (bounds.getX(),
                                                   bounds.getCentreY() - trackH * 0.5f,
                                                   trackW, trackH);

        paintSwitch (g, track, on);

        const auto labelArea = juce::Rectangle<float> (track.getRight() + 5.0f, bounds.getY(),
                                                       bounds.getRight() - track.getRight()
                                                           - 5.0f,
                                                       bounds.getHeight());
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.setColour (colour (0xb3000000));
        g.drawText (text, labelArea.translated (0.0f, 1.0f),
                    juce::Justification::centredLeft, true);
        g.setColour (colour (on ? 0xffc9c3b8 : 0xffa39d92));
        g.drawText (text, labelArea, juce::Justification::centredLeft, true);
    }
}

// --- Construction -----------------------------------------------------------

ODVoxAudioProcessorEditor::ODVoxAudioProcessorEditor (ODVoxAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      inMeter ("IN"),
      outMeter ("OUT"),
      lcd (p),
      divPrevCap(),
      divNextCap(),
      lowCutToggle (p, "lowcut_amount"),
      dcFilterToggle (p, "output_dc_filter"),
      delaySyncToggle (p, "delay_sync")
{
    // L'identite hardware d'abord : tous les composants crees apres ce point
    // la heritent (boutons, menus, infobulles).
    setLookAndFeel (&look);

    // --- Top bar V2 (maquette transposee) -----------------------------------
    // Logo : sérigraphie plate violette (mat — PAS de glow, retour user).
    productName.setText ("OD VOX", juce::dontSendNotification);
    productName.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    productName.setColour (juce::Label::textColourId, colour (Palette::inkAccent));
    productName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (productName);

    // AC2 de US-12 : la latence affichee est en echantillons ET en ms.
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setColour (juce::Label::textColourId, colour (0xffa89f92));
    latencyLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (latencyLabel);

    // Transport : chevron rond violet, vitre, chevron rond violet. Le geste
    // preset se reconnait d'un coup d'oeil sur toute l'interface (maquette).
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    previousPresetButton.style = Cap::Style::round;
    previousPresetButton.chevron = -1;
    previousPresetButton.onClick = [this] { stepPreset (-1); };
    previousPresetButton.setTooltip ("Preset precedent");
    addAndMakeVisible (previousPresetButton);

    nextPresetButton.style = Cap::Style::round;
    nextPresetButton.chevron = 1;
    nextPresetButton.onClick = [this] { stepPreset (1); };
    nextPresetButton.setTooltip ("Preset suivant");
    addAndMakeVisible (nextPresetButton);

    // A / B : le liseré violet designe l'emplacement actif (position = etat).
    slotAButton.style = Cap::Style::round;
    slotAButton.label = "A";
    slotAButton.onClick = [this] { toggleAorB(); };
    slotAButton.setTooltip ("Basculer entre A et B");
    addAndMakeVisible (slotAButton);

    slotBButton.style = Cap::Style::round;
    slotBButton.label = "B";
    slotBButton.onClick = [this] { toggleAorB(); };
    slotBButton.setTooltip ("Basculer entre A et B");
    addAndMakeVisible (slotBButton);

    copyButton.style = Cap::Style::dark;
    copyButton.label = "A>B";
    copyButton.onClick = [this]
    {
        // La copie part de l'ETAT COURANT (pas du dernier instantane).
        processor.captureSlot (getActiveSlot());
        processor.copySlot (getActiveSlot(), 1 - getActiveSlot());
    };
    copyButton.setTooltip ("Copier l'emplacement actif vers l'autre");
    addAndMakeVisible (copyButton);

    // CAL : lampe temoin encastree — elle s'allume pendant la mesure.
    calibrateButton.style = Cap::Style::dark;
    calibrateButton.label = "CAL";
    calibrateButton.hasLamp = true;
    calibrateButton.onClick = [this] { processor.startCalibration(); };
    addAndMakeVisible (calibrateButton);

    calibrationStatus.setJustificationType (juce::Justification::centredLeft);
    calibrationStatus.setFont (juce::FontOptions (11.0f));
    calibrationStatus.setColour (juce::Label::textColourId, colour (0xffa89f92));
    addAndMakeVisible (calibrationStatus);

    // --- Le heros : le knob COMP, les quatre correcteurs EQ, le LCD ---------
    // Les knobs naissent par le meme chemin : Rotary attache au parametre,
    // range dans `knobs`. La couverture Essential (plus bas) passe par cette
    // liste : un id absent de `knobs`/toggles/LED est un bug de specification.
    const auto addKnob = [this] (const char* id)
    {
        auto knob = std::make_unique<Rotary> (processor, id);
        knobs.push_back ({ std::move (knob), id });
        return knobs.back().knob.get();
    };

    compKnob = addKnob ("comp_amount");
    compKnob->setTooltip ("Compression (double-clic : defaut)");
    // Le nom et la valeur du heros sont dessines par l'editeur (ecran de
    // valeur sous le disque, comme la maquette .slot).
    compKnob->showLabel = false;

    // Les quatre correcteurs d'EQ : les quatre knobs de la bande TON. La LED
    // de la bande (eq_on) est l'interrupteur — le vieux toggle EQ ON a quitte
    // l'interface (l'automatisation du parametre reste possible).
    for (const char* id : { "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db" })
        (void) addKnob (id);

    for (const char* id : { "gate_amount", "deess_amount", "drive_amount",
                            "doubler_amount", "width_amount",
                            "reverb_short_pct", "reverb_small_pct",
                            "reverb_big_pct", "reverb_lush_pct",
                            "delay_amount", "delay_time_ms", "delay_ducking" })
        (void) addKnob (id);

    // Les mini-knobs des niveaux : input_gain (a gauche de la fenetre IN) et
    // output_gain (a droite de la fenetre OUT) — de vrais rotaries.
    inGainKnob  = addKnob ("input_gain_db");
    inGainKnob->showLabel = false;
    inGainKnob->setTooltip ("Input Gain (double-clic : 0 dB)");

    outGainKnob = addKnob ("output_gain_db");
    outGainKnob->showLabel = false;
    outGainKnob->setTooltip ("Output Gain (double-clic : 0 dB)");

    for (const auto& knob : knobs)
        addAndMakeVisible (*knob.knob);

    // Le LCD : la courbe d'EQ (F1.6c) dans le heros. Le bezel est dessine PAR
    // la vue (EqCurveView). La transform (0,44) est posee dans resized().
    addAndMakeVisible (lcd);

    // --- Les toggles du heros : LOW CUT et DC FILTER -------------------------
    addAndMakeVisible (lowCutToggle);
    addAndMakeVisible (dcFilterToggle);

    // --- La division du DELAY : vitre + deux mini-caps, et le SYNC ----------
    for (const auto& candidate : odvox::params::declared())
        if (candidate.id == juce::String ("delay_time"))
        {
            delayTimeCombo.info = candidate;
            break;
        }

    jassert (delayTimeCombo.info.has_value());

    delayTimeCombo.box.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (delayTimeCombo.box);
    delayTimeCombo.param = processor.state().getParameter ("delay_time");

    // Les libelles viennent du catalogue : le ctor par defaut de Combo ne les
    // a pas ajoutes (il ne connait pas encore l'info). SANS eux la vitre
    // reste vide et affiche « no choice » — JUCE ComboBox sans item ne rend
    // aucun texte (bug retour utilisateur 2026-09-30). On remplit AVANT de
    // creer l'attachment, qui synchronise la selection sur le parametre.
    if (delayTimeCombo.info.has_value())
    {
        juce::StringArray parts;
        parts.addTokens (juce::String (delayTimeCombo.info->choices), "|", "");
        parts.removeEmptyStrings (true);

        for (int i = 0; i < parts.size(); ++i)
            delayTimeCombo.box.addItem (parts[i], i + 1);

        if (delayTimeCombo.param != nullptr && ! parts.isEmpty())
            delayTimeCombo.box.setSelectedItemIndex (
                juce::jlimit (0, parts.size() - 1,
                              (int) std::lround (delayTimeCombo.param->getValue()
                                                 * (float) (parts.size() - 1))),
                juce::dontSendNotification);
    }

    delayTimeCombo.attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.state(), "delay_time", delayTimeCombo.box);

    // Le trio colle : les mini-caps TOUCHENT la vitre (coins exterieurs seuls
    // arrondis, cotes mockup).
    divPrevCap.style = Cap::Style::mini;
    divPrevCap.chevron = -1;
    divPrevCap.onClick = [this] { stepDivision (-1); };
    divPrevCap.setTooltip ("Division precedente");
    addAndMakeVisible (divPrevCap);

    divNextCap.style = Cap::Style::mini;
    divNextCap.chevron = 1;
    divNextCap.onClick = [this] { stepDivision (+1); };
    divNextCap.setTooltip ("Division suivante");
    addAndMakeVisible (divNextCap);

    // La vitre ouvre le menu deroulant natif (la ComboBox est l'organe, la
    // vitre son habillage).

    addAndMakeVisible (delaySyncToggle);
    delaySyncToggle.labelText = "SYNC";
    delaySyncToggle.button.setTooltip ("Synchroniser au tempo (sinon temps libre)");

    // --- Les quatre LED-interrupteurs de bande -------------------------------
    // TON (eq_on), FX (fx_on), REVERB (reverb_on), DELAY (delay_on) : l'etat
    // vit dans un parametre declare (amendement catalogue du 2026-09-29) — il
    // survit a l'A/B, aux presets et a la session.
    for (const char* id : { "eq_on", "fx_on", "reverb_on", "delay_on" })
    {
        auto led = std::make_unique<BandLed> (processor, id);
        addAndMakeVisible (*led);
        bandLeds.push_back (std::move (led));
    }

    // --- Les niveaux : deux instruments independants aux coins --------------
    // La fenetre des metres vit entre label et mini-knob ; les mini-knobs sont
    // de vrais rotaries (drag vertical, double-clic = defaut).
    inMeter.levelDb  = [this] { return processor.inputLevelDb(); };
    outMeter.levelDb = [this] { return processor.outputLevelDb(); };
    outMeter.mirrored = true;
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // L'assombrissement du contenu des bandes : branche a chaque LED (le
    // clic et l'arrivee d'un preset/A/B passent par le meme chemin).
    for (auto& led : bandLeds)
        led->onChanged = [this] (bool) { applyBandDim(); };

    // --- Couverture Essential : le contrat du catalogue ----------------------
    // Chaque parametre Essential doit etre porte par un controle de
    // l'interface, exactement une fois. `input_calibrate` est une ACTION : elle
    // vit dans le cap CAL de la top bar. Un parametre absent d'ici est un bug
    // de specification — l'assertion doit echouer en Debug.
    {
        std::map<juce::String, int> seen;

        for (const auto& candidate : odvox::params::declared())
            if (candidate.essential
                && candidate.id != juce::String ("input_calibrate")
                && candidate.id != juce::String ("hq_mode"))
                seen[candidate.id] = 0;

        for (const auto& knob : knobs)
            if (seen.find (knob.id) != seen.end())
                seen[knob.id] += 1;

        for (const auto* id : { "lowcut_amount", "output_dc_filter", "delay_sync" })
            if (seen.find (id) != seen.end())
                seen[id] += 1;

        for (const auto& led : bandLeds)
            if (led->info.has_value() && seen.find (led->info->id) != seen.end())
                seen[led->info->id] += 1;

        for (const auto& [id, count] : seen)
        {
            jassert (count == 1);   // absent ou double : bug de specification
            juce::ignoreUnused (id);
        }
    }

    // Les controles de chaque bande, pour l'assombrissement initial
    // (applyBandDim lit l'etat des LED a l'ouverture).
    applyBandDim();

    // Le timer repainne aussi la vitre de division : la ComboBox native affiche
    // deja l'item courant, refreshDivisionBox ne tient que l'infobulle a jour.
    startTimerHz (30);

    refreshPresetLabel();
    refreshCalibrateButton();
    refreshABButtons();
    refreshLatencyLabel();
    refreshDivisionBox();
    setSize (kDefaultWidth, kDefaultHeight);

    // Accessibilité DESACTIVEE dans le plugin : l'API UIA de Windows hache les
    // arbres de composants, et l'hote peut les enumerer pendant la fermeture
    // de la fenetre — un crash/hang FL a la RETRAITE du plugin (module fautif
    // journalise ce jour : UIAutomationCore.DLL) presente exactement cette
    // signature. Couru ICI, APRES creation de tous les composants : les
    // handlers UIA des nouveaux enfants ne naissent jamais.
    setAccessible (false);
    const auto disableTree = [&] (auto&& self, juce::Component* parent) -> void
    {
        for (auto* child : parent->getChildren())
        {
            child->setAccessible (false);
            self (self, child);
        }
    };
    disableTree (disableTree, this);
}

juce::StringArray ODVoxAudioProcessorEditor::uncoveredEssentialIds() const
{
    // Le miroir lisible de la couverture : ce que le test d'interface exige
    // vide. (Le jassert du constructeur logge sans bloquer sans debogueur ;
    // ce test-la doit VRAIMENT echouer.)
    std::map<juce::String, int> seen;

    for (const auto& candidate : odvox::params::declared())
        if (candidate.essential
            && candidate.id != juce::String ("input_calibrate")
            && candidate.id != juce::String ("hq_mode"))
            seen[candidate.id] = 0;

    for (const auto& knob : knobs)
        if (seen.find (knob.id) != seen.end())
            seen[knob.id] += 1;

    for (const auto* id : { "lowcut_amount", "output_dc_filter", "delay_sync",
                            "delay_time" })   // delay_time : la vitre de division
        if (seen.find (id) != seen.end())
            seen[id] += 1;

    for (const auto& led : bandLeds)
        if (led->info.has_value() && seen.find (led->info->id) != seen.end())
            seen[led->info->id] += 1;

    juce::StringArray uncovered;

    for (const auto& [id, count] : seen)
        if (count != 1)
            uncovered.add (id);

    return uncovered;
}

const ODVoxAudioProcessorEditor::BandLed* ODVoxAudioProcessorEditor::bandLed (int index) const
{
    return juce::isPositiveAndBelow (index, (int) bandLeds.size())
               ? bandLeds[(size_t) index].get() : nullptr;
}

const ODVoxAudioProcessorEditor::Toggle* ODVoxAudioProcessorEditor::toggleById (const char* parameterId) const
{
    for (auto* candidate : { &lowCutToggle, &dcFilterToggle, &delaySyncToggle })
        if (candidate->parameterId == juce::String (parameterId))
            return candidate;

    return nullptr;
}

void ODVoxAudioProcessorEditor::stepDivision (int direction)
{
    if (auto* param = delayTimeCombo.param)
    {
        // La valeur normalisee du parametre choice EST l'index/20 (JUCE
        // AudioParameterChoice : NormalisableRange 0..20, conversions lineaires
        // v*end et v/end) — le processeur fait lround(valeur brute) = index.
        const int last = odvox::Delay::kNumDivisions - 1;
        const int index = juce::jlimit (0, last,
                                        (int) std::lround (param->getValue() * (float) last)
                                            + direction);
        param->beginChangeGesture();
        param->setValueNotifyingHost ((float) index / (float) last);
        param->endChangeGesture();
    }
}

void ODVoxAudioProcessorEditor::clickedDivPrev() { stepDivision (-1); }
void ODVoxAudioProcessorEditor::clickedDivNext() { stepDivision (+1); }

int ODVoxAudioProcessorEditor::divisionItemCount() const
{
    return delayTimeCombo.box.getNumItems();
}

juce::String ODVoxAudioProcessorEditor::divisionLabelText() const
{
    return delayTimeCombo.box.getText();
}

float ODVoxAudioProcessorEditor::bandDimAlpha (int index) const
{
    return juce::isPositiveAndBelow (index, (int) std::size (bandControls))
               && ! bandControls[(size_t) index].empty()
               ? bandControls[(size_t) index].front()->getAlpha() : 1.0f;
}

const ODVoxAudioProcessorEditor::LevelMeter* ODVoxAudioProcessorEditor::levelMeter (int index) const
{
    switch (index)
    {
        case 0:  return &inMeter;
        case 1:  return &outMeter;
        default: return nullptr;
    }
}

ODVoxAudioProcessorEditor::~ODVoxAudioProcessorEditor()
{
    setLookAndFeel (nullptr);   // avant que le membre `look` ne disparaisse
    stopTimer();
}

void ODVoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    // La plaque Figma EST le fond (photo opaque, lumiere de studio incluse).
    if (skin::hasBackground())
    {
        g.drawImage (skin::background(), getLocalBounds().toFloat());
    }
    else
    {
        // Repli : un fond sombre neutre (les alcoves et le metal clair des
        // knobs restent lisibles dessus).
        g.fillAll (colour (0xff101012));
    }

    // Les alcoves : le heros, puis les quatre bandes — les enfants (knobs,
    // LED, vitres) sont dessines PAR-DESSUS par JUCE.
    paintAlcove (g, heroRect().toFloat());

    for (int i = 0; i < 4; ++i)
        paintAlcove (g, bandRect (i).toFloat());

    // L'ecran de valeur du COMP (le seul du produit, avec la division) :
    // fenetre encaissee, valeur violette Consolas, nom grave dessous — le
    // .slot de la maquette.
    if (compKnob != nullptr && ! compValueBounds.isEmpty())
    {
        const auto bounds = compValueBounds.toFloat();
        g.setColour (colour (0xff0b0a09));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (colour (0x85000000));
        g.drawRoundedRectangle (bounds.translated (0.0f, -0.5f).reduced (0.5f), 4.0f, 1.0f);
        g.setColour (colour (0xff060505));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        // La valeur formatee : calcul direct depuis le parametre du knob
        // (Rotary n'affiche plus rien lui-meme, l'ecran du heros est le seul
        // endroit ou la valeur du COMP est visible).
        const auto text = valueText (*compKnob->info,
                                     odvox::params::normalisedToActual (
                                         compKnob->info->id, compKnob->normalised()));

        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (colour (0xb3000000));
        g.drawText (text, bounds.translated (0.0f, 1.0f),
                    juce::Justification::centred, true);
        g.setColour (colour (Palette::ledHi));
        g.drawText (text, bounds, juce::Justification::centred, true);

        const auto nameArea = juce::Rectangle<float> (bounds.getX(), (float) kCompNameY,
                                                      bounds.getWidth(), 12.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (colour (0xb3000000));
        g.drawText (compKnob->name(), nameArea.translated (0.0f, 1.0f),
                    juce::Justification::centred, true);
        g.setColour (colour (0xffc9c3b8));
        g.drawText (compKnob->name(), nameArea, juce::Justification::centred, true);
    }
}

void ODVoxAudioProcessorEditor::resized()
{
    // --- Top bar V2, posee DANS la pedale (cotes du mockup) ------------------
    {
        const juce::Rectangle<int> bar (kBarX, kBarY, 980 - 2 * kBarX - 20, kBarH);
        layoutTopBar (bar);
    }

    // --- L'etage hero : COMP + LCD + toggles ---------------------------------
    layoutHero (heroRect());

    // --- Les quatre bandes de la grille 2x2 ----------------------------------
    // Ordre des defs = ordre des LED = ordre des controles construits.
    const auto defs = bandDefs();
    jassert (defs.size() == bandLeds.size());

    for (int i = 0; i < (int) defs.size(); ++i)
    {
        const auto rect = bandRect (i);
        bandLeds[(size_t) i]->setBounds (rect.getX() + kLedX, rect.getY() + kLedY,
                                         kLedSz, kLedSz);

        const bool isDelay = defs[(size_t) i].ledId == 3;

        // Le contenu est ALIGN A GAUCHE dans la section (retour utilisateur du
        // 2026-10-02 : « aligne quand meme a gauche les knobs »). Les quatre
        // bandes n'ont pas le meme contenu (4 / 5 / 4 knobs, plus le trio de
        // division du DELAY) : une section de 398 px laisse donc 70 a 150 px de
        // vide a DROITE, et c'est voulu — cale a gauche, la LED et le premier
        // knob partagent le meme x sur les quatre rangees, l'oeil lit une seule
        // colonne. (Une version centree du contenu existe dans l'historique de
        // cette fonction : elle evaluait l'etendue reelle par rangee.)
        int x = rect.getX() + kBandCtrlX0;

    for (const auto* id : defs[(size_t) i].ids)
    {
        if (auto* knob = knobById (id))
        {
            knob->setBounds (x, rect.getY() + kBandKnobTop,
                             kBandKnobSz + 16, kBandKnobSz + 14);
            x += kBandCtrlStep;
        }
    }

        // Les controles de la bande, pour l'assombrissement (LED OFF).
        bandControls[(size_t) i].clear();

        for (const auto* id : defs[(size_t) i].ids)
            if (auto* knob = knobById (id))
                bandControls[(size_t) i].push_back (knob);

        if (isDelay)
        {
            bandControls[(size_t) i].push_back (&delayTimeCombo.box);
            bandControls[(size_t) i].push_back (&divPrevCap);
            bandControls[(size_t) i].push_back (&divNextCap);
            bandControls[(size_t) i].push_back (&delaySyncToggle);

            // Le groupe division, colle a la suite des knobs : mini-cap,
            // vitre (bordure violette), mini-cap, avec le SYNC DESSOUS —
            // les cotes du mockup (divwrap : les caps TOUCHENT la vitre).
            divPrevCap.setBounds (rect.getX() + kDivX, rect.getY() + kDivY,
                                  kMiniCapW, kDivH);
            delayTimeCombo.box.setBounds (rect.getX() + kDivBoxX, rect.getY() + kDivY,
                                          kDivBoxW, kDivH);
            divNextCap.setBounds (rect.getX() + kDivNextX, rect.getY() + kDivY,
                                  kMiniCapW, kDivH);
            delaySyncToggle.setBounds (rect.getX() + kSyncX, rect.getY() + kSyncY,
                                       kSyncW, kSyncH);
        }
    }

    // --- Les niveaux : deux instruments independants aux coins --------------
    // La fenetre vit entre label et mini-knob (cotes DOM) ; les mini-knobs de
    // gain sont de vrais rotaries, poses par-dessus l'instrument.
    inMeter.setBounds (kMeterInset, kMeterY, kMeterW, kMeterH);
    outMeter.setBounds (980 - kMeterInset - kMeterW, kMeterY, kMeterW, kMeterH);

    // Les mini-knobs des gains (disque 24 px, cotes DOM : centres a (62,501)
    // et (918,501) — origin composant = centre - 13). Le disque se dessine en
    // haut du composant (2 px de marge) : origin y = centre - 12 = kMeterY - 1.
    if (inGainKnob != nullptr)
        inGainKnob->setBounds (kInMeterKnobOriginX, kMeterY - 1, kMeterKnobSz + 16, kMeterKnobSz + 2);

    if (outGainKnob != nullptr)
        outGainKnob->setBounds (kOutMeterKnobOriginX, kMeterY - 1, kMeterKnobSz + 16, kMeterKnobSz + 2);

    // L'assombrissement des bandes suit l'etat courant des LED (les
    // bandControls viennent d'etre remplis).
    applyBandDim();
}

void ODVoxAudioProcessorEditor::timerCallback()
{
    // L'etat courant vit dans le slot actif : la capture suit les reglages.
    // A 30 Hz un std::map de ~57 cles reste negligeable.
    processor.captureSlot (getActiveSlot());

    refreshCalibrateButton();
    refreshABButtons();
    refreshLatencyLabel();
    refreshDivisionBox();   // l'infobulle de la division suit la valeur

    // Les vumetres IN/OUT suivent les taps RMS du thread audio ; les knobs et
    // LED se repainnent au meme rythme (halos, ecran de valeur).
    inMeter.repaint();
    outMeter.repaint();

    if (compKnob != nullptr && ! compValueBounds.isEmpty())
    {
        repaint (compValueBounds);
        repaint ({ kCompValX, kCompNameY, kCompValW, 14 });   // le nom grave dessous
    }

    for (const auto& knob : knobs)
        knob.knob->repaint();
}

void ODVoxAudioProcessorEditor::refreshLatencyLabel()
{
    // AC2 de US-12 : echantillons ET millisecondes, dans le meme libelle.
    const int latency = processor.getLatencySamples();

    if (const double sr = processor.getSampleRate(); sr > 0.0)
        latencyLabel.setText (juce::String (latency) + " smp / "
                                  + juce::String (latency * 1000.0 / sr, 1) + " ms",
                              juce::dontSendNotification);
    else
        latencyLabel.setText (juce::String (latency) + " smp",
                              juce::dontSendNotification);
}

// --- Rotary -----------------------------------------------------------------

ODVoxAudioProcessorEditor::Rotary::Rotary (ODVoxAudioProcessor& p, const char* parameterId,
                                           bool mini)
{
    const auto all = odvox::params::declared();

    for (const auto& candidate : all)
        if (parameterId == juce::String (candidate.id))
        {
            info = candidate;
            break;
        }

    jassert (info.has_value()); // tout controle de l'interface doit etre declare

    auto* parameter = p.state().getParameter (parameterId);
    param = parameter;

    if (param != nullptr)
        param->addListener (this);

    setRepaintsOnMouseActivity (true);
    juce::ignoreUnused (mini);
}

ODVoxAudioProcessorEditor::Rotary::~Rotary()
{
    if (param != nullptr)
        param->removeListener (this);
}

float ODVoxAudioProcessorEditor::Rotary::normalised() const noexcept
{
    return param != nullptr ? param->getValue() : 0.0f;
}

juce::String ODVoxAudioProcessorEditor::Rotary::name() const
{
    return info.has_value() ? juce::String (info->name) : juce::String();
}

void ODVoxAudioProcessorEditor::Rotary::paint (juce::Graphics& g)
{
    const auto bounds  = getLocalBounds().toFloat();
    const float value  = normalised();

    // Regle de geometrie (cotes maquette) : le DISQUE est centre horizontalement,
    // pose en haut du composant (8 px de marge de chaque cote pour l'ombre, 2 px
    // en dessous) ; la bande de nom (14 px) ferme le composant quand showLabel.
    const float diameter = juce::jmin (bounds.getWidth() - 16.0f,
                                       bounds.getHeight()
                                           - (showLabel ? 14.0f : 2.0f));
    const auto centre = juce::Point<float> (bounds.getCentreX(),
                                            bounds.getY() + diameter * 0.5f + 1.0f);
    const float radius = diameter * 0.5f;

    // --- Le corps : l'atlas de frames (Skin.h) — piece MXR ECB071 -----------
    // La frame de l'atlas porte la piece (86 % de la case, cf. generateur)
    // et son ombre portee ; drawKnob dessine la frame a 93 % de la zone
    // recue. La case est donc majoree pour que le DISQUE garde exactement
    // la geometrie maquette calculee ci-dessus.
    const float frameSide = diameter / (0.86f * 0.93f);
    skin::drawKnob (g,
                    juce::Rectangle<float> (frameSide, frameSide).withCentre (centre),
                    value);

    // Le nom grave SOUS le disque.
    if (showLabel)
    {
        const auto labelArea = juce::Rectangle<float> (bounds.getX(),
                                                       centre.y + radius + 2.0f,
                                                       bounds.getWidth(), 12.0f);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (colour (0xb3000000));
        g.drawText (name(), labelArea.translated (0.0f, 1.0f),
                    juce::Justification::centred, true);
        g.setColour (colour (0xffc9c3b8));
        g.drawText (name(), labelArea, juce::Justification::centred, true);
    }
}

void ODVoxAudioProcessorEditor::Rotary::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! info.has_value() || param == nullptr)
        return;

    // Double-clic = retour a la valeur par defaut du catalogue (US-02 AC2).
    if (e.getNumberOfClicks() == 2)
    {
        param->setValueNotifyingHost (
            odvox::params::actualToNormalised (info->id, info->defaultValue));
        return;
    }

    dragStartValue = normalised();
    dragStartPixel = e.position.y;
}

void ODVoxAudioProcessorEditor::Rotary::mouseDrag (const juce::MouseEvent& e)
{
    if (! info.has_value() || param == nullptr || ! e.mods.isLeftButtonDown())
        return;

    // Maj = reglage fin. Vertical, comme sur la plupart des rotaries JUCE.
    const float sensitivity = e.mods.isShiftDown() ? 0.0015f : 0.005f;
    const float delta       = (float) (dragStartPixel - e.position.y) * sensitivity;
    param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartValue + delta));
}

void ODVoxAudioProcessorEditor::Rotary::mouseWheelMove (const juce::MouseEvent&,
                                                        const juce::MouseWheelDetails& wheel)
{
    if (! info.has_value() || param == nullptr)
        return;

    const float step   = 0.02f;
    const float deltaY = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised() + deltaY * step));
}

void ODVoxAudioProcessorEditor::Rotary::parameterValueChanged (int, float)
{
    repaint();
}

void ODVoxAudioProcessorEditor::Rotary::parameterGestureChanged (int, bool) {}

// --- Combo ------------------------------------------------------------------

ODVoxAudioProcessorEditor::Combo::Combo (ODVoxAudioProcessor& p, const char* parameterId)
{
    const auto all = odvox::params::declared();

    for (const auto& candidate : all)
        if (parameterId == juce::String (candidate.id))
        {
            info = candidate;
            break;
        }

    jassert (info.has_value());

    if (info.has_value())
    {
        // Les libelles viennent du catalogue : une seule source de verite.
        juce::StringArray parts;
        parts.addTokens (juce::String (info->choices), "|", "");
        parts.removeEmptyStrings (true);

        for (int i = 0; i < parts.size(); ++i)
            box.addItem (parts[i], i + 1);
    }

    // La vitre de division (CSS .divbox) : fond noir encaisse, bordure
    // VIOLETTE rgba(168,110,245,.65) — la signature du reglage de division.
    box.setColour (juce::ComboBox::backgroundColourId, colour (0xff0b0a09));
    box.setColour (juce::ComboBox::outlineColourId,
                   colour (0xffa86ef5).withAlpha (0.65f));
    box.setColour (juce::ComboBox::textColourId, colour (Palette::ledHi));
    box.setColour (juce::ComboBox::arrowColourId, colour (0xff6d675e));
    box.setColour (juce::ComboBox::buttonColourId, colour (0xff0b0a09));
    box.setJustificationType (juce::Justification::centred);

    param = p.state().getParameter (parameterId);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.state(), parameterId, box);
}

// --- Toggle (interrupteur du heros / SYNC, dessin maison) -------------------

ODVoxAudioProcessorEditor::Toggle::Toggle (ODVoxAudioProcessor& p, const char* parameterIdIn)
    : parameterId (parameterIdIn)
{
    const auto all = odvox::params::declared();

    for (const auto& candidate : all)
        if (parameterId == juce::String (candidate.id))
        {
            info = candidate;
            break;
        }

    jassert (info.has_value());

    button.setClickingTogglesState (true);

    param = p.state().getParameter (parameterId);

    if (param != nullptr)
        param->addListener (this);   // la source de verite du dessin (voir plus bas)

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.state(), parameterId, button);
}

ODVoxAudioProcessorEditor::Toggle::~Toggle()
{
    if (param != nullptr)
        param->removeListener (this);
}

void ODVoxAudioProcessorEditor::Toggle::parameterValueChanged (int, float newValue)
{
    // LE VISUEL SUIT LE PARAMETRE, PAS LE CLIC. Sans ce listener, le dessin
    // restait fige : le parametre changeait (l'attachment suit), mais RIEN ne
    // declenchait de repaint — ni au clic (pas de setRepaintsOnMouseActivity,
    // contrairement a BandLed), ni depuis l'hote (preset, A/B, automation).
    // Le clic ne dessine jamais lui-meme : c'est le parametre, source unique,
    // qui commande le dessin (meme loi que BandLed::parameterValueChanged).
    juce::ignoreUnused (newValue);

    // Le callback peut venir du thread audio (automatisation) : marshal au fil
    // de messages comme ParameterAttachment (juce_ParameterAttachments.cpp),
    // jamais de repaint depuis le thread audio.
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}

void ODVoxAudioProcessorEditor::Toggle::handleAsyncUpdate()
{
    // Le parametre a change : le dessin se relit le parametre. La valeur lue
    // est la VALEUR DU PARAMETRE, pas un etat interne : pas de divergence
    // possible entre l'entendu (le parametre) et le vu (le dessin).
    repaint();
}

void ODVoxAudioProcessorEditor::Toggle::paint (juce::Graphics& g)
{
    paintSwitchLabel (g, getLocalBounds().toFloat(),
                      labelText.isNotEmpty() ? labelText
                                             : (info.has_value() ? juce::String (info->name)
                                                                 : juce::String()),
                      button.getToggleState());
}

void ODVoxAudioProcessorEditor::Toggle::parameterGestureChanged (int, bool) {}

void ODVoxAudioProcessorEditor::Toggle::mouseDown (const juce::MouseEvent&)
{
    // Le composant dessine son propre interrupteur : le clic est transmis au
    // bouton ATTACHE (l'attachment suit, le parametre et l'hote sont tenus).
    button.triggerClick();
}

// --- LED de bande (l'interrupteur du groupe) --------------------------------

ODVoxAudioProcessorEditor::BandLed::BandLed (ODVoxAudioProcessor& p, const char* parameterId)
    : processor (p)
{
    const auto all = odvox::params::declared();

    for (const auto& candidate : all)
        if (parameterId == juce::String (candidate.id))
        {
            info = candidate;
            break;
        }

    jassert (info.has_value());

    param = p.state().getParameter (parameterId);

    if (param != nullptr)
    {
        param->addListener (this);
        on = param->getValue() >= 0.5f;
    }

    setRepaintsOnMouseActivity (true);
}

ODVoxAudioProcessorEditor::BandLed::~BandLed()
{
    if (param != nullptr)
        param->removeListener (this);
}

void ODVoxAudioProcessorEditor::BandLed::paint (juce::Graphics& g)
{
    // CSS .bled : 8 px, dome radial — veilleuse sombre (off) ou violette vive
    // (on), bague sombre autour, SANS bloom (vocabulaire de la top bar).
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto centre = bounds.getCentre();

    // La bague : l'alcove autour de la lampe.
    g.setColour (colour (0xff14120f));
    g.fillEllipse (centre.x - bounds.getWidth(), centre.y - bounds.getHeight(),
                   bounds.getWidth() * 2.0f, bounds.getHeight() * 2.0f);

    juce::ColourGradient dome (colour (on ? Palette::ledHi : Palette::ledLow),
                               centre.x - bounds.getWidth() * 0.3f,
                               centre.y - bounds.getHeight() * 0.3f,
                               colour (on ? Palette::ledMid : 0xff241540),
                               centre.x + bounds.getWidth() * 0.3f,
                               centre.y + bounds.getHeight() * 0.3f, true);
    g.setGradientFill (dome);
    g.fillEllipse (bounds);

    g.setColour (colour (on ? 0x66ffffff : 0x14000000));
    g.drawEllipse (bounds, 1.0f);

    // Le reflet : un point de lumiere en haut a gauche (une LED vraie).
    g.setColour (colour (on ? 0xaaffffff : 0x30ffffff));
    g.fillEllipse (bounds.getX() + bounds.getWidth() * 0.15f,
                   bounds.getY() + bounds.getHeight() * 0.12f,
                   bounds.getWidth() * 0.3f, bounds.getHeight() * 0.3f);
}

void ODVoxAudioProcessorEditor::BandLed::mouseDown (const juce::MouseEvent&)
{
    // LA LED EST L'INTERRUPTEUR : clic = inverse l'etat du groupe. L'ecriture
    // passe par le parametre (gesture begin/end pour l'automation de l'hote).
    if (param == nullptr)
        return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (on ? 0.0f : 1.0f);
    param->endChangeGesture();

    // PAS de bascule manuelle ici : setValueNotifyingHost declenche
    // parameterValueChanged SYNCHRONIQUEMENT — c'est LUI la source unique de
    // verite (etat, repaint, assombrissement). Une bascule manuelle en plus
    // faisait l'aller-retour : la LED restait allumee pendant que le
    // parametre passait a 0, et le clic suivant reecrivait la meme valeur
    // (bug attrape par le test du vrai clic, 2026-09-30).
}

void ODVoxAudioProcessorEditor::BandLed::parameterValueChanged (int, float newValue)
{
    // Un preset, l'A/B ou l'automatisation a change l'etat : suivre.
    const bool nowOn = newValue >= 0.5f;

    if (nowOn != on)
    {
        on = nowOn;
        repaint();

        if (onChanged)
            onChanged (on);
    }
}

void ODVoxAudioProcessorEditor::BandLed::parameterGestureChanged (int, bool) {}

// --- Niveaux ----------------------------------------------------------------

ODVoxAudioProcessorEditor::LevelMeter::LevelMeter (const juce::String& labelText)
    : label (labelText)
{
    // L'instrument ne prend AUCUNE souris : le mini-knob de gain (un autre
    // composant, pose par-dessus la zone) doit recevoir les gestes.
    setInterceptsMouseClicks (false, false);
}

void ODVoxAudioProcessorEditor::LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Le label serigraphie (CSS .hlabel) : violet, 34 px, cote interieur.
    const auto labelArea = mirrored
        ? juce::Rectangle<float> (bounds.getRight() - 34.0f, bounds.getY() + 7.0f, 34.0f, 10.0f)
        : juce::Rectangle<float> (bounds.getX(), bounds.getY() + 7.0f, 34.0f, 10.0f);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (colour (0x99000000));
    g.drawText (label, labelArea.translated (0.0f, 1.0f), juce::Justification::centred, true);
    g.setColour (colour (Palette::inkAccent));
    g.drawText (label, labelArea, juce::Justification::centred, true);    // La fenetre : 372x9, encaissee (CSS .hwin). Le mini-knob de gain vit
    // ENTRE label et fenetre (pose par l'editeur, ce composant ne le dessine
    // pas) : cotes DOM — IN : fenetre a x78 local ; OUT : a x8 local.
    const auto window = mirrored
        ? juce::Rectangle<float> (bounds.getX() + 8.0f, bounds.getY() + 7.0f, 372.0f, 9.0f)
        : juce::Rectangle<float> (bounds.getRight() - 372.0f, bounds.getY() + 7.0f,
                                  372.0f, 9.0f);
    g.setColour (colour (0xff0b0a09));
    g.fillRoundedRectangle (window, 3.0f);
    g.setColour (colour (0x85000000));
    g.drawRoundedRectangle (window.translated (0.0f, -0.5f).reduced (0.5f), 3.0f, 1.0f);
    g.setColour (colour (0xff060505));
    g.drawRoundedRectangle (window.reduced (0.5f), 3.0f, 1.0f);

    // Le remplissage : depuis le cote du label (droite pour OUT), violet sombre
    // vers vif. La fraction : -60 dBFS = 0, 0 dBFS = 1 (echelle RMS lineaire).
    const float db = levelDb != nullptr ? levelDb() : -100.0f;
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (frac > 0.001f)
    {
        const auto filled = mirrored
            ? juce::Rectangle<float> (window.getRight() - (window.getWidth() - 2.0f) * frac,
                                      window.getY() + 1.0f,
                                      (window.getWidth() - 2.0f) * frac, window.getHeight() - 2.0f)
            : juce::Rectangle<float> (window.getX() + 1.0f, window.getY() + 1.0f,
                                      (window.getWidth() - 2.0f) * frac, window.getHeight() - 2.0f);

        juce::ColourGradient fill (colour (Palette::ledLow),
                                   mirrored ? filled.getRight() : filled.getX(), 0.0f,
                                   colour (Palette::ledMid),
                                   mirrored ? filled.getX() : filled.getRight(), 0.0f, false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (filled, 2.0f);
    }

    // Les crans de graduation aux quarts (CSS .hwin::after).
    g.setColour (colour (0x22ffffff));

    for (int q = 1; q < 4; ++q)
        g.fillRect (window.getX() + window.getWidth() * 0.25f * (float) q,
                    window.getY() + 1.0f, 1.0f, window.getHeight() - 2.0f);
}

// --- Top bar V2 : layout + peinture -----------------------------------------
void ODVoxAudioProcessorEditor::layoutTopBar (juce::Rectangle<int> bar)
{
    // Logo a gauche (la sérigraphie).
    productName.setBounds (bar.removeFromLeft (120).withSizeKeepingCentre (118, 26));

    // Le bloc CAL + statut a droite : statut SOUS le cap (ligne de la maquette).
    auto right = bar.removeFromRight (170);
    calibrateButton.setBounds (right.removeFromLeft (88)
                                   .withSizeKeepingCentre (82, kCapH));
    latencyLabel.setBounds (right.withSizeKeepingCentre (right.getWidth(), 16));

    // Transport centre : chevron vitre chevron, puis A B A>B a cote (mockup).
    const int centreW = 392;
    auto centre = bar.withSizeKeepingCentre (centreW, kCapH);

    previousPresetButton.setBounds (centre.removeFromLeft (34));
    centre.removeFromLeft (6);
    presetButton.setBounds (centre.removeFromLeft (170));
    centre.removeFromLeft (6);
    nextPresetButton.setBounds (centre.removeFromLeft (34));
    centre.removeFromLeft (14);
    slotAButton.setBounds (centre.removeFromLeft (36));
    centre.removeFromLeft (5);
    slotBButton.setBounds (centre.removeFromLeft (36));
    centre.removeFromLeft (5);
    copyButton.setBounds (centre.removeFromLeft (42));
}

void ODVoxAudioProcessorEditor::Cap::paint (juce::Graphics& g)
{
    const bool mini   = style == Style::mini;
    const bool round  = style == Style::round;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = round ? bounds.getHeight() * 0.5f
                               : (mini ? 4.0f : 7.0f);
    const bool down = pressed;

    // --- Le corps : metal bombe (identique au mockup) ------------------------
    if (! down)
    {
        g.setColour (colour (0x99000000));
        g.fillRoundedRectangle (bounds.translated (0.0f, 2.5f), radius);
    }

    juce::ColourGradient body (colour (style == Style::silver ? 0xffb9b4ab : 0xff4a4742),
                               bounds.getX(), bounds.getY(),
                               colour (style == Style::silver ? 0xff6b675f : 0xff1b1916),
                               bounds.getX(), bounds.getBottom(), false);

    if (down)
        body = juce::ColourGradient (colour (style == Style::silver ? 0xff6b675f : 0xff1b1916),
                                     bounds.getX(), bounds.getY(),
                                     colour (style == Style::silver ? 0xffb9b4ab : 0xff3a3733),
                                     bounds.getX(), bounds.getBottom(), false);

    {
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.saveState();
        g.reduceClipRegion (clip);
        g.setGradientFill (body);
        g.fillRect (bounds);
        g.restoreState();
    }

    // Contour de definition.
    g.setColour (colour (style == Style::silver ? 0xff4c483f : 0xff0d0c0a));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    // Biseaux internes.
    g.setColour (colour ((down ? 0x2e000000 : 0x38ffffff)));
    g.drawRoundedRectangle (bounds.translated (0.0f, down ? -1.0f : 1.0f)
                                    .reduced (1.5f), radius, 1.0f);
    g.setColour (colour ((down ? 0x30ffffff : 0x8c000000)));
    g.drawRoundedRectangle (bounds.translated (0.0f, down ? 1.0f : -1.0f)
                                    .reduced (1.5f), radius, 1.0f);

    // --- L'etat : liseré violet NET (couronne A/B) --------------------------
    if (ringOn)
    {
        g.setColour (colour (Palette::ledGlow));
        g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.0f, 1.5f);
    }

    // --- La lampe temoin (CAL) ----------------------------------------------
    if (hasLamp)
    {
        const float lampR = 4.5f;
        const auto lampCentre = juce::Point<float> (bounds.getX() + 13.0f,
                                                    bounds.getCentreY());

        g.setColour (colour (0xff14120f));
        g.fillEllipse (lampCentre.x - lampR - 3.0f, lampCentre.y - lampR - 3.0f,
                       (lampR + 3.0f) * 2.0f, (lampR + 3.0f) * 2.0f);
        g.setColour (colour (0x14ffffff));
        g.drawEllipse (lampCentre.x - lampR - 3.0f, lampCentre.y - lampR - 3.0f,
                       (lampR + 3.0f) * 2.0f, (lampR + 3.0f) * 2.0f, 1.0f);

        juce::ColourGradient lamp (colour (lampOn ? Palette::ledHi : Palette::ledLow),
                                   lampCentre.x - lampR * 0.4f,
                                   lampCentre.y - lampR * 0.4f,
                                   colour (lampOn ? Palette::ledMid : 0xff241540),
                                   lampCentre.x + lampR * 0.4f,
                                   lampCentre.y + lampR * 0.4f, true);
        {
            juce::Path clip;
            clip.addEllipse (lampCentre.x - lampR, lampCentre.y - lampR,
                             lampR * 2.0f, lampR * 2.0f);
            g.saveState();
            g.reduceClipRegion (clip);
            g.setGradientFill (lamp);
            g.fillRect (bounds);
            g.restoreState();
        }
    }

    // --- Le chevron VIOLET : la signature du geste ---------------------------
    // Rond + chevron = transport preset (top bar) ; rectangulaire + chevron =
    // division du delay. Les fleches ASCII ont quitte l'interface.
    if (chevron != 0)
    {
        const auto centre = bounds.getCentre();
        const float half  = 3.5f;    // demi-hauteur du chevron (8 px, maquette)
        const float depth = 3.0f;    // profondeur horizontale

        juce::Path arrow;
        // Cotes maquette (mockup_cards : div-prev "M7 1.5 L3.5 5 L7 8.5",
        // div-next "M3 1.5 L6.5 5 L3 8.5") : la POINTE est du cote verse par
        // le chevron — chevron > 0 (suivant) pointe a DROITE, chevron < 0
        // (precedent) pointe a GAUCHE. Les deux mini-caps etaient dessines
        // inverses (retour utilisateur 2026-09-30).
        const float tipX  = centre.x + chevron * depth;
        const float tailX = centre.x - chevron * depth;

        arrow.startNewSubPath (tailX, centre.y - half);
        arrow.lineTo (tipX, centre.y);
        arrow.lineTo (tailX, centre.y + half);

        g.setColour (colour (Palette::inkAccent));
        g.strokePath (arrow, juce::PathStrokeType (2.2f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // --- Le libelle ----------------------------------------------------------
    if (label.isEmpty())
        return;

    auto textArea = bounds;

    if (hasLamp)
        textArea.setLeft (bounds.getX() + 22.0f);

    const bool silver = style == Style::silver;
    const float fontH = mini ? 8.0f : (float) getHeight() * 0.36f;

    g.setFont (juce::FontOptions (fontH, juce::Font::bold));

    if (silver)
    {
        g.setColour (colour (0x50ffffff));
        g.drawText (label, textArea.translated (0.0f, 1.0f), juce::Justification::centred, true);
        g.setColour (colour (0xff26231e));
    }
    else
    {
        g.setColour (colour (0xb3000000));
        g.drawText (label, textArea.translated (0.0f, 1.0f), juce::Justification::centred, true);
        g.setColour (colour (0xffcfc9bf));
    }

    g.drawText (label, textArea, juce::Justification::centred, true);
}

void ODVoxAudioProcessorEditor::Cap::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    repaint();
}

void ODVoxAudioProcessorEditor::Cap::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    repaint();

    if (onClick && e.mouseWasClicked() && contains (e.position))
        onClick();
}

void ODVoxAudioProcessorEditor::PresetWindow::paintButton (juce::Graphics& g, bool, bool)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = 6.0f;

    // La vitre est DANS la plaque : fond sombre, ombre EN HAUT.
    g.setColour (colour (0xff12110f));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (colour (0x85000000));
    g.drawRoundedRectangle (bounds.translated (0.0f, -0.5f).reduced (0.5f), radius, 1.0f);
    g.setColour (colour (0x12ffffff));
    g.drawRoundedRectangle (bounds.translated (0.0f, 0.5f).reduced (0.5f), radius, 1.0f);
    g.setColour (colour (0xff060505));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    {
        juce::ColourGradient sheen (colour (0x0dffffff), 0.0f, bounds.getY(),
                                    colour (0x40000000), 0.0f, bounds.getBottom(), false);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.saveState();
        g.reduceClipRegion (clip);
        g.setGradientFill (sheen);
        g.fillRect (bounds);
        g.restoreState();
    }

    auto textArea = bounds.reduced (12.0f, 0.0f);
    const float fontH = (float) getHeight() * 0.38f;

    g.setFont (juce::FontOptions (fontH, juce::Font::bold));
    g.setColour (colour (0x99000000));
    g.drawText (getButtonText(), textArea.translated (0.0f, 1.0f),
                juce::Justification::centred, true);
    g.setColour (colour (0xffd9d4c8));
    g.drawText (getButtonText(), textArea, juce::Justification::centred, true);

    g.setColour (colour (0xff6d675e));
    juce::Path caret;
    const float cx = bounds.getRight() - 14.0f;
    const float cy = bounds.getCentreY();
    caret.addTriangle (cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 2.5f);
    g.fillPath (caret);
}

// --- Pose des zones (maquette validee) --------------------------------------

void ODVoxAudioProcessorEditor::layoutHero (juce::Rectangle<int> hero)
{
    juce::ignoreUnused (hero);   // les cotes sont les constantes du mockup

    // L'ecran de valeur du COMP, puis son knob (96 px, maquette .slot : le
    // disque, l'ecran de valeur dessous, le nom grave sous l'ecran — le nom
    // est peint par paint() a kCompNameY).
    compValueBounds = { kCompValX, kCompValY, kCompValW, kCompValH };

    if (compKnob != nullptr)
        compKnob->setBounds (kCompKnobX, kCompKnobY, kCompKnobSz + 16, kCompKnobSz + 26);

    // Le LCD : l'ecran veritable, a l'echelle 0,44, pose a DROITE du heros
    // (maquette : 331 px de large, la hauteur suit le ratio 753:419). Le
    // dessin reste 1:1 par la transform : JUCE mappe les clics.
    // ATTENTION convention JUCE : la transform s'applique AUSSI a l'origine
    // des bounds (visuel = scale x position). Pour un coin haut-gauche VISUEL
    // a (kLcdVisualX, kLcdVisualY), les bounds valent donc visual / 0,44 —
    // c'est verifie au pixel par tools/compare_capture_mockup.py.
    //
    // ESSAI 2026-10-02 : kLcdEnabled = false masque le LCD (la colonne de
    // droite est prise par les bandes empilees). Le composant reste cable —
    // seul l'affichage change, remettre le drapeau suffit a le rendre a sa
    // pose d'origine.
    if constexpr (kLcdEnabled)
    {
        lcd.setTransform (juce::AffineTransform::scale (
            (float) kLcdScaleNum / (float) kLcdScaleDen));
        lcd.setBounds ((int) std::lround ((float) kLcdVisualX * kLcdScaleDen
                                              / (float) kLcdScaleNum),
                       (int) std::lround ((float) kLcdVisualY * kLcdScaleDen
                                              / (float) kLcdScaleNum),
                       odvox::lcd::kWidth, odvox::lcd::kHeight);
        lcd.setVisible (true);
    }
    else
    {
        lcd.setVisible (false);
    }

    // Les toggles du heros : empiles SOUS le COMP (colonne de gauche, essai
    // 2026-10-02) — positions absolues, ils ne decalentent pas le contenu.
    // Ancienne pose « cartes V6 » : a la meme ligne, en bas a DROITE.
    lowCutToggle.setBounds (kLowCutTogX, kHeroTogY, kLowCutTogW, kHeroTogH);
    dcFilterToggle.setBounds (kDcFilterTogX, kHeroTog2Y, kDcFilterTogW, kHeroTogH);
}

ODVoxAudioProcessorEditor::Rotary* ODVoxAudioProcessorEditor::knobById (const char* id) const
{
    for (const auto& knob : knobs)
        if (knob.id == juce::String (id))
            return knob.knob.get();

    return nullptr;
}

void ODVoxAudioProcessorEditor::refreshDivisionBox()
{
    if (! delayTimeCombo.info.has_value())
        return;

    const int index = juce::jlimit (0, odvox::Delay::kNumDivisions - 1,
                                    (int) std::lround (
                                        odvox::params::normalisedToActual (
                                            "delay_time",
                                            delayTimeCombo.param != nullptr
                                                ? delayTimeCombo.param->getValue() : 0.0f)));
    juce::StringArray parts;
    parts.addTokens (juce::String (delayTimeCombo.info->choices), "|", "");
    parts.removeEmptyStrings (true);

    const juce::String text = juce::isPositiveAndBelow (index, parts.size())
                                  ? parts[(size_t) index] : juce::String ("--");

    delayTimeCombo.box.setTooltip (juce::String ("delay_time : 21 divisions (")
                                   + text + ")");
    delayTimeCombo.box.repaint();
}

void ODVoxAudioProcessorEditor::applyBandDim()
{
    // L'assombrissement du contenu quand la bande est OFF (maquette :
    // opacity .38). La peinture des LED et l'etat des parametres portent le
    // reste : les enfants baissent d'alpha, la bande « s'eteint ».
    for (size_t i = 0; i < bandLeds.size() && i < 4; ++i)
    {
        const float alpha = bandLeds[i]->isOn() ? 1.0f : 0.42f;

        for (auto* control : bandControls[i])
            control->setAlpha (alpha);
    }
}

// --- Actions de la barre ---------------------------------------------------

int ODVoxAudioProcessorEditor::getActiveSlot() const
{
    return processor.getActiveSlot();
}

void ODVoxAudioProcessorEditor::toggleAorB()
{
    // L'etat courant VIT dans le slot actif : capture AVANT de partir.
    processor.captureSlot (getActiveSlot());

    if (! processor.slotHasContent (1 - getActiveSlot()))
        processor.copySlot (getActiveSlot(), 1 - getActiveSlot());

    processor.recallSlot (1 - getActiveSlot());
    refreshABButtons();
    refreshPresetLabel();
}

void ODVoxAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < processor.getNumFactoryPresets(); ++i)
        menu.addItem (i + 1, processor.getFactoryPresetName (i), true,
                      i == selectedPreset);

    // Le callback tourne APRES showMenuAsync, potentiellement apres la
    // destruction de l'editeur : SafePointer = nul si mort.
    juce::Component::SafePointer<ODVoxAudioProcessorEditor> safeThis { this };

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            safeThis->selectedPreset = result - 1;

                            if (safeThis->processor.loadFactoryPreset (safeThis->selectedPreset))
                                safeThis->refreshPresetLabel();
                        });
}

void ODVoxAudioProcessorEditor::stepPreset (int direction)
{
    const int count = processor.getNumFactoryPresets();

    if (count == 0)
        return;

    selectedPreset = ((selectedPreset + direction) % count + count) % count;

    if (processor.loadFactoryPreset (selectedPreset))
        refreshPresetLabel();
}

void ODVoxAudioProcessorEditor::refreshPresetLabel()
{
    const int count = processor.getNumFactoryPresets();

    const juce::String name =
        juce::isPositiveAndBelow (selectedPreset, count)
            ? processor.getFactoryPresetName (selectedPreset)
            : juce::String ("Init");

    presetButton.setButtonText (name);
    presetButton.setTooltip (juce::String ("A/B : ")
                             + (getActiveSlot() == 0 ? "A" : "B"));
}

void ODVoxAudioProcessorEditor::refreshABButtons()
{
    const int slot = getActiveSlot();

    // La couronne violette designe l'emplacement actif : la POSITION lit
    // l'etat, pas une couleur de fond (vocabulaire du mockup).
    slotAButton.ringOn = slot == 0;
    slotBButton.ringOn = slot == 1;

    copyButton.setTooltip (juce::String ("Copier ")
                           + (slot == 0 ? "A" : "B") + " vers "
                           + (slot == 0 ? "B" : "A") + ".");

    slotAButton.repaint();
    slotBButton.repaint();
}

void ODVoxAudioProcessorEditor::refreshCalibrateButton()
{
    using State = odvox::Calibrator::State;

    // La lampe du cap CAL : allumee pendant l'attente et la mesure.
    calibrateButton.lampOn = false;

    switch (processor.calibrationState())
    {
        case State::waiting:
            calibrateButton.label = "Parlez...";
            calibrateButton.lampOn = true;
            calibrationStatus.setText ("Attente du signal", juce::dontSendNotification);
            break;
        case State::measuring:
            calibrateButton.label = "Mesure";
            calibrateButton.lampOn = true;
            calibrationStatus.setText ("Mesure en cours", juce::dontSendNotification);
            break;
        case State::done:
        {
            const float peak = processor.calibrationMeasuredPeakDb();
            calibrateButton.label = "CAL";
            calibrationStatus.setText ("Crete mesuree : " + juce::String (peak, 1) + " dBFS",
                                       juce::dontSendNotification);
            break;
        }
        case State::abandoned:
            calibrateButton.label = "CAL";
            calibrationStatus.setText ("Aucun signal detecte", juce::dontSendNotification);
            break;
        case State::idle:
            calibrateButton.label = "CAL";
            calibrationStatus.setText ({}, juce::dontSendNotification);
            break;
    }

    calibrateButton.repaint();
}
