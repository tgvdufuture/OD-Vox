#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <map>
#include <vector>

namespace odvox
{
    /** Entite `Preset` (PRD.md §3.3), serialisee en JSON dans un fichier
        `.odvoxpreset`.

        `parameters` fait correspondre un identifiant de parametre a sa valeur
        **normalisee 0..1**, pas sa valeur reelle : c'est ce qui permet de
        corriger une plage plus tard sans invalider les presets deja ecrits. */
    struct Preset
    {
        static constexpr int kSchemaVersion = 1;

        juce::String name { "Init" };
        juce::String author { "OD Audio" };
        juce::String category { "user" };
        juce::StringArray tags;
        std::map<juce::String, float> parameters;
        juce::String mode { "essential" };

        juce::var toVar() const;
        juce::String toJsonString() const;

        /** Ecrit le preset. Renvoie false si le fichier n'est pas inscriptible,
            auquel cas l'appelant doit conserver l'etat audio courant
            (PRD.md §3.4, cas d'erreur « dossier non inscriptible »). */
        bool writeToFile (const juce::File&) const;

        bool operator== (const Preset&) const = default;
    };

    /** Resultat d'analyse d'un preset. `ok == false` signifie refus : l'etat
        audio doit rester inchange (PRD.md §3.4). */
    struct PresetLoadResult
    {
        bool ok = false;
        Preset preset;
        juce::StringArray warnings;  // identifiants inconnus ignores
        juce::String error;          // motif du refus, si !ok
    };

    PresetLoadResult loadPresetFromVar (const juce::var&);
    PresetLoadResult loadPresetFromJson (const juce::String&);
    PresetLoadResult loadPresetFromFile (const juce::File&);

    /** Ecrit un preset en JSON lisible. Le format reste diagnosticable a l'oeil
        nu, sans lancer le plugin (PRD.md §3.3). */
    juce::String serialisePreset (const Preset&);

    /** Ne conserve que les parametres declares, et journalise les autres.
        Un identifiant inconnu est ignore sans faire echouer le chargement
        (PRD.md §3.4). */
    std::map<juce::String, float> filterToDeclared (const std::map<juce::String, float>&,
                                                    juce::StringArray& warnings);

    /** Les 20 presets d'usine. Cible ramenee de 60 a 20 le 2026-09-18 : elle
        venait d'une exigence de mise sur le marche, sans objet ici (PRD.md §8).
        Six d'entre eux sont indexes par type de micro (AC2 de US-01). */
    const std::vector<Preset>& factoryPresets();

    /** Les emplacements A/B (US-11). Chaque emplacement est un instantane
        complet des parametres declares. */
    class Snapshots
    {
    public:
        static constexpr int kNumSlots = 2;   // 0 = A, 1 = B

        void capture (int slot, std::map<juce::String, float> values);
        bool has (int slot) const;
        void copy (int from, int to);
        const std::map<juce::String, float>& get (int slot) const;

    private:
        std::array<std::map<juce::String, float>, (size_t) kNumSlots> slots;
        std::array<bool, (size_t) kNumSlots> filled { false, false };
    };
}
