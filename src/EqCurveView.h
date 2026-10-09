#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Eq.h"
#include "EqCurve.h"
#include "PluginProcessor.h"

/**
    La courbe d'EQ interactive et son analyseur de spectre (lot F1.6c, US-04).

    La **geometrie** et les **gestes** ne sont pas ici : ils vivent dans
    `EqCurveModel` et dans `odvox::eqcurve`, sans dependance a l'interface, donc
    verifiables par les tests unitaires. Ce composant ne fait que deux choses :
    dessiner (la courbe, le spectre, les poignees) et transmettre les gestes.

    Ce qu'il dessine, de l'arriere vers l'avant :
    1. la **grille** (frequences de repere, +12 / +6 / 0 / −6 / −12 dB) ;
    2. le **spectre**, rempli, lu sur l'analyseur du processeur — dont la FFT est
       calculee par le thread de messages, jamais par le thread audio ;
    3. la **courbe de reponse**, qui n'est pas un modele : elle vient de
       `Eq::responseDb`, donc des MEMES coefficients que les filtres qui
       traitent le son (AC2 de US-04) ;
    4. les **cinq poignees**, une par bande, saisissables au glisse-depose.

    Gestes : glisser-deposer = frequence et gain · molette = largeur de bande
    (Q) · clic droit = type de bande · double-clic = retour aux defauts du
    catalogue pour la bande visee.
*/
class EqCurveView final : public juce::Component,
                          private juce::Timer
{
public:
    explicit EqCurveView (ODVoxAudioProcessor&);
    ~EqCurveView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Cadence de rafraichissement de l'analyseur. 30 images/s depasse les
        20 exigees par AC3 de US-04, et c'est la cadence a laquelle la donnee
        neuve arrive au mieux (23 maj/s a 48 kHz). */
    static constexpr int kRefreshHz = 30;

    /** Bornes de l'echelle du spectre, en dBFS. */
    static constexpr float kSpectrumMinDb = odvox::SpectrumAnalyzer::kMinDb;
    static constexpr float kSpectrumMaxDb = odvox::SpectrumAnalyzer::kMaxDb;

    /** Position verticale, en pixels, d'un niveau du spectre. */
    float yForDb (float db) const noexcept;

    /** Ouvre le menu des types de bande — c'est le quatrieme des quatre
        parametres d'AC1 de US-04, et il passe par un menu parce qu'un type n'est
        pas une quantite : il ne se glisse pas. Public parce que le test exerce
        le chemin complet « choisir un type ». */
    void showTypeMenu (int band, juce::Point<int> where);

private:
    void timerCallback() override;

    ODVoxAudioProcessor& processor;
    odvox::EqCurveModel model;

    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurveView)
};
