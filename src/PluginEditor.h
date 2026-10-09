#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EqCurve.h"      // odvox::EqCurveModel : le LCD le reconstruit
#include "EqCurveView.h"  // odvox::lcd : dimensions de l'ecran
#include "Metal.h"
#include "Parameters.h"
#include "Palette.h"
#include "PluginProcessor.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

/** La palette partagee (src/Palette.h) : l'editeur et les composants qui
    dessinent dans les cartes tirent les memes couleurs, donc l'identite visuelle
    se change en un seul endroit. */
namespace Palette = odvox::palette;

/**
    Editeur minimal (lot F1.5b).

    La **structure** represente la chaine dans l'ordre du traitement : des
    cartes de modules avec LED d'activite, un
    menu de presets et un bouton de calibration avec retour d'etat.

    Les cartes sont data-driven : chacune liste les parametres Essential de son
    module (PRD.md §3.4), tous les 25 doivent s'y retrouver — un paramètre
    Essential absent de l'interface est un bug de specification, pas un choix.

    L'**identite visuelle** est volontairement sobre et interim : palette sombre,
    accent orange. L'identite forte viendra apres — il suffira de changer
    `Palette` et le rendu des cartes, pas la structure.
*/
class ODVoxAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit ODVoxAudioProcessorEditor (ODVoxAudioProcessor&);
    ~ODVoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Verification de couverture (tests d'interface) : les identifiants
        Essential NON portes par un controle de l'interface. Vide = couverture
        complete. `input_calibrate` est une action (cap CAL), `hq_mode` est
        toujours actif sans reglage affiche : tous deux exclus par convention.
        Ce test doit VRAIMENT echouer (un jassert de constructeur se contente de
        logger hors debogueur) — voir tests/EditorTests.cpp. */
    juce::StringArray uncoveredEssentialIds() const;

private:
    // Types prives declares plus bas : forward-declares pour les accesseurs.
    struct BandLed;
    struct LevelMeter;
    struct Toggle;

public:
    /** Lecture de controle (tests d'interface) : la LED-interrupteur de bande
        (0..3, ordre de la grille : TON, FX, REVERB, DELAY) et les instruments
        de niveau (0 = IN, 1 = OUT). Les types sont prives : le test les lit
        par `auto*` sans les nommer. */
    const BandLed* bandLed (int index) const;
    const LevelMeter* levelMeter (int index) const;

    /** Lecture de controle (tests) : un toggle par identifiant de parametre
        (nullptr si absent). Le type est prive : le test le lit par `auto*`. */
    const Toggle* toggleById (const char* parameterId) const;

    /** Declencheurs de la navigation de division (tests, et raccourcis
        clavier a venir) : meme chemin que le onClick des mini-caps, clampe
        dans les 21 divisions. */
    void clickedDivPrev();
    void clickedDivNext();

    /** Lecture de controle (tests) : la vitre de division doit contenir les
        21 libelles du catalogue et afficher la division courante — une
        ComboBox sans item ne rend aucun texte (« no choice »). */
    int divisionItemCount() const;
    juce::String divisionLabelText() const;

    /** Lecture de controle (tests) : l'alpha courant du contenu de la bande
        (1,0 groupe allume, ~0,42 groupe eteint — applyBandDim). */
    float bandDimAlpha (int index) const;

    /** La fenetre fait EXACTEMENT le corps de pedale transpose (maquette
        tools/mockup_cards.html : scene 980x516, pedale photo incluse). */
    static constexpr int kDefaultWidth  = 980;
    static constexpr int kDefaultHeight = 516;

private:
    // --- Top bar (maquette tools/mockup_topbar.html, transposee en code) ----

    /** Une capsule hardware de la top bar : corps metal bombe (lisiere claire
        en haut, ventre sombre), ombre portee (elle SORT de la plaque) et
        ENFONCEMENT au clic (gradient inverse + 1 px vers le bas) — le faux-3D
        valide en mockup. MAT : aucun glow ; l'etat se lit sur un liseré net
        (couronne A/B) ou une LED encastrée (CAL), jamais sur une lumiere qui
        bave — retour utilisateur du 2026-09-25 : "les lueurs, trop chelou".
        Dessin 100% code (meme grammaire que Metal.h).

        `mini` : la capsule RECTANGULAIRE de la division du delay (18x16 dans
        la maquette, coins arrondis 4, grille de police reduite) — meme corps
        metal sombre, juste plus petite.

        `chevron` : le chevron VIOLET pose sur le cap — c'est la signature du
        geste : rond + chevron = transport PRESET (top bar) ; rectangulaire +
        chevron = division du delay. Les fleches ASCII sont supprimees. */
    struct Cap final : public juce::Component,
                       public juce::SettableTooltipClient
    {
        enum class Style { dark, silver, round, mini };

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        Style style = Style::dark;

        /** Libelle grave (texte, ou rien quand le cap porte un chevron). */
        juce::String label;

        /** Chevron VIOLET pose sur le corps : 0 = aucun, -1 = vers la gauche
            (precedent), +1 = vers la droite (suivant). La signature du geste
            (maquette tools/mockup_cards.html) : les caps du transport preset
            sont RONDS a chevron, les mini-caps de la division DELAY sont
            rectangulaires a chevron. */
        int chevron = 0;

        /** Lampe temoin encastree dans sa bague, cote gauche du cap (CAL). */
        bool hasLamp = false;
        bool lampOn  = false;

        /** Couronne d'etat : liseré violet net quand ce cap designe l'actif
            (slot A ou B selectionne). */
        bool ringOn = false;

        std::function<void()> onClick;

    private:
        bool pressed = false;
    };

    /** La vitre du preset : ecran ENCASTRE dans la plaque (l'inverse des
        caps — il est dans le metal, pas devant), nom grave au centre, caret
        a droite. Un Button pour le menu (onClick), peint maison. */
    struct PresetWindow final : public juce::Button
    {
        PresetWindow() : juce::Button ("preset") {}

        void paintButton (juce::Graphics&, bool, bool) override;
    };

    /** Refonte UI (cartes V6, transposee de tools/mockup_cards.html) : le
        mecanisme de transition est SUPPRIME — le nouvel habillage couvre tout,
        il n'y a plus rien a masquer. Les composants sous l'habillage existant
        (courbe d'EQ pleine largeur, cartes de modules) ont disparu du header :
        le LCD vit desormais dans le heros (EqCurveView integree), et les
        controles sont poses directement sur la plaque. */

    /** Un rotary dessine maison : arc de progression 7h -> 17h, valeur reelle
        au centre, unite sous le cadre. Un controle par parametre numerique
        (dB, %, Hz, ms) — accent orange a la prise.

        Il tient son parametre APVTS par identifiant et s'y abonne : il ne
        repeint que quand la valeur change, sans polling. Le double-clic rend
        la valeur par defaut du catalogue (US-02 AC2). */
    struct Rotary final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::AudioProcessorParameter::Listener
    {
        Rotary (ODVoxAudioProcessor&, const char* parameterId, bool mini = false);
        ~Rotary() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** Valeur normalisee 0..1 du parametre. */
        float normalised() const noexcept;

        /** Nom du parametre (catalogue), affiche sous le controle. */
        juce::String name() const;

        /** Definition de catalogue (l'identifiant est forcement declare). */
        std::optional<odvox::params::Info> info;

        /** Habillage (maquette cartes V6) : le nom grave sous le disque. PAS
            de valeur au centre : gravee, elle se melangeait au balayage de
            l'encoche, et la pastille au survol n'a pas survive a l'essai
            (retour utilisateur 2026-09-30). Le heros COMP garde son ecran de
            valeur separe (peint par l'editeur). Les mini-knobs des niveaux
            n'affichent rien. */
        bool showLabel       = true;

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

        juce::AudioProcessorParameter* param = nullptr;

        float dragStartValue = 0.0f;
        float dragStartPixel = 0.0f;
    };

    /** Un parametre `choice` : menu deroulant, attache a l'APVTS. Constructeur
        par defaut pour les membres habilles a la main (la vitre de division). */
    struct Combo final : public juce::Component
    {
        Combo() = default;
        Combo (ODVoxAudioProcessor&, const char* parameterId);

        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        juce::AudioProcessorParameter* param = nullptr;
        std::optional<odvox::params::Info> info;
    };

    /** Un parametre `boolean` : interrupteur du heros ou du SYNC, attache a
        l'APVTS. Dessin maison (coulisse + pastille, libelle a droite), cotes
        de la maquette (.sw/.swlbl) : `labelText` surcharge le nom du catalogue
        (LOW CUT / DC FILTER / SYNC en capitales serigraphie). */
    struct Toggle final : public juce::Component,
                          private juce::AudioProcessorParameter::Listener,
                          private juce::AsyncUpdater
    {
        Toggle (ODVoxAudioProcessor&, const char* parameterId);
        ~Toggle() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        juce::AudioProcessorParameter* param = nullptr;
        std::optional<odvox::params::Info> info;

        juce::String labelText;

        /** L'identifiant du parametre, tel que donne au ctor : l'accesseur de
            lecture (tests) reconnait les toggles par la. */
        juce::String parameterId;

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
        void handleAsyncUpdate() override;
    };

    /** LA LED DE BANDE EST L'INTERRUPTEUR DU GROUPE (maquette V6, tranche avec
        l'utilisateur pour la bande FX) : clic = active/desactive les effets de
        la bande. L'etat vit dans un parametre declare (`eq_on`, `fx_on`,
        `delay_on`, `reverb_on`) : il survit a l'A/B, aux presets et a la
        session. OFF : veilleuse sombre (ledLow), le contenu de la bande est
        assombri et desature (la peinture des bandes lit `isOn()`). */
    struct BandLed final : public juce::Component,
                           private juce::AudioProcessorParameter::Listener
    {
        BandLed (ODVoxAudioProcessor& p, const char* parameterId);
        ~BandLed() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        /** Etat courant du groupe (le parametre, normalise >= 0,5). */
        bool isOn() const noexcept { return on; }

        std::optional<odvox::params::Info> info;

        /** Branche par l'editeur : assombrir le contenu de la bande quand le
            groupe passe OFF (et le rallumer). */
        std::function<void(bool)> onChanged;

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

        ODVoxAudioProcessor& processor;
        juce::AudioProcessorParameter* param = nullptr;
        bool on = true;
    };

    /** Un vumetre HORIZONTAL des niveaux (refonte UI, cartes V6) : deux
        instruments INDEPENDANTS aux coins du bas de la fenetre — IN a gauche
        (avec son mini-knob input_gain), OUT a droite (output_gain). Remplissage
        depuis le cote du label, crans de graduation aux quarts, fenetre
        encaissee. La valeur vient des taps RMS du processeur (atomiques),
        repain au timer de l'editeur. Miroir : `mirrored` remplit depuis la
        droite (OUT). Le mini-knob de gain dessine dedans est un vrai Rotary
        (drag vertical, double-clic = defaut) — il est attache par l'editeur,
        pas possede. */
    struct LevelMeter final : public juce::Component
    {
        explicit LevelMeter (const juce::String& labelText);

        void paint (juce::Graphics&) override;

        /** Niveau courant, en dBFS (taps RMS du processeur, lues au timer). */
        std::function<float()> levelDb;

        /** Miroir : remplissage depuis la DROITE (bande OUT). */
        bool mirrored = false;

    private:
        juce::String label;
    };

    void timerCallback() override;

    void refreshPresetLabel();
    void refreshCalibrateButton();
    void refreshABButtons();
    void refreshLatencyLabel();

    void showPresetMenu();
    void stepPreset (int direction);
    void toggleAorB();
    int  getActiveSlot() const;

    /** Pose la top bar V2 dans la bande donnee : logo, transport centre
        (◀ vitre ▶), A/B/A>B, CAL + statut a droite — les cotes du mockup
        (variables CSS --bar-x, --bar-y, --bar-h). */
    void layoutTopBar (juce::Rectangle<int> bar);

    /** Pose l'ETAGE HERO (maquette .zones/.hero) : COMP a gauche (96 px,
        ecran de valeur dessous), le LCD (EqCurveView a l'echelle 0,44) a
        droite, la ligne de toggles LOW CUT / DC FILTER en bas a droite de
        l'alcoVe — ils ne decalent pas le contenu (absolute, cotes mockup). */
    void layoutHero (juce::Rectangle<int> hero);

    /** Reconstruit l'affichage de la division du delay (vitre + infobulle)
        quand la valeur du parametre change. */
    void refreshDivisionBox();

    /** Le Rotary d'un parametre (nullptr si l'id n'est pas un knob) : la pose
        des bandes et du heros lit les defs par id. */
    Rotary* knobById (const char* id) const;

    ODVoxAudioProcessor& processor;

    // --- Top bar -------------------------------------------------------------

    /** Sérigraphie du produit : OD VOX, peinture plate violette (mat),
        comme le logo de la maquette. */
    juce::Label productName;

    /** Latence reportee a l'hote (AC2 de US-12), lue sur le processeur
        (`getLatencySamples()`), rafraichie au timer. Le statut de calibration
        vit SOUS le cap CAL (ligne de la maquette). */
    juce::Label latencyLabel;

    /** La vitre du preset : ecran encastré, nom du preset grave au centre. */
    PresetWindow presetButton;

    Cap previousPresetButton;
    Cap nextPresetButton;
    Cap slotAButton;
    Cap slotBButton;
    Cap copyButton;
    Cap calibrateButton;

    juce::Label calibrationStatus;
    int selectedPreset = -1;

    // --- Niveaux (refonte cartes V6) ------------------------------------------

    /** Les deux instruments de niveau, independants, aux coins du bas :
        IN a gauche (label + mini-knob input_gain + fenetre), OUT a droite
        (fenetre + mini-knob output_gain + label). Les mini-knobs vivent dans
        `knobs` comme tous les rotaries (pose au layout) ; les fenetres sont
        peintes par LevelMeter. */
    LevelMeter inMeter;
    LevelMeter outMeter;

    // --- Controles (refonte cartes V6) ---------------------------------------

    /** L'ecran LCD du heros : la courbe d'EQ interactive (F1.6c, US-04), posee
        a l'echelle 0,44 dans son bezel. Les gestes sont mappes par la transform
        de JUCE : le dessin reste 1:1, les clics tombent juste. */
    EqCurveView lcd;

    /** Un rotary par parametre numerique, possede ici (l'ancienne structure
        data-driven par cartes est remplacee par le pose des bandes de la
        maquette). `knob` : le Rotary construit, pour le mini-knob du heros. */
    struct Knob
    {
        std::unique_ptr<Rotary> knob;
        const char* id = nullptr;
    };

    std::vector<Knob>                    knobs;

    /** Les 4 LED-interrupteurs de bande, dans l'ordre de la grille :
        TON (eq_on), FX (fx_on), REVERB (reverb_on), DELAY (delay_on). */
    std::vector<std::unique_ptr<BandLed>> bandLeds;

    /** La vitre de la division du delay (delay_time, choice a 21 valeurs) et
        ses deux mini-caps de parcours — le trio colle du mockup, pose sous
        DUCK. La vitre ouvre le menu deroulant natif de la ComboBox, dont elle
        est l'habillage. */
    Combo delayTimeCombo;
    Cap   divPrevCap;
    Cap   divNextCap;

    /** Les toggles du heros : LOW CUT (lowcut_amount) et DC FILTER
        (output_dc_filter) — poses en bas a droite de l'alcoVe — et le SYNC
        de la bande DELAY (sous le trio de division). */
    Toggle lowCutToggle;
    Toggle dcFilterToggle;
    Toggle delaySyncToggle;

    /** Pointeurs de service : le knob COMP du heros (son ecran de valeur est
        peint par l'editeur), les mini-knobs des niveaux. Ils vivent aussi dans
        `knobs` (la pose et la couverture Essential passent par la meme liste). */
    Rotary* compKnob    = nullptr;
    Rotary* inGainKnob  = nullptr;
    Rotary* outGainKnob = nullptr;

    /** Le rectangle de l'ecran de valeur COMP (coords editeur) : repeint au
        timer quand la valeur bouge. */
    juce::Rectangle<int> compValueBounds;

    /** Les controles de chaque bande, pour l'assombrissement quand la LED
        groupe passe OFF (meme ordre que `bandLeds`). */
    std::vector<juce::Component*> bandControls[4];

    /** Applique l'assombrissement du contenu des bandes selon l'etat de leurs
        LED (maquette : opacity .38, saturate .4 — approxime par setAlpha). */
    void applyBandDim();

    /** Un pas de navigation de division : lit l'index courant depuis la valeur
        normalisee du parametre choice (index = round(norm * 20) — JUCE
        AudioParameterChoice), l'increment/decremente en clamant, et ecrit via
        gesture+notify (l'hot reçoit l'automatisation). */
    void stepDivision (int direction);

    /** L'identite hardware (src/Metal.h) : applique a CET editeur seulement,
        jamais au L&F global (deux fenetres de plugin ne doivent pas se battre
        pour le style de l'application hote). */
    odvox::metal::HardwareLookAndFeel look;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ODVoxAudioProcessorEditor)
};
