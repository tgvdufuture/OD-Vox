#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Calibrator.h"
#include "Compressor.h"
#include "DeEsser.h"
#include "Delay.h"
#include "DcBlocker.h"
#include "Reverb.h"
#include "Drive.h"
#include "Eq.h"
#include "Gate.h"
#include "Image.h"
#include "LowCut.h"
#include "Presets.h"
#include "Smoothing.h"
#include "Spectrum.h"

#include <array>
#include <atomic>
#include <map>
#include <vector>

/**
    OD Vox.

    F1.2 : bus des parametres `Essential` figes, lissage, presets `.odvoxpreset`
    et emplacements A/B.
    F1.3 a F1.5 : debut de la chaine — gain d'entree avec calibration, gate, low
    cut. Les modules suivants (F1.6 et au-dela) s'inserent dans cet ordre, celui
    du pipeline de PRD.md §3.1.
*/
class ODVoxAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorParameter::Listener
{
public:
    ODVoxAudioProcessor();
    ~ODVoxAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Remet l'etat dynamique des modules a zero (arret du transport chez
        l'hote). Sans cela, une reduction figee contamine la passe suivante —
        les passes de mesures en seraient faussees (lecon du F1.7 : comparer
        deux processeurs neufs). Les hotes appellent reset() hors traitement. */
    void reset() override
    {
        gate.reset();
        lowCut.reset();
        eq.reset();
        compressor.reset();
        deEsser.reset();
        drive.reset();
        image.reset();
        dcBlocker.reset();

        // Le spectre repart du silence : garder l'image du signal precedent
        // ferait croire a un signal qui n'existe plus.
        spectrumTapBuffer.reset();
        spectrumAnalyzerEngine.reset();

        lastGateReduction  = 0.0f;
        lastCompReduction  = 0.0f;
        lastDeEssReduction = 0.0f;

        // Les vumetres IN/OUT repartent du silence avec la chaine (arret du
        // transport) : sinon un niveau fige ferait croire a un signal absent.
        lastInputRms.store (0.0f, std::memory_order_relaxed);
        lastOutputRms.store (0.0f, std::memory_order_relaxed);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
    const odvox::Smoothing& modulation() const noexcept { return smoothing; }

    // --- Bus de parametres (F1.2) -------------------------------------------

    /** Valeurs normalisees de tous les parametres declares, indexees par id. */
    std::map<juce::String, float> captureParameters() const;

    /** Applique des valeurs normalisees 0..1. Les identifiants inconnus sont
        ignores et journalises dans `warnings` ; renvoie le nombre de parametres
        effectivement appliques. N'ecrit jamais ailleurs que dans les parametres
        declares (AC3 de US-01). */
    int applyParameters (const std::map<juce::String, float>& values,
                         juce::StringArray& warnings);

    /** Ecrit une valeur reelle dans un parametre declare. Renvoie false si
        l'identifiant n'existe pas. */
    bool setParameterActual (juce::StringRef id, float actual);

    // --- Presets (F1.2) ------------------------------------------------------

    int getNumFactoryPresets() const;
    juce::String getFactoryPresetName (int index) const;
    bool loadFactoryPreset (int index);

    bool loadPreset (const odvox::Preset&, juce::StringArray& warnings);
    odvox::Preset capturePreset (const juce::String& name) const;

    // --- Emplacements A/B (F1.2, US-11) --------------------------------------

    void captureSlot (int slot);
    void recallSlot (int slot);
    void copySlot (int from, int to);
    bool slotHasContent (int slot) const;
    void setActiveSlot (int slot) noexcept { activeSlot = slot; }
    int getActiveSlot() const noexcept { return activeSlot; }

    // --- Calibration du gain d'entree (F1.3, US-06) --------------------------

    /** Declenche la mesure. Sans effet si une mesure est deja en cours. */
    void startCalibration();

    /** Annule la mesure en cours et laisse le gain d'entree inchange
        (AC2 de US-06). */
    void cancelCalibration();

    odvox::Calibrator::State calibrationState() const noexcept { return calibrator.getState(); }
    float calibrationMeasuredPeakDb() const noexcept { return calibrator.measuredPeakDb(); }

    // --- Observation (tests et interface) ------------------------------------

    /** Reduction maximale atteinte par le gate depuis le dernier `prepareToPlay`.
        AC2 de US-05 demande qu'un tel vumetre s'ecarte de moins de 1 dB de la
        reduction reellement appliquee. */
    float lastGateReductionDb() const noexcept { return lastGateReduction; }

    /** Reduction instantanee du compresseur, en dB (vumetre, AC2 de US-05). */
    float lastCompReductionDb() const noexcept { return lastCompReduction; }

    /** Reduction instantanee de la bande sifflante, en dB (vumetre F1.8). */
    float lastDeEssReductionDb() const noexcept { return lastDeEssReduction; }

    /** Niveau d'ENTREE (avant gain et modules), RMS du dernier bloc, en dBFS
        (vumetre IN de la refonte UI). Le tap vit dans `processBlock`, la lecture
        est faite par le thread de messages : atomique relâchée, aucune
        synchronisation — un niveau affiche a une frame de retard, peu importe. */
    float inputLevelDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (lastInputRms.load (std::memory_order_relaxed));
    }

