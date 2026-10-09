#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "Smoothing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    /** Le contrat fige du 2026-09-18 (PRD.md §3.4). Ecrit a la main, et non
        derive du catalogue : c'est tout l'interet du test. Si un identifiant
        change, ce test doit echouer, parce qu'un renommage casse les presets et
        l'automation deja enregistres.

        Rev. F2.2 (2026-09-22) : `hq_mode` s'ajoute aux 25 (US-09, rev. du PRD :
        un ajout APRES les figes, sur le modele de l'amendement Gate). Rev.
        refonte UI (2026-09-29) : les trois enables de groupe de la LED de
        bande s'ajoutent aux 26 (maquette tools/mockup_cards.html). */
    const char* const kFrozenEssentialIds[] =
    {
        "input_gain_db", "input_calibrate", "gate_amount", "lowcut_amount",
        "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db", "eq_on",
        "comp_amount", "deess_amount", "drive_amount", "hq_mode",
        "doubler_amount", "width_amount",
        "delay_amount", "delay_time", "delay_sync", "delay_time_ms", "delay_ducking",
        "reverb_short_pct", "reverb_small_pct", "reverb_big_pct", "reverb_lush_pct",
        "fx_on", "delay_on", "reverb_on",
        "output_gain_db", "output_dc_filter"
    };

    const char* const kMicTags[] = { "SM7b", "SM58", "Condenseur", "NT1" };

    /** Cle sous laquelle un hote VST3 voit un parametre. **Mesure** le 2026-09-18
        sur l'hote de verification (§3.4, pedalboard / JUCE) : ce n'est PAS
        l'identifiant du catalogue, mais un slug du **nom affiche**, suivi de
        l'unite quand elle en a une (`_db`, `_ms`, `_hz`) et de rien pour un
        pourcentage, un choix, un booleen ou une action.

        Aucune specification VST3 ne l'impose. Ce qui est etabli, c'est que
        l'hote voit ce nom-la, donc **deux parametres qui partagent un nom
        affiche se recouvrent** et l'un des deux devient inatteignable : c'est
        exactement comme cela que le plug dont on s'inspire a perdu un de ses
        32 parametres. D'ou le test ci-dessous, sur l'unicite des noms.

        Attention a ce que ce test prouve : il compare la regle ci-dessus a une
        liste figee a la main, donc il detecte un **renommage**, pas une erreur de
        regle. C'est `tools/verify_plugin.py` qui confronte la regle aux cles
        reellement exposees — et c'est lui qui a corrige le suffixe `_hz`, absent
        de la premiere version de cette fonction. */
    juce::String hostVisibleKey (const odvox::params::Info& info)
    {
        juce::String key;

        for (auto c : juce::String (info.name))
        {
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                key << juce::CharacterFunctions::toLowerCase (c);
            else
                key << '_';
        }

        switch (info.unit)
        {
            case odvox::params::Unit::db: key << "_db"; break;
            case odvox::params::Unit::ms: key << "_ms"; break;
            case odvox::params::Unit::hz: key << "_hz"; break;
            // Le rapport a un symbole d'unite (« x ») : l'hote l'ajoute aussi
            // — releve reellement sur l'hote de verification (`ratio_x`).
            case odvox::params::Unit::ratio: key << "_x"; break;
            // Mesure du 2026-09-19 sur le .vst3 : l'hote suffixe aussi les
            // cents (`Doubler Detune` -> `doubler_detune_cents`). Les pourcents,
            // booleens, choix et actions restent nus (`drive_mix`, `comp`).
            case odvox::params::Unit::cents: key << "_cents"; break;
            default: break;
        }

        return key;
    }

    /** Les cles observees chez l'hote, relevees a la main et dans l'ordre du
        catalogue. Ecrites en dur : c'est ce qui rend le test capable de detecter
        un renommage. */
    const char* const kHostVisibleKeys[] =
    {
        "input_gain_db", "calibrate", "gate", "low_cut",
        "low_db", "mid_db", "high_db", "air_db", "eq_on",
        // rev. suppression du mode Avance : les 12 reglages freq/Q/type des
        // bandes ne sont plus exposes — ancres figees, bandes pitch-suiveuses.
        "comp",
        "de_ess",
        "drive", "hq_mode",   // F2.2 : interrupteur du mode haute qualite
        "doubler", "width",
        "delay", "delay_time", "delay_sync", "free_time_ms", "ducking",
        "short", "small", "big", "lush",
        "fx_on", "delay_on", "reverb_on",   // refonte UI : enables de groupe
        "output_gain_db", "dc_filter"
    };

    /** Remplit les parametres figes (29 `Essential` depuis l'amendement UI du
        2026-09-29) avec des valeurs normalisees distinctes, dans
        0,05..0,95 : hors de portee de 0,02 et de 0,98, ce qui permet aux tests
        de distinguer « a change » de « n'a pas bouge » sans ambiguite. */
    std::map<juce::String, float> distinctValues (float seed)
    {
        std::map<juce::String, float> values;
        const auto all = odvox::params::declared();

        for (size_t i = 0; i < all.size(); ++i)
            values[all[i].id] = 0.05f + 0.9f * std::fmod (seed + 0.037f * (float) i, 1.0f);

        return values;
    }

    bool isSnakeCase (const juce::String& s)
    {
        if (s.isEmpty() || s.startsWith ("_") || s.endsWith ("_") || s.contains ("__"))
            return false;

        for (auto c : s)
            if (! (juce::CharacterFunctions::isLowerCase (c)
                   || juce::CharacterFunctions::isDigit (c)
                   || c == '_'))
                return false;

        return true;
    }

    bool hasMicTag (const odvox::Preset& p)
    {
        for (const auto& tag : p.tags)
            for (const auto* mic : kMicTags)
                if (tag == mic)
                    return true;

        return false;
    }

    class ParameterBusTests final : public juce::UnitTest
    {
    public:
        ParameterBusTests() : juce::UnitTest ("Bus de parametres, presets et A/B (F1.2)") {}

        void runTest() override
        {
            testFrozenCatalogue();
            testHostVisibleKeysAreFrozenAndUnique();
            testSmoothingSatisfiesMinimumRamp();
            testFactoryPresets();
            testPresetJsonRoundTrip();
            testUnknownParameterIsTolerated();
            testUnsupportedSchemaVersionIsRefused();
            testMalformedJsonIsRefused();
            testUnreadableFileIsRefused();
            testSnapshots();
            testProcessorPresetIsolation();
            testProcessorPresetApplyIsFast();
            testProcessorAbSnapshots();
            testAbSwitchProducesNoClick();
            testPresetFileRoundTrip();
            testUnwritableDestinationIsRefused();
        }

    private:
        // --- Catalogue ---------------------------------------------------------

        void testFrozenCatalogue()
        {
            beginTest ("Le catalogue declare les identifiants figes");

            const auto all = odvox::params::declared();

            juce::StringArray ids, essentials;

            for (const auto& info : all)
            {
                ids.add (info.id);

                if (info.essential)
                    essentials.add (info.id);
            }

            // Le compteur vient du TABLEAU, pas d'un nombre magique : une liste
            // figee qui traigne derriere le catalogue echoue proprement ici au
            // lieu de lire hors bornes (segfault du 2026-09-20, liste a 49 clés
            // lue avec les 54 parametres du catalogue).
            const int numFrozen = (int) (sizeof (kFrozenEssentialIds) / sizeof (kFrozenEssentialIds[0]));
            const juce::StringArray expected (kFrozenEssentialIds, numFrozen);
            expect (essentials == expected,
                    "les identifiants `Essential` doivent correspondre au contrat fige, dans l'ordre : "
                        + essentials.joinIntoString (", "));
            expectEquals (essentials.size(), numFrozen, "nombre de parametres figes");

            for (const auto& info : all)
            {
                expect (isSnakeCase (info.id), juce::String (info.id) + " doit etre en snake_case");
                expect (info.min <= info.max, juce::String (info.id) + " plage incoherente");
                expect (info.defaultValue >= info.min && info.defaultValue <= info.max,
                        juce::String (info.id) + " defaut hors plage");
            }

            auto uniqueIds = ids;
            uniqueIds.removeDuplicates (false);
            expectEquals (uniqueIds.size(), (int) all.size(), "les identifiants doivent etre uniques");

            // Seul un parametre d'action n'est pas automatisable.
            for (const auto& info : all)
                if (! info.automatable)
                    expectEquals (juce::String (info.unit == odvox::params::Unit::action ? "action" : "?"),
                                  juce::String ("action"),
                                  juce::String (info.id) + " : seul un parametre d'action est non automatisable");

            // Le total augmente module par module (PRD.md §3.4) — il a meme baisse
            // deux fois le 2026-09-19 (module Justesse retire, puis coupe-bas
            // figé et EQ ramene a 4 bandes) — mais jamais les 20 `Essential`.
            expect ((int) all.size() >= 20, "le catalogue doit contenir au moins les 20 figes");
        }

        void testHostVisibleKeysAreFrozenAndUnique()
        {
            beginTest ("Les cles vues par l'hote sont figees et sans recouvrement");

            const auto all = odvox::params::declared();

            juce::StringArray names, keys;

            for (const auto& info : all)
            {
                names.add (info.name);
                keys.add (hostVisibleKey (info));
            }

            auto uniqueNames = names;
            uniqueNames.removeDuplicates (false);
            expectEquals (uniqueNames.size(), (int) all.size(),
                          "deux parametres qui partagent un nom affiche se recouvrent chez "
                          "l'hote et l'un des deux devient inatteignable : noms = "
                              + names.joinIntoString (", "));

            auto uniqueKeys = keys;
            uniqueKeys.removeDuplicates (false);
            expectEquals (uniqueKeys.size(), (int) all.size(),
                          "les cles vues par l'hote doivent etre uniques : "
                              + keys.joinIntoString (", "));

            const int numKeys = (int) (sizeof (kHostVisibleKeys) / sizeof (kHostVisibleKeys[0]));
            const juce::StringArray observed (kHostVisibleKeys, numKeys);
            expect (keys == observed,
                    "renommer un parametre a l'ecran change ce contrat ; releve attendu "
                    "mais absent : " + observed.joinIntoString (", ")
                        + " / releve obtenu : " + keys.joinIntoString (", "));
        }

        void testSmoothingSatisfiesMinimumRamp()
        {
            beginTest ("Le lissage respecte le minimum de 10 ms exige par §3.4");

            odvox::Smoothing smoothing;
            smoothing.prepare (48000.0, 1);

            const float zero = 0.0f;
            smoothing.snapToTargets (&zero, 1);

            const float one = 1.0f;
            smoothing.setTargets (&one, 1);

            int samples = 0;
            while (smoothing.isSmoothing() && samples < 48000)
            {
                smoothing.getNextValue (0);
                ++samples;
            }

            expect (samples >= 480, "le lissage doit durer au moins 10 ms a 48 kHz (480 echantillons)");
            expect (samples <= 1200, "le lissage ne doit pas depasser ~25 ms sinon les controles traînent");
            expectWithinAbsoluteError (smoothing.getCurrent (0), 1.0f, 1.0e-6f,
                                       "la valeur cible doit etre atteinte");
        }

        // --- Presets d'usine ---------------------------------------------------

        void testFactoryPresets()
        {
            beginTest ("Les 20 presets d'usine sont complets et indexes par micro");

            const auto& presets = odvox::factoryPresets();
            expectEquals ((int) presets.size(), 20, "nombre de presets d'usine");

            juce::StringArray names;
            int micIndexed = 0;

            for (const auto& p : presets)
            {
                names.add (p.name);

                expectEquals ((int) p.parameters.size(), (int) odvox::params::declared().size(),
                              p.name + " doit renseigner tous les parametres declares, "
                                       "sinon une comparaison A/B ne serait pas exacte");

                for (const auto& [id, value] : p.parameters)
                {
                    expect (odvox::params::isDeclared (id),
                            p.name + " reference un parametre non declare : " + id);
                    expect (value >= 0.0f && value <= 1.0f,
                            p.name + " : valeur normalisee hors de 0..1 pour " + id);
                }

                if (hasMicTag (p))
                    ++micIndexed;
            }

            expect (micIndexed >= 6, "au moins 6 presets doivent etre indexes par type de micro");
            auto uniqueNames = names;
            uniqueNames.removeDuplicates (false);
            expectEquals (uniqueNames.size(), 20, "les noms de presets doivent etre uniques");
        }

        void testPresetJsonRoundTrip()
        {
            beginTest ("Aller-retour ecriture puis relecture d'un preset : restitution a l'identique");

            for (const auto& original : odvox::factoryPresets())
            {
                const auto json = original.toJsonString();
                const auto result = odvox::loadPresetFromJson (json);

                expect (result.ok, original.name + " doit etre relu sans erreur : " + result.error);
                expect (result.warnings.isEmpty(),
                        original.name + " ne doit produire aucun avertissement");
                expect (result.preset == original,
                        original.name + " doit etre restitue a l'identique");
            }
        }

        void testUnknownParameterIsTolerated()
        {
            beginTest ("Un parametre inconnu est ignore, les autres sont charges");

            const auto& source = odvox::factoryPresets().front();
            auto v = source.toVar();

            auto* obj = v.getDynamicObject();
            auto* paramObj = obj->getProperty ("parameters").getDynamicObject();
            paramObj->setProperty ("parametre_qui_n_existe_pas", 0.42);

            const auto result = odvox::loadPresetFromVar (v);

            expect (result.ok, "le chargement doit reussir malgre l'identifiant inconnu");
            expect (! result.preset.parameters.count ("parametre_qui_n_existe_pas"),
                    "l'identifiant inconnu ne doit pas etre conserve");
            expectEquals ((int) result.preset.parameters.size(),
                          (int) odvox::params::declared().size(),
                          "les autres parametres doivent tous etre charges");
            expect (result.warnings.size() >= 1, "un avertissement doit etre journalise");

            bool warned = false;
            for (const auto& w : result.warnings)
                warned = warned || w.contains ("parametre_qui_n_existe_pas");

            expect (warned, "l'avertissement doit nommer l'identifiant ignore");
        }

        void testUnsupportedSchemaVersionIsRefused()
        {
            beginTest ("Une version de schema inconnue est refusee");

            const auto source = odvox::factoryPresets().front().toVar();

            auto tooRecent = source;
            tooRecent.getDynamicObject()->setProperty ("schemaVersion", odvox::Preset::kSchemaVersion + 1);
            const auto recent = odvox::loadPresetFromVar (tooRecent);
            expect (! recent.ok, "un preset plus recent doit etre refuse");
            expect (recent.error.contains ("trop recent"), "le motif doit indiquer que le preset est trop recent");

            auto missing = source;
            missing.getDynamicObject()->removeProperty ("schemaVersion");
            expect (! odvox::loadPresetFromVar (missing).ok,
                    "un preset sans schemaVersion doit etre refuse");
        }

        void testMalformedJsonIsRefused()
        {
            beginTest ("Un JSON invalide est refuse");

            expect (! odvox::loadPresetFromJson ("{ ceci n'est pas du json").ok,
                    "un JSON illisible doit etre refuse");
            expect (! odvox::loadPresetFromJson ("[1, 2, 3]").ok,
                    "un tableau JSON n'est pas un preset et doit etre refuse");
        }

        void testUnreadableFileIsRefused()
        {
            beginTest ("Un fichier absent est refuse");

            const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("odvox_preset_qui_n_existe_pas.odvoxpreset");

            expect (! missing.existsAsFile(), "le fichier de test ne doit pas exister");
            expect (! odvox::loadPresetFromFile (missing).ok, "un fichier absent doit etre refuse");
        }

        // --- Emplacements A/B --------------------------------------------------

        void testSnapshots()
        {
            beginTest ("Les emplacements A/B capturent, recoivent et se dupliquent");

            odvox::Snapshots snapshots;
            expect (! snapshots.has (0) && ! snapshots.has (1), "les emplacements demarrent vides");

            const std::map<juce::String, float> a { { "comp_amount", 0.25f }, { "width_amount", 0.5f } };
            const std::map<juce::String, float> b { { "comp_amount", 0.90f }, { "width_amount", 0.1f } };

            snapshots.capture (0, a);
            snapshots.capture (1, b);

            expect (snapshots.has (0) && snapshots.has (1), "les deux emplacements doivent etre remplis");
            expect (snapshots.get (0) == a, "A doit restituer exactement ce qui a ete capture");
            expect (snapshots.get (1) == b, "B doit restituer exactement ce qui a ete capture");

            snapshots.copy (1, 0);
            expect (snapshots.get (0) == b, "la duplication doit copier l'emplacement source");

            // Ce qui compte pour AC1 de US-11 : revenir sur un emplacement doit
            // rendre un instantane bit a bit identique a celui capture.
            snapshots.capture (0, a);
            expect (snapshots.get (0) == a, "un aller-retour A -> B -> A doit restituer A a l'identique");
        }

        // --- Processeur : presets et A/B de bout en bout ------------------------

        void testProcessorPresetIsolation()
        {
            beginTest ("Charger un preset ne touche aucun parametre hors de ce preset (AC3 de US-01)");

            ODVoxAudioProcessor processor;
            juce::StringArray warnings;

            processor.applyParameters (distinctValues (0.0f), warnings);
            expect (warnings.isEmpty(), "aucun avertissement attendu sur des identifiants declares");

            const auto before = processor.captureParameters();
            expectEquals ((int) before.size(), (int) odvox::params::declared().size(),
                          "tous les parametres declares doivent etre capturables");

            // Un preset qui ne renseigne qu'un sous-ensemble des parametres declares :
            // c'est le cas que les 20 presets d'usine ne couvrent pas, puisqu'ils sont
            // tous complets.
            odvox::Preset partial;
            partial.name = "Partiel";

            std::map<juce::String, float> subset;
            const auto all = odvox::params::declared();

            for (size_t i = 0; i < all.size(); i += 6)
                subset[all[i].id] = before.at (all[i].id) < 0.5f ? 0.98f : 0.02f;

            partial.parameters = subset;
            expect ((int) subset.size() < (int) all.size(),
                    "le sous-ensemble doit laisser des parametres non cites, sinon le test ne prouve rien");

            juce::StringArray loadWarnings;
            expect (processor.loadPreset (partial, loadWarnings), "le chargement doit reussir");
            expect (loadWarnings.isEmpty(), "un preset sans identifiant inconnu ne doit rien journaliser");

            const auto after = processor.captureParameters();

            for (const auto& [id, value] : before)
            {
                if (subset.count (id) != 0)
                    expect (after.at (id) != value, id + " doit avoir ete applique");
                else
                    expectEquals (after.at (id), value,
                                  id + " ne doit pas avoir ete touche par un preset qui ne le cite pas");
            }
        }

        void testProcessorPresetApplyIsFast()
        {
            beginTest ("Un preset d'usine applique tout l'etat en moins de 100 ms (AC1 de US-01)");

            ODVoxAudioProcessor processor;
            expectEquals (processor.getNumFactoryPresets(), 20, "nombre de presets d'usine");

            double worst = 0.0;
            int slowest = -1;

            for (int i = 0; i < processor.getNumFactoryPresets(); ++i)
            {
                const auto start = std::chrono::steady_clock::now();
                expect (processor.loadFactoryPreset (i),
                        "le preset " + processor.getFactoryPresetName (i) + " doit se charger");
                const auto elapsed = std::chrono::duration<double, std::milli> (
                                         std::chrono::steady_clock::now() - start).count();

                if (elapsed > worst)
                {
                    worst = elapsed;
                    slowest = i;
                }
            }

            expect (worst < 100.0,
                    "le plus lent (" + processor.getFactoryPresetName (slowest) + ") a pris "
                        + juce::String (worst, 2) + " ms, au-dela des 100 ms exigees");
        }

        void testProcessorAbSnapshots()
        {
            beginTest ("A/B : rappeler un emplacement rend l'etat de depart a l'identique (AC1 de US-11)");

            ODVoxAudioProcessor processor;
            juce::StringArray warnings;

            expect (! processor.slotHasContent (0) && ! processor.slotHasContent (1),
                    "les emplacements demarrent vides");

            processor.applyParameters (distinctValues (0.0f), warnings);
            processor.captureSlot (0);
            const auto a = processor.captureParameters();

            processor.applyParameters (distinctValues (0.5f), warnings);
            processor.captureSlot (1);
            const auto b = processor.captureParameters();

            expect (a != b, "A et B doivent differer, sinon le test ne prouve rien");

            processor.recallSlot (0);
            expect (processor.captureParameters() == a, "A doit etre restitue a l'identique");
            expectEquals (processor.getActiveSlot(), 0, "l'emplacement actif doit suivre le rappel");

            processor.recallSlot (1);
            expect (processor.captureParameters() == b, "B doit etre restitue a l'identique");

            processor.recallSlot (0);
            expect (processor.captureParameters() == a,
                    "l'aller-retour A -> B -> A doit rendre A a l'identique, sur tout le catalogue");
        }

        void testAbSwitchProducesNoClick()
        {
            beginTest ("Basculer A/B ne produit aucune discontinuite (AC2 de US-11)");

            constexpr int blockSize = 512;

            ODVoxAudioProcessor processor;
            processor.prepareToPlay (48000.0, blockSize);

            // Le low cut est ON par defaut depuis l'amendement UI du
            // 2026-09-29 : sa phase ferait bouger la sortie d'un signal DC
            // (sonde du test) — hors du sujet, on le coupe. Le filtre DC de
            // sortie (2026-10-09) est pire encore pour cette sonde : il retire
            // le continu, or la sortie EST la courbe de gain ici.
            {
                juce::StringArray warnings;
                std::map<juce::String, float> v;
                v["lowcut_amount"] = 0.0f;
                v["output_dc_filter"] = 0.0f;
                processor.applyParameters (v, warnings);
            }

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer (2, blockSize);

            const auto applyInputGain = [&] (float db)
            {
                std::map<juce::String, float> v;
                v["input_gain_db"]  = odvox::params::actualToNormalised ("input_gain_db", db);
                v["output_gain_db"] = odvox::params::actualToNormalised ("output_gain_db", 0.0f);

                juce::StringArray warnings;
                processor.applyParameters (v, warnings);
            };

            // Signal continu (DC = 1) : la sortie EST la courbe de gain, donc une
            // discontinuite de gain se lit directement dans le signal echantillon
            // par echantillon, sans avoir a l'extraire d'une enveloppe.
            const auto runBlocks = [&] (int numBlocks, std::vector<float>* recorded = nullptr)
            {
                for (int b = 0; b < numBlocks; ++b)
                {
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        std::fill (buffer.getWritePointer (ch),
                                   buffer.getWritePointer (ch) + blockSize, 1.0f);

                    processor.processBlock (buffer, midi);

                    if (recorded != nullptr)
                        recorded->insert (recorded->end(), buffer.getReadPointer (0),
                                          buffer.getReadPointer (0) + blockSize);
                }
            };

            applyInputGain (-24.0f);
            runBlocks (20);
            processor.captureSlot (0);

            applyInputGain (24.0f);
            runBlocks (20);
            processor.captureSlot (1);

            std::vector<float> tail;
            processor.recallSlot (0);   // +24 dB -> -24 dB : 48 dB d'ecart
            runBlocks (6, &tail);

            expectEquals (processor.getActiveSlot(), 0, "l'emplacement actif doit suivre le rappel");

            float worstStep = 0.0f;
            for (size_t i = 1; i < tail.size(); ++i)
                worstStep = juce::jmax (worstStep, std::abs (tail[i] - tail[i - 1]));

            // Reference sans lissage : le saut vaudrait 15,849 - 0,063 = 15,8 sur ce
            // signal.
            expect (worstStep < 0.2f,
                    "le plus grand ecart entre deux echantillons vaut "
                        + juce::String (worstStep, 4)
                        + " ; sans lissage, la meme bascule vaudrait 15,8");

            expectWithinAbsoluteError (juce::Decibels::gainToDecibels (tail.back()),
                                       -24.0f, 0.5f,
                                       "la cible doit etre atteinte en quelques blocs");
        }

        // --- Presets sur disque -------------------------------------------------

        void testPresetFileRoundTrip()
        {
            beginTest ("Un preset ecrit sur disque se relit a l'identique (.odvoxpreset)");

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("odvox_roundtrip.odvoxpreset");
            file.deleteFile();

            const auto& original = odvox::factoryPresets().front();
            expect (original.writeToFile (file), "l'ecriture doit reussir");
            expect (file.existsAsFile(), "le fichier doit exister apres ecriture");

            const auto result = odvox::loadPresetFromFile (file);
            expect (result.ok, "la relecture doit reussir : " + result.error);
            expect (result.preset == original, "le preset relu doit etre identique a celui ecrit");

            file.deleteFile();
        }

        void testUnwritableDestinationIsRefused()
        {
            beginTest ("Un dossier non inscriptible est signale, jamais silencieux");

            const auto blocker = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("odvox_blocker.tmp");
            blocker.deleteFile();
            expect (blocker.replaceWithText ("x"), "le fichier bloquant doit pouvoir etre cree");

            // Un fichier ne peut pas servir de dossier parent : l'ecriture doit echouer
            // et le signaler, pour que l'appelant conserve l'etat audio courant.
            expect (! odvox::factoryPresets().front().writeToFile (
                        blocker.getChildFile ("preset.odvoxpreset")),
                    "l'ecriture dans un dossier impossible doit renvoyer false, pas reussir");

            blocker.deleteFile();
        }
    };

    ParameterBusTests parameterBusTests;
}
