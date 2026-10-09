#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "EqCurve.h"
#include "EqCurveView.h"
#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <set>
#include <type_traits>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    /** L'interface est un objet graphique : elle ne se construit pas sans un
        gestionnaire de messages.

        L'initialisation vit dans `EditorTestMain.cpp`, demarree et arretee
        explicitement dans `main` — la variante statique plante avant `main`, et
        la variante par fonction laisse le processus suspendu. La presence d'une
        souris de bureau est le temoin que l'initialisation a eu lieu. */
    void ensureGuiIsInitialised()
    {
        jassert (juce::Desktop::getInstance().getNumMouseSources() > 0);
    }

    /** Un evenement souris fabrique a la main, pour envoyer de VRAIS gestes au
        composant. On exerce ainsi le chemin reel `mouseDown` / `mouseDrag` /
        `mouseUp`, et non un raccourci de test : c'est ce qu'AC1 de US-04 demande
        (« verifie par le geste »). */
    juce::MouseEvent makeEvent (juce::Component& target, juce::Point<float> position,
                                bool rightButton = false)
    {
        const auto mods = rightButton ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                                      : juce::ModifierKeys();
        const auto now = juce::Time::getCurrentTime();

        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 position, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, now, position, now, 1, false);
    }

    /** Niveaux de gris distincts d'une image : un dessin qui ne change rien
        laisserait une seule valeur, un dessin qui bouge en change plusieurs. */
    int distinctLevels (const juce::Image& image)
    {
        std::set<int> levels;

        for (int y = 0; y < image.getHeight(); y += 3)
            for (int x = 0; x < image.getWidth(); x += 3)
            {
                const auto colour = image.getPixelAt (x, y);
                levels.insert ((int) (colour.getBrightness() * 255.0f));
            }

        return (int) levels.size();
    }

    /** Par defaut, peint SANS tenir compte des alpha de composants (les tests
        existants). `respectAlpha` = le chemin REEL d'un hote : l'alpha pose
        par applyBandDim (bande OFF) est alors visible dans l'image. */
    juce::Image render (juce::Component& component, bool respectAlpha = false)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, component.getWidth()),
                           juce::jmax (1, component.getHeight()), true);

        juce::Graphics g (image);
        component.paintEntireComponent (g, ! respectAlpha);

        return image;
    }

    /** Difference moyenne entre deux images de meme taille : c'est la mesure de
        « le dessin a change ». */
    float meanDifference (const juce::Image& a, const juce::Image& b)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return 255.0f;

        double total = 0.0;
        int count = 0;

        for (int y = 0; y < a.getHeight(); y += 2)
            for (int x = 0; x < a.getWidth(); x += 2)
            {
                const auto ca = a.getPixelAt (x, y);
                const auto cb = b.getPixelAt (x, y);

                total += std::abs ((double) ca.getBrightness() - (double) cb.getBrightness());
                ++count;
            }

        return (float) (total / juce::jmax (1, count) * 255.0);
    }

    float actualOf (ODVoxAudioProcessor& p, const char* id)
    {
        auto* value = p.state().getRawParameterValue (id);

        return value != nullptr ? value->load() : 0.0f;
    }

    std::vector<float> makeSine (float freq, float amplitude, double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        std::vector<float> v ((size_t) n);

        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * (double) freq * (double) i / kSampleRate));

        return v;
    }
}

class EditorTests : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Interface (F1.6c, AC1 et AC3 de US-04)") {}

    void runTest() override
    {
        beginTest ("L'editeur se construit, se dispose et se dessine hors ecran");
        testEditorDrawsOffscreen();

        beginTest ("AC1 : un VRAI glisser-depose sur la courbe ecrit les parametres");
        testRealDragOnTheCurve();

        beginTest ("La courbe se redessine quand le reglage change");
        testTheCurveRedraws();

        beginTest ("AC3 : le spectre apparait derriere la courbe");
        testSpectrumIsDrawn();

        beginTest ("La LED de bande est un VRAI interrupteur de groupe");
        testBandLedIsARealSwitch();

        beginTest ("Les caps de division parcourent les 21 divisions du delay");
        testDivisionCapsStepTheParameter();

        beginTest ("Les toggles du heros se redessinent quand leur parametre change");
        testTogglesRepaintOnParameterChange();
    }

