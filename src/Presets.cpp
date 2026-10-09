#include "Presets.h"

#include "Parameters.h"

namespace odvox
{
    namespace
    {
        const juce::StringArray& knownCategories()
        {
            static const juce::StringArray cats { "stream", "lead", "adlib", "fx", "space", "user" };
            return cats;
        }

        /** Un preset d'usine se decrit en VALEURS REELLES : c'est lisible et
            verifiable a l'oeil. La conversion en valeurs normalisees, seul
            format stocke, est faite par `makeFactoryPreset`. */
        struct FactoryEntry { const char* id; float actual; };

        struct FactoryDef
        {
            const char* name;
            const char* category;
            const char* tags;   // "SM7b|rap", ou nullptr
            std::vector<FactoryEntry> values;
        };

        Preset makeFactoryPreset (const FactoryDef& def)
        {
            Preset p;
            p.name     = def.name;
            p.author   = "OD Audio";
            p.category = def.category;
            p.mode     = "essential";

            if (def.tags != nullptr)
                p.tags.addTokens (juce::String (def.tags), "|", {});

            // Tout parametre declare doit figurer dans l'instantane, sinon une
            // comparaison A/B ou un aller-retour de preset ne serait pas exact.
            for (const auto& info : params::declared())
                p.parameters[info.id] = params::actualToNormalised (info.id, info.defaultValue);

            for (const auto& e : def.values)
                p.parameters[juce::String (e.id)] = params::actualToNormalised (e.id, e.actual);

            // Fidelite (rev du 2026-09-21) : le comp est ENGAGE par son seul
            // curseur — le preset n'a plus d'interrupteur a poser.

            // F1.8 rev : le demouillage de plosives est LIE au curseur De-ess
            // dans le module (cap 20 %) — le preset n'a plus a le poser.

            return p;
        }