    /** Niveau de SORTIE (apres le gain de sortie), RMS du dernier bloc, en dBFS
        (vumetre OUT de la refonte UI). Meme contrat que `inputLevelDb()`. */
    float outputLevelDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (lastOutputRms.load (std::memory_order_relaxed));
    }

    /** Etat de pilotage du compresseur — SUPPRIME (rev du 2026-09-21 :
        le produit n'expose aucun reglage de comp, la table pilote toujours).
        Garde pour compatibilite des tests, renvoie toujours faux. */
    bool isCompCustom() const noexcept { return compCustom.load(); }

    // --- Courbe d'EQ et analyseur (F1.6c, US-04) ----------------------------

    /** Le spectre le plus recent, une valeur en dB par colonne d'affichage. Il
        est calcule par **le thread de messages** (`updateSpectrum`), jamais par
        le thread audio : c'est la contrainte du §7 des risques. */
    const std::vector<float>& spectrumDb() const noexcept { return spectrumAnalyzerEngine.columnsDb(); }

    /** Echantillons abandonnes faute de place dans la capture du spectre. Doit
        rester a 0 : c'est la mesure de « aucun depassement de bloc » (AC3). */
    int spectrumDroppedSamples() const noexcept { return spectrumTapBuffer.droppedSamples(); }

    /** Consomme ce que le thread audio a capte et met a jour les colonnes du
        spectre. A appeler depuis le thread de messages, au rythme de
        l'affichage. */
    bool updateSpectrum (int numColumns, float maxHz)
    {
        return spectrumAnalyzerEngine.update (spectrumTapBuffer, numColumns, maxHz);
    }

    /** Reglages d'EQ courants, lus sur les PARAMETRES (thread de messages).

        C'est ce que la courbe trace. Le DSP les lit a travers son lissage : la
        courbe montre donc la CIBLE, pas l'etat lisse de quelques millisecondes
        plus tot — c'est ce qu'un utilisateur attend d'une courbe d'EQ. */
    odvox::Eq::Settings currentEqSettings() const;

    /** Silence STRICT (F1.13) : le bypass de l'hote route ici (JUCE route vers
        cette methode quand `getBypassParameter()` est nul, notre cas). Recopie
        canal par canal — la definition du « vrai contournement » de US-02 :
        une difference entree/sortie de moins de −120 dBFS, y compris tous
        modules a fond.

        L'editeur lit `getLatencySamples()`, que cette methode ne change jamais
        (zero echantillon : l'hote n'a rien a compenser sur un chemin d'ecoute
        sans etage). Le bypass n'a pas d'etat : il n'y a rien a reinitialiser.
        La version double n'est pas surchargee : la chaine ne supporte que le
        float, l'hote converait deja pour `processBlock`. */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** La latence du chemin de traitement, en echantillons : ZERO par
        construction — aucun moteur de la chaine ne regarde devant lui (revue
        des modules du 2026-09-22 : gate/filtres/reverb/delay/lissages
        travaillent sur le present). Reportee a l'hote une fois en preparation,
        puis JAMAIS touchée : un second appel ecraserait la compensation deja
        installee par l'hote.

        C'est la moitie lecture du contrat US-12 : le test C++ mesure la
        latence par correlation (impulsion entree contre sortie) et l'exige
        egale a la valeur reportee (±1 echantillon), ce qui verifie les deux
        cotes d'un seul coup. AC2 (affichage smp + ms) est tenu par l'editeur,
        qui affiche `getLatencySamples()` converti. */
    float eqTrackingHz() const noexcept { return eq.trackingHz(); }

    /** Frequence accrochee d'une bande d'EQ (lecture de controle des tests). */
    float eqTrackedFrequency (int band) const noexcept { return eq.trackedFrequencyHz (band); }

    /** Temps de delay reellement utilise, en ms (lecture de controle des
        tests : le mapping divisions x tempo se mesure la-dessus). */
    float delayCurrentMs() const noexcept { return delay.currentDelayMs(); }

    /** FORCE le tempo vu par le delay (tests seulement) : les tests unitaires
        n'ont pas de playhead, c'est le seul chemin pour verifier le mapping
        divisions x BPM sans simuler un hote. */
    void setTestBpm (float bpm) noexcept { lastKnownBpm.store (bpm, std::memory_order_relaxed); }

    /** Lecture/ecriture de la Settings du delay (tests seulement). Depuis la
        rev. suppression du mode Avance, le feedback n'est plus un parametre :
        c'est le seul chemin pour exercer un feedback autre que la constante de
        conception sans re-introduire un reglage public. */
    odvox::Delay::Settings delaySettingsForTests() const { return delaySettings; }
    void setDelaySettingsForTests (const odvox::Delay::Settings& s)
    {
        delaySettings = s;
        delay.setSettings (s);
    }

    /** Note : il n'y a PAS de point d'entree pour injecter une fondamentale dans
        l'EQ. Les tests du suivi lui presentent un signal harmonique et laissent
        son detecteur faire son travail — un chemin d'injection permettrait a un
        test de passer alors que le cablage reel est rompu. */

    /** Le detecteur de fondamentale du produit. C'est le composant PARTAGE, et
        il n'en existe qu'un : il vit dans le module EQ. */
    odvox::PitchDetector& pitchDetector() noexcept { return eq.detector(); }