private:
    /** LA LED EST L'INTERRUPTEUR (amende 2026-09-29, verification par le geste
        comme AC1) : un VRAI clic sur la LED de la bande TON inverse eq_on,
        assombrit le contenu de la bande, n'ecrit RIEN d'autre (les valeurs des
        curseurs survivent — contrat « bypass de groupe par inertie »), et un
        second clic remet l'etat d'origine. */
    void testBandLedIsARealSwitch()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        ODVoxAudioProcessorEditor editor (p);

        auto* led = editor.bandLed (0);
        expect (led != nullptr, "LED de la bande TON presente");

        if (led == nullptr)
            return;

        auto* eqOn = p.state().getParameter ("eq_on");
        expect (eqOn != nullptr, "eq_on est declare");

        if (eqOn == nullptr)
            return;

        // Les rendus respectent l'alpha (chemin reel d'un hote) : c'est ce qui
        // rend l'assombrissement de la bande visible dans l'image.
        const auto before = render (editor, true);
        expect (led->isOn(), "LED allumee aux defauts (eq_on = 1)");

        // Le VRAI clic : un evenement souris fabrique a la main, envoye a la
        // LED — le chemin reel mouseDown, pas un raccourci de test. L'accesseur
        // de lecture expose const, mais l'editeur possede la LED non-const :
        // le geste a le droit de la muter (le type est privé, on le deduit).
        auto* mutableLed = const_cast<std::add_pointer_t<std::remove_cv_t<std::remove_pointer_t<decltype (led)>>>> (led);
        juce::Component& target = *mutableLed;
        const auto centre = juce::Point<float> ((float) led->getWidth() * 0.5f,
                                                (float) led->getHeight() * 0.5f);
        target.mouseDown (makeEvent (target, centre));

        expect (! led->isOn(), "apres le clic, la LED est eteinte");
        expectEquals (eqOn->getValue(), 0.0f, "eq_on ecrit a 0");
        expect (editor.bandDimAlpha (0) < 0.5f,
                "le contenu de la bande est assombri (off)");

        const auto after = render (editor, true);

        // La difference se juge DANS la bande TON (LED eteinte + contenu
        // assombri) : a l'echelle de l'editeur entier, la moyenne serait
        // diluee par les pixels intacts.
        const auto bandArea = juce::Rectangle<int> (led->getX() - 13, led->getY() - 30,
                                                    398, 69); // cotes d'une bande (essai 2026-10-02 : colonne empilee a droite, une MOITIE de la zone)
        const float diff = meanDifference (before.getClippedImage (bandArea),
                                           after.getClippedImage (bandArea));
        expect (diff > 2.0f,
                "le rendu de la bande a change (LED eteinte + assombrissement), diff = "
                    + juce::String (diff, 2));

        // Second clic : retour a l'etat d'origine.
        target.mouseDown (makeEvent (target, centre));

        expect (led->isOn(), "le second clic rallume la LED");
        expectEquals (eqOn->getValue(), 1.0f, "eq_on revient a 1");

        // Inertie : le toggle n'ecrit QUE le parametre du groupe.
        expectEquals (actualOf (p, "eq_low_db"), 0.0f,
                      "eq_low_db n'a pas bouge (inertie)");
        expectWithinAbsoluteError ((double) actualOf (p, "gate_amount"), 0.0, 0.0001,
                                   "gate_amount (autre bande) n'a pas bouge");
    }

    /** Le trio de division du DELAY (amende 2026-09-29, verification par le
        geste) : un VRAI clic sur le mini-cap precedent remonte d'une division
        (10 -> 9 = 1/4T), un clic sur suivant redescend (9 -> 10 = 1/4). La
        valeur ECRITE dans le parametre est l'INDEX/20 — JUCE AudioParameterChoice
        mappe normalise 0..1 sur les index 0..20. C'est le chemin reel des
        onClick (Cap::mouseUp), pas un raccourci de test. */
    void testDivisionCapsStepTheParameter()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        ODVoxAudioProcessorEditor editor (p);

        auto* param = p.state().getParameter ("delay_time");
        expect (param != nullptr, "delay_time est declare");
        if (param == nullptr)
            return;

        // Par defaut, la division est 1/4 (index 10). L'editeur est pose au
        // layout pour que les caps existent aux cotes maquette.
        editor.setBounds (0, 0, editor.getWidth(), editor.getHeight());
        editor.resized();
        const float oneQuarter = param->getValue();
        expectWithinAbsoluteError ((double) oneQuarter, 0.5, 0.001,
                                   "defaut 1/4 = index 10 sur 0..20");        // Les mini-caps sont prives : le geste passe par les declencheurs
        // publics de l'editeur — le MEME chemin que le onClick des caps.
        const int before = (int) std::lround (oneQuarter * 20.0f);
        expectEquals (before, 10, "la division par defaut est 1/4 (index 10)");

        // La vitre affiche le libelle : une ComboBox JUCE sans item ne rend
        // aucun texte (l'utilisateur voyait « no choice »).
        expectEquals (editor.divisionItemCount(), odvox::Delay::kNumDivisions,
                      "la vitre contient les 21 libelles du catalogue");
        expect (editor.divisionLabelText().contains ("1/4"),
                "la vitre affiche la division courante, pas « no choice »");

        editor.clickedDivPrev();
        expectEquals ((int) std::lround (param->getValue() * 20.0f), before - 1,
                      "le cap precedent remonte d'une division (1/4 -> 1/4T)");

        editor.clickedDivNext();
        editor.clickedDivNext();
        expectEquals ((int) std::lround (param->getValue() * 20.0f), before + 1,
                      "le cap suivant redescend d'une division (1/4T -> 1/4)");

        // Les bornes : colle aux extremes, chaque cap clampe sans boucler.
        param->setValueNotifyingHost (0.0f);   // 1/32T (index 0)
        editor.clickedDivPrev();
        expectWithinAbsoluteError ((double) param->getValue(), 0.0, 0.001,
                                   "a 1/32T, precedent reste a 1/32T (borne)");

        param->setValueNotifyingHost (1.0f);   // 2/1D (index 20)
        editor.clickedDivNext();
        expectWithinAbsoluteError ((double) param->getValue(), 1.0, 0.001,
                                   "a 2/1D, suivant reste a 2/1D (borne)");
    }

    // =====================================================================
    // LE VISUEL SUIT LE PARAMETRE (retour utilisateur du 2026-09-30 : les
    // toggles ne bougeaient pas au clic — le parametre changeait, RIEN ne
    // repeignait). Le piege : paintEntireComponent appelle paint()
    // directement, un simple rendu avant/apres passerait MEME sans le
    // correctif. On simule donc la vraie victime : le cache de rendu d'un
    // hote, qui ne se re-rend que sur invalidate — espion pose sur le
    // composant, comme le fait le peer de JUCE (StandardCachedComponentImage).
    // =====================================================================
    struct RepaintSpy : public juce::CachedComponentImage
    {
        int invalidations = 0;

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::pink); }
        bool invalidateAll() override           { ++invalidations; return true; }
        bool invalidate (const juce::Rectangle<int>&) override
        {
            ++invalidations;
            return true;
        }
        void releaseResources() override {}
    };

    void testTogglesRepaintOnParameterChange()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        ODVoxAudioProcessorEditor editor (p);

        for (const char* id : { "lowcut_amount", "output_dc_filter", "delay_sync" })
        {
            auto* toggle = editor.toggleById (id);
            expect (toggle != nullptr, juce::String (id) + " est porte par l'interface");

            if (toggle == nullptr)
                continue;

            auto* param = p.state().getParameter (id);
            expect (param != nullptr, juce::String (id) + " est declare");

            if (param == nullptr)
                continue;

            // Le type du toggle est prive : le cast passe par decltype, comme
            // le test de la LED.
            auto* mutableToggle = const_cast<std::add_pointer_t<std::remove_cv_t<std::remove_pointer_t<decltype (toggle)>>>> (toggle);

            // Le cache de rendu, pose sur le composant comme le fait le peer :
            // il ne se re-rend QUE si le composant s'invalide (repaint()).
            // POSSESSION : setCachedComponentImage ADOPTÉ l'objet (doc JUCE) —
            // il vit donc sur le tas et meurt avec le composant. Le détacher
            // d'une pile (setCachedComponentImage (nullptr)) ferait un delete
            // d'un objet statique : le crash silencieux du premier essai.
            auto* spy = new RepaintSpy();
            mutableToggle->setCachedComponentImage (spy);

            const int before = spy->invalidations;

            // Le chemin REEL du clic, sans son unique saut de message : le
            // clic finit dans Button::setToggleState (sendNotification) —
            // l'attachment ecrit le parametre, le listener du correctif
            // commande l'invalidation, le tout de façon synchrone, exactement
            // comme l'hot le vivra entre deux images.
            mutableToggle->button.setToggleState (! mutableToggle->button.getToggleState(),
                                                  juce::sendNotification);

            expect (spy->invalidations > before,
                    juce::String (id) + " : le clic invalide le rendu (invalidations "
                        + juce::String (before) + " -> "
                        + juce::String (spy->invalidations) + ")");
            expect (param->getValue() != param->getDefaultValue(),
                    juce::String (id) + " : le clic a ecrit le parametre");

            // L'etat change AUSSI depuis l'hote (preset, A/B, automation) :
            // l'invalidation doit suivre pareil.
            const int afterClick = spy->invalidations;
            param->setValueNotifyingHost (param->getDefaultValue());

            expect (spy->invalidations > afterClick,
                    juce::String (id) + " : le changement par le parametre invalide aussi");
        }
    }

    /** L'editeur complet : c'est le composant qu'un hote instancie. S'il y avait
        une assertion de construction (un identifiant de parametre faux, une
        disposition impossible), ce test la declencherait. */
    void testEditorDrawsOffscreen()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        ODVoxAudioProcessorEditor editor (p);

        // La fenetre fait exactement la taille du corps de pedale transpose
        // (maquette tools/mockup_cards.html : 980x516).
        expectEquals (editor.getWidth(), ODVoxAudioProcessorEditor::kDefaultWidth,
                      "largeur par defaut");
        expectEquals (editor.getHeight(), ODVoxAudioProcessorEditor::kDefaultHeight,
                      "hauteur par defaut (corps de pedale, maquette V6)");

        // --- Couverture Essential, le test qui doit VRAIMENT echouer ---------
        // L'assertion du constructeur (jassert) ne bloque pas sans debogueur :
        // c'est ici que la couverture des 29 Essential est sanctionnee. Tout
        // identifiant de catalogue absent de l'interface (ou place deux fois)
        // fait echouer ce test — le trou hq_mode du lot precedent serait
        // attrape ici.
        const auto uncovered = editor.uncoveredEssentialIds();

        expect (uncovered.isEmpty(),
                "tout parametre Essential est porte par l'interface (manquants : "
                    + uncovered.joinIntoString (", ") + ")");

        // Les quatre LED-interrupteurs de bande existent, sont des interrupteurs
        // vivants, et posent DANS la colonne des bandes empilees a droite
        // (x>=491, y>=138, essai 2026-10-02 ; l'ancienne grille 2x2 « cartes
        // V6 » les posait a x>=85, y>=316).
        {
            for (int i = 0; i < 4; ++i)
            {
                const auto* led = editor.bandLed (i);
                expect (led != nullptr, "LED de bande " + juce::String (i) + " presente");

                if (led != nullptr)
                {
                    expect (led->getX() >= 491 && led->getY() >= 138,
                            "LED posee dans la colonne des bandes (essai 2026-10-02)");
                    expect (led->isOn(), "LED allumee aux defauts du catalogue");
                }
            }
        }

        // Les niveaux IN/OUT existent, 450 px de large, ancres aux coins bas.
        {
            const auto* inMeter  = editor.levelMeter (0);
            const auto* outMeter = editor.levelMeter (1);

            expect (inMeter != nullptr && outMeter != nullptr,
                    "deux instruments de niveau IN/OUT");

            if (inMeter != nullptr)
            {
                expectWithinAbsoluteError ((double) inMeter->getX(), 6.0, 1.0,
                                           "IN ancre au bord gauche");
                expect (inMeter->getWidth() >= 400, "instrument IN large");
            }

            if (outMeter != nullptr)
            {
                expectWithinAbsoluteError ((double) (outMeter->getRight()), 974.0, 1.0,
                                           "OUT ancre au bord droit");
                expect (outMeter->getWidth() >= 400, "instrument OUT large");
            }
        }

        const auto image = render (editor);

        expect (distinctLevels (image) > 8,
                "le dessin produit une vraie image (" + juce::String (distinctLevels (image))
                    + " niveaux distincts)");

        // La courbe vit DESORMAIS dans le heros (LCD a l'echelle 0,44, pose a
        // (379,118), maquette cartes V6) : la bande exacte de l'ecran est
        // copiee pour inspection — le contenu doit y etre dessine.
        // NB essai 2026-10-02 : le LCD est MASQUE dans l'editeur
        // (kLcdEnabled = false, disposition « panneau unique »), cette
        // copie sert donc d'outil de diagnostic, pas de verif — l'ecran
        // lui-meme est verifie via EqCurveView (testRealDragOnTheCurve).
        constexpr int kLcdX = 379;
        constexpr int kLcdY = 118;
        constexpr int kLcdW = 331;
        constexpr int kLcdH = 184;

        juce::Image lcdBand (juce::Image::ARGB, kLcdW, kLcdH, true);
        juce::Graphics bandGraphics (lcdBand);
        bandGraphics.drawImage (image,
                                kLcdX, kLcdY, kLcdW, kLcdH,
                                0, 0, kLcdW, kLcdH);
    }

    // =====================================================================
    // AC1 de US-04 par le GESTE : on envoie au composant des evenements souris
    // fabriques, exactement comme le ferait un utilisateur. Le modele est deja
    // verifie separement ; ce test verifie que l'interface le BRANCHE — c'est la
    // seule chose qu'un test du modele ne peut pas voir.
    // =====================================================================
    void testRealDragOnTheCurve()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        EqCurveView view (p);
        view.setSize (900, 160);

        odvox::EqCurveModel reference (p, 900, 160);

        // La vue applique la geometrie de l'ecran LCD (asset embarque) a son
        // montage : la reference de test doit lire la MEME geometrie, sinon on
        // comparerait deux echelles differentes.
        reference.setViewport (odvox::lcd::axisViewport());

        const juce::String gainId ("eq_mid_db");

        const float gainBefore = actualOf (p, gainId.toRawUTF8());

        // La poignee de la bande Mid, telle que le modele la place : le composant
        // et le modele lisent la MEME geometrie.
        const auto handle = reference.handlePosition (1);
        const juce::Point<float> target { 0.45f * 900.0f, 0.30f * 160.0f };

        // rev. suppression du mode Avance : plus de molette-Q (Q fige par bande)
        // et le drag vertical ne fait plus bouger la frequence (bandes
        // pitch-suiveuses sur ancres figees) — le geste ne porte que le gain.

        // Le glisser-deposer, du centre de la poignee vers la cible.
        view.mouseDown (makeEvent (view, { handle.x, handle.y }));
        view.mouseDrag (makeEvent (view, target));
        view.mouseUp (makeEvent (view, target));

        const float gainAfter = actualOf (p, gainId.toRawUTF8());

        expect (std::abs (gainAfter - gainBefore) > 1.0f,
                "le glisser-depose a change le gain du Mid (" + juce::String (gainBefore, 2)
                    + " -> " + juce::String (gainAfter, 2) + " dB)");

        // Et le double-clic rend la bande a ses defauts, poignee comprise.
        const auto moved = reference.handlePosition (1);
        view.mouseDoubleClick (makeEvent (view, { moved.x, moved.y }));

        expectWithinAbsoluteError (actualOf (p, "eq_mid_db"), 0.0f, 0.01f,
                                   "le double-clic a ramene le gain du Mid a 0 dB");
    }

    void testTheCurveRedraws()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        EqCurveView view (p);
        view.setSize (900, 160);

        const auto before = render (view);

        // Un reglage par les PARAMETRES : c'est le meme chemin qu'un geste, et le
        // dessin doit le suivre.
        if (auto* param = p.state().getParameter ("eq_mid_db"))
            param->setValueNotifyingHost (odvox::params::actualToNormalised ("eq_mid_db", 12.0f));

        const auto after = render (view);
        const float difference = meanDifference (before, after);

        expect (difference > 0.5f,
                "la courbe se redessine quand le reglage change (difference moyenne "
                    + juce::String (difference, 2) + " niveaux)");
    }

    // =====================================================================
    // AC3 de US-04 : le spectre doit etre VISIBLE derriere la courbe. Un
    // analyseur juste mais jamais dessine ne satisferait pas le critere — et
    // c'est exactement ce qu'un test de calcul ne peut pas voir.
    // =====================================================================
    void testSpectrumIsDrawn()
    {
        ensureGuiIsInitialised();

        ODVoxAudioProcessor p;
        p.prepareToPlay (kSampleRate, 512);

        EqCurveView view (p);
        view.setSize (900, 160);

        // On pousse un signal dans la chaine, puis on alimente l'analyseur comme
        // le fait son timer.
        const auto signal = makeSine (1000.0f, 0.3f, 3.0);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (size_t position = 0; position + 512 <= signal.size(); position += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, signal[position + (size_t) i]);
                buffer.setSample (1, i, signal[position + (size_t) i]);
            }

            p.processBlock (buffer, midi);
            p.updateSpectrum (view.getWidth(), odvox::eqcurve::kMaxHz);
        }

        expect (! p.spectrumDb().empty(), "le spectre est publie apres traitement");

        // La bande haute de la fenetre est celle ou le spectre a le plus de
        // chose a dire : on compare le dessin avec le spectre disponible.
        const auto drawn = render (view);
        const int levels = distinctLevels (drawn);

        expect (levels > 6,
                "le spectre est dessine sous la courbe (" + juce::String (levels)
                    + " niveaux distincts dans l'image)");

        // Et le spectre est bien celui de la SONDE : son maximum tombe dans la
        // bande de 1 kHz, sur les donnees que le processeur publie.
        const auto& columns = p.spectrumDb();
        const auto peak = std::max_element (columns.begin(), columns.end());
        const int index = (int) std::distance (columns.begin(), peak);
        const float peakHz = odvox::SpectrumAnalyzer::columnFrequency (index, (int) columns.size(),
                                                                      odvox::eqcurve::kMaxHz);

        expect (std::abs (peakHz - 1000.0f) / 1000.0f < 0.05f,
                "le spectre dessine montre la sonde a 1 kHz (pic a "
                    + juce::String (peakHz, 1) + " Hz)");
    }
};

static EditorTests editorTests;
