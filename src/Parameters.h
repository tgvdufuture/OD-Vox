#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace odvox::params
{
    /** Module de traitement auquel appartient un parametre (PRD.md §3.3). */
    enum class Module
    {
        inputGate, cleanup, eq, comp, deess, drive, image, delay, reverb, output
    };

    /** Unite d'affichage d'un parametre (PRD.md §3.3).

        `q` est sans dimension et SANS etiquette : l'hote compose la cle d'un
        parametre en accoleant son etiquette a son nom affiche, donc une
        etiquette « Q » donnerait la cle `low_q_q`. Nom affiche « Low Q » + aucune
        etiquette donnent `low_q`, qui se lit et s'automatise sans ambiguite. */
    enum class Unit { db, percent, hz, ms, cents, choice, boolean, action, ratio, q };

    /** Une entree du catalogue de parametres (PRD.md §3.3, entite
        `ParameterDefinition`). */
    struct Info
    {
        const char* id;
        const char* name;
        Module module;
        bool essential;      // fait partie des 22 identifiants FIGES
        Unit unit;
        float min, max, step;
        float defaultValue;
        const char* choices; // "A|B|C" si l'unite vaut `choice`, sinon nullptr
        bool automatable;
    };

    juce::String moduleName (Module);
    juce::String unitName (Unit);

    /** Plage d'un parametre, construit en UN SEUL endroit pour que le bus de
        parametres et les conversions reelle <-> normalisee ne puissent pas
        diverger. */
    juce::NormalisableRange<float> rangeFor (const Info&);

    /** Les parametres declares par F1.2 : exactement les 22 `Essential` figes du
        §3.4 du PRD. Les `Advanced` sont ajoutes module par module, quand le
        lot de Phase 1 qui les implemente est ecrit — leurs identifiants ne sont
        donc pas encore figes. */
    juce::Span<const Info> declared();

    /** Vrai si `id` fait partie des parametres figes. Sert a filtrer un preset
        qui referencerait un identifiant inconnu (PRD.md §3.4). */
    bool isDeclared (juce::StringRef id);

    /** Construit le bus de parametres a partir de `declared()`. Un seul endroit
        declare les parametres : le catalogue ci-dessus. */
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Conversion valeur reelle -> valeur normalisee 0..1, telle que stockee dans
        un preset (PRD.md §3.3). Renvoie 0 si l'identifiant n'est pas declare. */
    float actualToNormalised (juce::StringRef id, float actual);

    /** Conversion inverse. */
    float normalisedToActual (juce::StringRef id, float normalised);
}