        // Categories disponibles : stream, lead, adlib, fx, space, user.
        // Reverb : QUATRE curseurs, un par moteur.
        // Repere de calage : « Clean Vocals Reverb » vaut
        // 0,181/0,200/0,152/0.
        std::vector<FactoryDef> factoryDefinitions()
        {
            using E = FactoryEntry;

            return {
                { "Init", "user", nullptr, {} },

                { "Voix parlee - Neutre", "stream", nullptr, {
                    { "gate_amount", 30.0f }, { "lowcut_amount", 1.0f },
                    { "eq_air_db", 2.0f },
                    { "comp_amount", 40.0f }, { "deess_amount", 20.0f },
                    { "reverb_short_pct", 35.0f }, { "reverb_small_pct", 39.0f }, { "reverb_big_pct", 35.0f } } },

                { "Radio - Presence", "stream", nullptr, {
                    { "gate_amount", 40.0f }, { "lowcut_amount", 1.0f },
                    { "eq_mid_db", 1.5f }, { "eq_hi_db", 2.5f },
                    { "eq_air_db", 4.0f },
                    { "comp_amount", 65.0f }, { "deess_amount", 30.0f }, { "drive_amount", 8.0f },
                    { "reverb_short_pct", 31.0f }, { "reverb_small_pct", 34.0f }, { "reverb_big_pct", 31.0f } } },

                // --- 6 presets indexes par type de micro (AC2 de US-01) --------
                { "SM7b - Voix parlee", "stream", "SM7b", {
                    { "gate_amount", 35.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", 0.5f }, { "eq_mid_db", 0.5f },
                    { "eq_hi_db", 2.0f }, { "eq_air_db", 3.0f },
                    { "comp_amount", 50.0f }, { "deess_amount", 18.0f }, { "drive_amount", 5.0f },
                    { "reverb_short_pct", 35.0f }, { "reverb_small_pct", 39.0f }, { "reverb_big_pct", 35.0f } } },

                { "SM7b - Rap", "lead", "SM7b|rap", {
                    { "gate_amount", 45.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -1.0f }, { "eq_mid_db", 0.5f },
                    { "eq_hi_db", 2.5f }, { "eq_air_db", 4.5f },
                    { "comp_amount", 70.0f }, { "deess_amount", 28.0f }, { "drive_amount", 15.0f },
                    { "doubler_amount", 20.0f }, { "width_amount", 120.0f },
                    { "delay_amount", 15.0f }, { "delay_time", 8.0f },
                    { "reverb_short_pct", 42.0f }, { "reverb_small_pct", 46.0f }, { "reverb_big_pct", 42.0f } } },

                { "SM58 - Live", "lead", "SM58", {
                    { "gate_amount", 50.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -1.5f }, { "eq_mid_db", 1.0f }, { "eq_hi_db", 1.5f },
                    { "eq_air_db", 1.5f },
                    { "comp_amount", 60.0f }, { "deess_amount", 35.0f }, { "drive_amount", 12.0f },
                    { "width_amount", 90.0f }, { "reverb_short_pct", 51.0f }, { "reverb_small_pct", 56.0f }, { "reverb_big_pct", 51.0f } } },

                { "SM58 - Podcast", "stream", "SM58", {
                    { "gate_amount", 30.0f }, { "lowcut_amount", 1.0f },
                    { "eq_hi_db", 1.5f }, { "eq_air_db", 2.5f }, { "comp_amount", 48.0f }, { "deess_amount", 28.0f },
                    { "drive_amount", 6.0f }, { "reverb_short_pct", 33.0f }, { "reverb_small_pct", 37.0f }, { "reverb_big_pct", 33.0f } } },

                { "Condenseur - Pop", "lead", "Condenseur", {
                    { "gate_amount", 35.0f }, { "lowcut_amount", 1.0f },
                    { "eq_mid_db", 0.5f }, { "eq_hi_db", 2.0f },
                    { "eq_air_db", 5.0f },
                    { "comp_amount", 55.0f }, { "deess_amount", 40.0f },
                    { "doubler_amount", 25.0f }, { "width_amount", 130.0f },
                    { "delay_amount", 18.0f }, { "delay_ducking", 40.0f },
                    { "reverb_short_pct", 54.0f }, { "reverb_small_pct", 60.0f }, { "reverb_big_pct", 54.0f } } },

                { "NT1 - Chant doux", "lead", "NT1", {
                    { "gate_amount", 20.0f }, { "lowcut_amount", 1.0f },
                    { "eq_air_db", 3.0f },
                    { "comp_amount", 42.0f }, { "deess_amount", 30.0f },
                    { "width_amount", 110.0f }, { "reverb_short_pct", 48.0f }, { "reverb_small_pct", 53.0f }, { "reverb_big_pct", 48.0f } } },

                // --- Autres voix ------------------------------------------------
                { "Rap - Lead devant", "lead", "rap", {
                    { "gate_amount", 40.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -2.0f }, { "eq_hi_db", 2.0f },
                    { "eq_air_db", 4.0f },
                    { "comp_amount", 75.0f }, { "deess_amount", 30.0f }, { "drive_amount", 20.0f },
                    { "reverb_short_pct", 39.0f }, { "reverb_small_pct", 43.0f }, { "reverb_big_pct", 39.0f } } },

                { "Rap - Adlib loin", "adlib", "rap", {
                    { "gate_amount", 45.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -3.0f }, { "eq_air_db", 3.0f },
                    { "comp_amount", 60.0f }, { "deess_amount", 35.0f },
                    { "width_amount", 150.0f }, { "delay_amount", 30.0f }, { "delay_time", 12.0f },
                    { "delay_ducking", 55.0f },
                    { "reverb_short_pct", 63.0f }, { "reverb_small_pct", 70.0f }, { "reverb_big_pct", 63.0f } } },

                { "Trap - Lead", "lead", "trap", {
                    { "gate_amount", 45.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -1.0f }, { "eq_hi_db", 3.0f },
                    { "eq_air_db", 6.0f },
                    { "comp_amount", 80.0f }, { "deess_amount", 32.0f }, { "drive_amount", 18.0f },
                    { "doubler_amount", 35.0f }, { "width_amount", 140.0f },
                    { "delay_amount", 22.0f }, { "delay_time", 8.0f },
                    { "reverb_short_pct", 44.0f }, { "reverb_small_pct", 49.0f }, { "reverb_big_pct", 44.0f } } },

                { "Trap - Adlib", "adlib", "trap", {
                    { "gate_amount", 50.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -3.0f }, { "eq_air_db", 4.0f },
                    { "comp_amount", 65.0f }, { "deess_amount", 35.0f },
                    { "doubler_amount", 45.0f }, { "width_amount", 160.0f },
                    { "delay_amount", 35.0f }, { "delay_ducking", 60.0f },
                    { "reverb_short_pct", 60.0f }, { "reverb_small_pct", 67.0f }, { "reverb_big_pct", 60.0f } } },

                { "Chant - Intime", "lead", nullptr, {
                    { "gate_amount", 15.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", 2.0f }, { "eq_air_db", 2.0f }, { "comp_amount", 38.0f }, { "deess_amount", 25.0f },
                    { "drive_amount", 8.0f }, { "reverb_short_pct", 53.0f }, { "reverb_small_pct", 58.0f }, { "reverb_big_pct", 53.0f } } },

                { "Chant - Large", "lead", nullptr, {
                    { "gate_amount", 25.0f }, { "lowcut_amount", 1.0f },
                    { "eq_hi_db", 2.0f }, { "eq_air_db", 5.0f }, { "comp_amount", 55.0f }, { "deess_amount", 35.0f },
                    { "doubler_amount", 40.0f }, { "width_amount", 155.0f },
                    { "delay_amount", 25.0f }, { "delay_time", 11.0f },
                    { "reverb_short_pct", 59.0f }, { "reverb_small_pct", 65.0f }, { "reverb_big_pct", 59.0f } } },

                { "Doublage - Voix off", "stream", nullptr, {
                    { "gate_amount", 40.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", 1.0f }, { "eq_mid_db", 1.0f },
                    { "eq_air_db", 2.0f },
                    { "comp_amount", 50.0f }, { "deess_amount", 25.0f }, { "drive_amount", 4.0f },
                    { "reverb_short_pct", 28.0f }, { "reverb_small_pct", 31.0f }, { "reverb_big_pct", 28.0f } } },

                { "Espace - Petit", "space", nullptr, {
                    { "comp_amount", 45.0f }, { "deess_amount", 25.0f },
                    { "eq_air_db", 2.5f },
                    { "reverb_short_pct", 55.0f }, { "reverb_small_pct", 25.0f },
                    { "reverb_big_pct", 20.0f } } },

                { "Espace - Grand", "space", nullptr, {
                    { "comp_amount", 45.0f }, { "deess_amount", 25.0f },
                    { "eq_air_db", 2.5f },
                    { "delay_amount", 20.0f },
                    { "reverb_short_pct", 25.0f }, { "reverb_small_pct", 40.0f },
                    { "reverb_big_pct", 55.0f }, { "reverb_lush_pct", 25.0f } } },

                { "FX - Telephone", "fx", nullptr, {
                    { "gate_amount", 45.0f }, { "lowcut_amount", 1.0f },
                    { "eq_low_db", -8.0f }, { "eq_mid_db", 4.0f }, { "eq_hi_db", -9.0f },
                    { "eq_air_db", -12.0f },
                    { "comp_amount", 80.0f }, { "deess_amount", 20.0f },
                    { "width_amount", 0.0f } } },

                { "FX - Drive pousse", "fx", nullptr, {
                    { "gate_amount", 50.0f }, { "lowcut_amount", 1.0f },
                    { "eq_hi_db", 1.0f }, { "eq_air_db", 1.0f }, { "comp_amount", 85.0f }, { "deess_amount", 30.0f },
                    { "drive_amount", 70.0f }, { "reverb_short_pct", 35.0f }, { "reverb_small_pct", 39.0f }, { "reverb_big_pct", 35.0f } } },
            };
        }
    }

    juce::var Preset::toVar() const
    {
        auto* obj = new juce::DynamicObject();

        obj->setProperty ("schemaVersion", kSchemaVersion);
        obj->setProperty ("name", name);
        obj->setProperty ("author", author);
        obj->setProperty ("category", category);
        obj->setProperty ("mode", mode);

        juce::Array<juce::var> tagArray;

        for (const auto& t : tags)
            tagArray.add (t);

        obj->setProperty ("tags", tagArray);

        auto* paramObj = new juce::DynamicObject();

        // `std::map` est ordonne : le fichier produit est stable d'une
        // sauvegarde a l'autre, ce qui rend les differences lisibles.
        for (const auto& [id, value] : parameters)
            paramObj->setProperty (juce::Identifier (id), value);

        obj->setProperty ("parameters", juce::var (paramObj));

        return juce::var (obj);
    }

    juce::String Preset::toJsonString() const
    {
        return juce::JSON::toString (toVar(), false);
    }

    juce::String serialisePreset (const Preset& p) { return p.toJsonString(); }

    bool Preset::writeToFile (const juce::File& file) const
    {
        if (file.getParentDirectory().createDirectory().failed())
            return false;

        return file.replaceWithText (toJsonString());
    }

    std::map<juce::String, float> filterToDeclared (const std::map<juce::String, float>& values,
                                                    juce::StringArray& warnings)
    {
        std::map<juce::String, float> kept;

        for (const auto& [id, value] : values)
        {
            if (params::isDeclared (id))
                kept[id] = value;
            else
                warnings.add (id);
        }

        return kept;
    }

    PresetLoadResult loadPresetFromVar (const juce::var& v)
    {
        PresetLoadResult result;

        if (! v.isObject())
        {
            result.error = "preset invalide : la racine n'est pas un objet JSON";
            return result;
        }

        auto* obj = v.getDynamicObject();

        if (obj == nullptr)
        {
            result.error = "preset invalide : objet JSON illisible";
            return result;
        }

        const int version = (int) obj->getProperty ("schemaVersion");

        if (version < 1)
        {
            result.error = "preset invalide : champ schemaVersion absent";
            return result;
        }

        if (version > Preset::kSchemaVersion)
        {
            result.error = "preset trop recent";
            return result;
        }

        Preset p;

        if (obj->hasProperty ("name"))     p.name     = obj->getProperty ("name").toString();
        if (obj->hasProperty ("author"))   p.author   = obj->getProperty ("author").toString();
        if (obj->hasProperty ("mode"))     p.mode     = obj->getProperty ("mode").toString();
        if (obj->hasProperty ("category")) p.category = obj->getProperty ("category").toString();

        if (! knownCategories().contains (p.category))
        {
            result.warnings.add ("categorie inconnue : " + p.category);
            p.category = "user";
        }

        if (auto* tags = obj->getProperty ("tags").getArray())
            for (const auto& t : *tags)
                p.tags.add (t.toString());

        std::map<juce::String, float> raw;

        if (auto* paramObj = obj->getProperty ("parameters").getDynamicObject())
            for (const auto& property : paramObj->getProperties())
                raw[property.name.toString()] = (float) (double) property.value;

        juce::StringArray unknown;
        p.parameters = filterToDeclared (raw, unknown);

        for (const auto& id : unknown)
            result.warnings.add ("parametre inconnu ignore : " + id);

        result.preset = std::move (p);
        result.ok = true;
        return result;
    }

    PresetLoadResult loadPresetFromJson (const juce::String& json)
    {
        juce::var parsed;

        if (juce::JSON::parse (json, parsed).failed())
        {
            PresetLoadResult result;
            result.error = "preset invalide : JSON illisible";
            return result;
        }

        return loadPresetFromVar (parsed);
    }

    PresetLoadResult loadPresetFromFile (const juce::File& file)
    {
        PresetLoadResult result;

        if (! file.existsAsFile())
        {
            result.error = "preset introuvable : " + file.getFullPathName();
            return result;
        }

        return loadPresetFromJson (file.loadFileAsString());
    }

    const std::vector<Preset>& factoryPresets()
    {
        static const std::vector<Preset> presets = []
        {
            std::vector<Preset> built;

            for (const auto& def : factoryDefinitions())
                built.push_back (makeFactoryPreset (def));

            return built;
        }();

        return presets;
    }

    void Snapshots::capture (int slot, std::map<juce::String, float> values)
    {
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
        {
            jassertfalse;
            return;
        }

        slots[(size_t) slot] = std::move (values);
        filled[(size_t) slot] = true;
    }

    bool Snapshots::has (int slot) const
    {
        return juce::isPositiveAndBelow (slot, kNumSlots) && filled[(size_t) slot];
    }

    void Snapshots::copy (int from, int to)
    {
        if (! has (from) || ! juce::isPositiveAndBelow (to, kNumSlots))
        {
            jassertfalse;
            return;
        }

        slots[(size_t) to] = slots[(size_t) from];
        filled[(size_t) to] = true;
    }

    const std::map<juce::String, float>& Snapshots::get (int slot) const
    {
        jassert (has (slot));

        static const std::map<juce::String, float> empty;
        return has (slot) ? slots[(size_t) slot] : empty;
    }
}