private:
    void applyCalibrationGain (float deltaDb);
    void refreshDerivedSettings();

    // Les gestes (souris, clavier) pilotent la regle du macro : un geste sur un
    // parametre avance passe en `Custom`, la FIN d'un geste sur `comp_amount`
    // y revient. Les changements de valeur sans geste (automation) ne changent
    // jamais l'etat — sinon une automation du curseur ferait sortir du mode
    // `Custom`, ce que AC5 interdit.
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    juce::AudioProcessorValueTreeState apvts;

    odvox::Smoothing smoothing;   // parametres de module, lus une fois par bloc
    odvox::Smoothing gains;       // gain d'entree et de sortie, lus par echantillon

    std::vector<float> targets;
    float gainTargets[2] { 0.0f, 0.0f };

    odvox::Gate       gate;
    odvox::LowCut     lowCut;
    odvox::Eq         eq;
    odvox::Compressor compressor;
    odvox::DeEsser    deEsser;
    odvox::Drive      drive;
    odvox::Image      image;
    odvox::Delay      delay;

    /** La Settings COURANTE du delay : `refreshDerivedSettings` la met a jour
        champ par champ (parametres + tempo), en PRESERVANT les champs de
        conception (feedback, filtre) qu'elle ne possede pas — et que les tests
        peuvent modifier via `setDelaySettingsForTests`. */
    odvox::Delay::Settings delaySettings;
    odvox::Reverb     reverb;
    odvox::DcBlocker  dcBlocker;   // Sortie (F1.13) : `output_dc_filter`
    odvox::Calibrator calibrator;

    odvox::SpectrumTap      spectrumTapBuffer;        // alimente par le thread audio
    odvox::SpectrumAnalyzer spectrumAnalyzerEngine;   // calcule par le thread de messages

    // Etat du pilotage du compresseur, partage entre le thread de messages
    // (gestes) et le thread audio. `compCustom` est l'etat lui-meme.
    std::atomic<bool> compCustom { false };
    std::atomic<int>  lastGestureIndex { -1 };

    // Dernier tempo publie par le playhead de l'hote (thread audio). Amorce
    // sur 120 BPM : le tempo par defaut des DAW.
    std::atomic<float> lastKnownBpm { 120.0f };

    float lastCalibrateValue = 0.0f;
    float lastGateReduction = 0.0f;
    float lastCompReduction = 0.0f;
    float lastDeEssReduction = 0.0f;

    // Taps RMS des vumetres IN/OUT (refonte UI, cartes V6) : ecrits par le
    // thread audio, lus par l'editeur. Atomiques : la lecture du thread de
    // messages n'a pas a s'arranger avec une ecriture en cours.
    std::atomic<float> lastInputRms { 0.0f };
    std::atomic<float> lastOutputRms { 0.0f };

    int inputGainIndex = -1;
    int outputGainIndex = -1;
    int outputDcIndex = -1;
    int calibrateIndex = -1;
    int gateAmountIndex = -1;
    int lowCutAmountIndex = -1;

    int compAmountIndex = -1;

    int deessAmountIndex = -1;

    int driveAmountIndex = -1;
    int doublerAmountIndex = -1;
    int widthAmountIndex = -1;

    int delayAmountIndex = -1;
    int delayTimeIndex = -1;
    int delaySyncIndex = -1;
    int delayTimeMsIndex = -1;
    int delayDuckingIndex = -1;
    int hqModeIndex = -1;

    // Enables de groupe (amendement UI du 2026-09-29) : la LED de bande est
    // l'interrupteur du groupe, l'etat vit dans un parametre (survit aux
    // presets, a l'A/B et a la session).
    int fxOnIndex = -1;
    int delayOnIndex = -1;
    int reverbOnIndex = -1;

    int reverbGainIndices[odvox::Reverb::kNumEngines] = { -1, -1, -1, -1 };

    // L'EQ a quatre bandes de meme forme : UN parametre par bande (le gain,
    // rev du 2026-09-22 — freq/Q/type sont des constantes de conception).
    int eqOnIndex = -1;
    int eqGainIndex[odvox::Eq::kNumBands] {};

    odvox::Snapshots snapshots;
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ODVoxAudioProcessor)
};
