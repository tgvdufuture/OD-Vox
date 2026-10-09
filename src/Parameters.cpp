#include "Parameters.h"

#include <iterator>

namespace odvox::params
{
    namespace
    {
        // Les 21 divisions rythmiques du delay, dans l'ordre (PRD.md §3.4).
        // Index 10 = "1/4", le defaut retenu.
        constexpr const char* kDelayDivisions =
            "1/32T|1/32|1/32D|1/16T|1/16|1/16D|1/8T|1/8|1/8D|1/4T|1/4|1/4D"
            "|1/2T|1/2|1/2D|1/1T|1/1|1/1D|2/1T|2/1|2/1D";

        // Types de bande d'EQ (F1.6). L'ORDRE est un contrat : les presets et
        // l'automation enregistrent l'index, pas le libelle — le reordonner
        // changerait silencieusement ce que designe un preset deja ecrit.
        constexpr const char* kEqTypes = "Bell|Low Shelf|High Shelf|High Pass";

        // Catalogue des 22 parametres FIGES (PRD.md §3.4, tableau
        // "Catalogue des 62 parametres", lignes marquees `Essential`).
        // Pour les parametres `choice`, `defaultValue` porte l'INDEX du choix.
        // Pour les `boolean` et `action`, `defaultValue` vaut 0 ou 1.
        constexpr Info kDeclared[] =
        {
            // --- Entree & Gate (3 Essential + 3 Advanced) --------------------
            { "input_gain_db",   "Input Gain",   Module::inputGate, true, Unit::db,      -24.0f, 24.0f, 0.1f,  0.0f, nullptr, true  },
            { "input_calibrate", "Calibrate",    Module::inputGate, true, Unit::action,    0.0f,  1.0f, 1.0f,  0.0f, nullptr, false },
            { "gate_amount",     "Gate",         Module::inputGate, true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            // Rev du 2026-09-22 : PLUS de reglages de gate exposes. Seuil (−45 dB),
            // release (150 ms) et plafond (80 dB) sont des constantes de conception
            // dans `Gate::Settings` — le produit n'expose qu'un knob, et chaque
            // knob de plus contredit la philosophie du produit.

            // --- Nettoyage (1 Essential) -------------------------------------
            // **F1.5 revu le 2026-09-19 : le coupe-bas est FIGE.** C'est un
            // interrupteur, exactement comme dans la cible, dont la
            // mesure etablit un passe-haut a coin -3 dB vers 110-120 Hz, de pente
            // ~19,8 dB/oct, **sans aucun reglage**. Les trois `Advanced` qui
            // l'accompagnaient (frequence,
            // pente, profondeur dynamique) sont retires : ils reglaient un module
            // dont la raison d'etre est precisement de ne pas se regler. La
            // frequence et la pente vivent maintenant dans `LowCut` comme
            // constantes de conception (120 Hz, 24 dB/oct), pas ici.
            //
            // **AMENDEMENT du 2026-09-29 (refonte UI, cartes V6)** : le defaut
            // passe de Off a ON, comme dans la cible. Le contrat de
            // transparence (§3.4) est assoupli en consequence : il porte desormais
            // sur les CURSEURS (gate/comp/de-ess/drive a 0 % = transparence), pas
            // sur les interrupteurs de nettoyage — un coupe-bas est un reglage
            // TONAL, au meme titre que l'Air +2,5 dB du defaut. Le maquettage a
            // valide le toggle ON dans le heros (tools/mockup_cards.html).
            { "lowcut_amount",   "Low Cut",      Module::cleanup,   true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },

            // --- Justesse : MODULE RETIRE (decision du 2026-09-19) -----------
            // `pitch_amount`, `pitch_speed_ms` et `pitch_key` ont ete declares
            // au F1.2 puis jamais implementes (aucun module DSP, aucun usage dans
            // la chaine) : trois curseurs inertes a l'ecran. Le PRD placait la
            // correction de justesse en Phase 2 (F2.1), jugee trop risquee
            // (warble, clics) pour le gain attendu. Le produit est donc
            // **sans correction de justesse**, et le catalogue n'en porte plus
            // trace : un parametre sans module derriere n'est pas un reglage,
            // c'est un mensonge a l'utilisateur. Les identifiants ne reviendront
            // pas — les reutiliser plus tard casserait ce contrat.

            // --- EQ (6) -----------------------------------------------------
            // **4 bandes, la structure retenue** (revise le 2026-09-19) :
            // Low shelf 120 Hz, Mid cloche 700 Hz Q~1, High shelf (coin 1,75 kHz,
            // plein gain a 3,5 kHz), Air shelf > 10 kHz — la structure MESUREE
            // (recallee au signal le 2026-09-20). La cinquieme
            // bande, Low-Mid, etait NOTRE ajout : elle est retiree, avec ses
            // trois `Advanced`. Les quatre bandes restantes ne sont plus un EQ
            // statique : chacune porte la dynamique relative (a
            // son propre niveau, accrochage harmonique borne, AutoGain d'etage).
            { "eq_low_db",       "Low",          Module::eq,        true, Unit::db,      -15.0f, 15.0f, 0.1f,  0.0f, nullptr, true  },
            { "eq_mid_db",       "Mid",          Module::eq,        true, Unit::db,      -15.0f, 15.0f, 0.1f,  0.0f, nullptr, true  },
            { "eq_hi_db",        "High",         Module::eq,        true, Unit::db,      -15.0f, 15.0f, 0.1f,  0.0f, nullptr, true  },
            { "eq_air_db",       "Air",          Module::eq,        true, Unit::db,      -15.0f, 15.0f, 0.1f,  2.5f, nullptr, true  },
            { "eq_on",           "EQ On",        Module::eq,        true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },

            // Rev du 2026-09-22 : PLUS de freq/Q/type exposes par bande. Les quatre
            // bandes sont PITCH-SUIVEUSES (F1.6 revu) : leurs ancres (120 / 700 /
            // 1750 / 10 000 Hz), Q (1,00 / 1,15 / 1,20 / 1,00) et types (shelf /
            // cloche / shelf / shelf) sont des constantes de conception calibrees
            // calibrees, et vécues dans `Eq::Settings`.
            // Une frequence manuelle contredirait le suivi de pitch ; les gains
            // restent les quatre knobs Essential ci-dessus.

            // --- Compresseur (1+4) -------------------------------------------
            // Defaut a 0 % : AC2 de US-02 exige la transparence quand tous les
            // curseurs sont a 0 %, et le test de chaine par defaut la verifie au
            // bit pres. Les presets d'usine posent leur valeur explicitement.
            // NOTE fidelite (F1.7b) : la cible est a 70 % par defaut
            // et n'est JAMAIS desactive par son curseur (1,5:1 des 0 %). Chez
            // nous, c'est le court-circuit du curseur a 0 % qui donne la
            // transparence bit-exacte (rev du 2026-09-21 : comp_on supprime).
            { "comp_amount",     "Comp",         Module::comp,      true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            // UN SEUL curseur public, ENGAGEANT (rev du 2026-09-21 : le produit
            // n'expose aucun reglage de comp du tout — son compresseur est
            // interne et toujours actif). La table mesuree pilote tout : seuil
            // fixe -50 dBFS, release interne 150 ms, make-up de table.
            // Le defaut 0 % est transparent : a 0 % le make-up de table vaut
            // +1,7 dB et la reduction 1,5:1 — dans le plancher de tolerance du
            // contrat US-02 AC2 (les modules ENGAGES d'usine ne sont pas
            // transparents par conception, cf. le compresseur).
            // Les parametres avances et le mode Custom sont supprimes.

            // --- De-esser (1) -------------------------------------------------
            // UN SEUL curseur, cible de conception (retour utilisateur
            // du 2026-09-21) : croisement fixe 5 kHz, plosives lies au curseur
            // (cap 20 %), listen supprime — tout le reste est preregle interne.
            // Defaut a 0 % : AC2 de US-02 exige la transparence quand tous les
            // curseurs sont a 0 %, et un de-esser ADAPTATIF actif a 25 % la
            // violerait (meme arbitrage que `comp_amount` au F1.7) — la valeur
            // 25 % du PRD initial est reprise par les presets d'usine.
            { "deess_amount",    "De-ess",       Module::deess,     true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },            // --- Drive (2) --------------------------------------------------
            { "drive_amount",     "Drive",        Module::drive,     true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            // Mode haute qualite (F2.2, US-09) : oversampling 4x du saturateur
            // (plafonne a 2x au-dela de 96 kHz, edge case du PRD). Off =
            // zero-latency. Le seul ajout du catalogue depuis le figeage des
            // 25 : il vient de la Phase 2, pas d'un reglage cache.
            { "hq_mode",          "HQ Mode",      Module::drive,     true, Unit::boolean,   0.0f,  1.0f, 1.0f,  0.0f, nullptr, true  },
            // UN SEUL curseur (rev du 2026-09-21 : le `drive` de la cible
            // est son SEUL reglage, 0..1) : l'engin est fige sur Console —
            // la saveur MESUREE chez lui — et le mix est interne.
            // Defaut `Console` : c'est la saveur qui reproduit l'engin MESURE du
            // cible (saturation symetrique, impairs seuls). Le choix
            // par defaut du PRD initial etait `Tube`, dont l'asymetrie ne
            // correspond pas a la mesure — voir §3.4.
            //
            // **AMENDEMENT du 2026-09-29 (refonte UI)** : `hq_mode` est TOUJOURS
            // ACTIF — le processeur pose `setHqRequested (true)` au prepareToPlay
            // et ignore le parametre (decide avec l'utilisateur : « Toujours HQ
            // (recommande) »). Il reste declare pour la compatibilite des
            // sessions/presets, mais plus aucun toggle ne l'affiche.
            //

            // --- Image (2) --------------------------------------------------
            { "doubler_amount",  "Doubler",      Module::image,     true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            { "width_amount",    "Width",        Module::image,     true, Unit::percent,   0.0f,200.0f, 1.0f,100.0f, nullptr, true  },
            // Detune (12 cents) et retard (22 ms) du doubler : constantes de
            // conception dans `Image::Settings` (rev du 2026-09-22, cf. Gate).

            // --- Delay (7) --------------------------------------------------
            { "delay_amount",    "Delay",        Module::delay,     true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            { "delay_time",      "Delay Time",   Module::delay,     true, Unit::choice,    0.0f, 20.0f, 1.0f, 10.0f, kDelayDivisions, true },
            { "delay_sync",      "Delay Sync",   Module::delay,     true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },
            { "delay_time_ms",   "Free Time",    Module::delay,     true, Unit::ms,        1.0f,2000.0f, 1.0f,500.0f, nullptr, true  },
            { "delay_ducking",   "Ducking",      Module::delay,     true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            // Feedback (20 %, plafonne a 95 % en interne) et filtre des echoes
            // (39 %, la valeur retenue) : constantes de conception
            // dans `Delay::Settings` (rev du 2026-09-22).

            // --- Reverb (4) -------------------------------------------------
            // Structure FIDELE au vif de la cible (retour utilisateur du
            // 2026-09-21) : quatre boutons, un par moteur — Short / Small /
            // Big / Lush — un curseur par moteur. Les
            // moteurs sont TOUJOURS cumules, l'utilisateur dose chaque couleur
            // moteur par moteur ; le pre-delay reste préréglé en interne.
            { "reverb_short_pct", "Short",       Module::reverb,    true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            { "reverb_small_pct", "Small",       Module::reverb,    true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            { "reverb_big_pct",   "Big",         Module::reverb,    true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },
            { "reverb_lush_pct",  "Lush",        Module::reverb,    true, Unit::percent,   0.0f,100.0f, 1.0f,  0.0f, nullptr, true  },

            // --- Enables de groupe (3) — AMENDEMENT du 2026-09-29 ----------
            // La refonte UI (maquette tools/mockup_cards.html, transposee) fait
            // de la LED de bande un INTERRUPTEUR du groupe. Trois nouvelles
            // entrees du catalogue, sur le modele de `eq_on` :
            // - `fx_on`     : bypass de groupe GATE/DE-ESS/DRIVE/DOUBLER/WIDTH
            //   (tranche avec l'utilisateur : bypass, pas valeurs a zero —
            //   aucun reglage perdu, transparence bit-exacte via `inert`) ;
            // - `delay_on`  : bypass du delay (la LED DELAY/REVERB pilote son
            //   module ; avant l'amendement, aucun enable n'existait) ;
            // - `reverb_on` : bypass de la reverb.
            // Un OFF survit aux presets, a l'A/B et a la session : c'est un
            // parametre, pas un etat d'interface. ON par defaut : les defauts
            // du catalogue restent ceux de la chaine validee.
            { "fx_on",           "FX On",        Module::deess,     true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },
            { "delay_on",        "Delay On",     Module::delay,     true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },
            { "reverb_on",       "Reverb On",    Module::reverb,    true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },

            // --- Sortie (2) -------------------------------------------------
            { "output_gain_db",  "Output Gain",  Module::output,    true, Unit::db,      -24.0f, 24.0f, 0.1f,  0.0f, nullptr, true  },
            { "output_dc_filter", "DC Filter",   Module::output,    true, Unit::boolean,   0.0f,  1.0f, 1.0f,  1.0f, nullptr, true  },
        };

        constexpr int countEssential()
        {
            int n = 0;

            for (const auto& info : kDeclared)
                n += info.essential ? 1 : 0;

            return n;
        }

        // Le contrat de sortie du produit : ces identifiants ne se renomment
        // jamais (PRD.md §3.4). F2.2 (2026-09-22) ajoute `hq_mode` — un ajout
        // de la Phase 2, pas un reglage cache.
        // 26 params avant le 2026-09-29 ; l'amendement UI ajoute les trois
        // enables de groupe (fx_on, delay_on, reverb_on).
        static_assert (countEssential() == 29,
                       "Le catalogue declare exactement les 29 parametres `Essential` (26 du 2026-09-29 + les 3 enables de groupe de la refonte UI).");

        // Rev du 2026-09-22 : le mode Avance est SUPPRIME. Les 19 parametres
        // Advanced d'alors (3 gate, 12 EQ, 2 doubler, 2 delay) reglaient des
        // choses tenues pour internes a la conception, et que nos modules
        // portent maintenant en constantes. Un knob par idee — la surface
        // visee, plus l'interrupteur HQ de la Phase 2.
        static_assert (std::size (kDeclared) == 29,
                       "29 parametres, tous `Essential` (Entree & Gate, Nettoyage, EQ, Comp, De-ess, Drive + HQ, Image, Delay, Reverb, Sortie, enables de groupe).");

        juce::StringArray splitChoices (const char* choices)
        {
            juce::StringArray result;
            result.addTokens (juce::String (choices), "|", {});
            return result;
        }
    }

    juce::String moduleName (Module m)
    {
        switch (m)
        {
            case Module::inputGate: return "Entree & Gate";
            case Module::cleanup:   return "Nettoyage";
            case Module::eq:        return "EQ";
            case Module::comp:      return "Compresseur";
            case Module::deess:     return "De-esser";
            case Module::drive:     return "Drive";
            case Module::image:     return "Image";
            case Module::delay:     return "Delay";
            case Module::reverb:    return "Reverb";
            case Module::output:    return "Sortie";
        }

        return {};
    }

    juce::String unitName (Unit u)
    {
        switch (u)
        {
            case Unit::db:      return "dB";
            case Unit::percent: return "%";
            case Unit::hz:      return "Hz";
            case Unit::ms:      return "ms";
            case Unit::cents:   return "cents";
            case Unit::choice:  return "choix";
            case Unit::boolean: return "booleen";
            case Unit::action:  return "action";
            case Unit::ratio:   return "x";
            case Unit::q:       return {};   // sans dimension, voir Parameters.h
        }

        return {};
    }

    juce::Span<const Info> declared() { return kDeclared; }

    // Declare ici plutot qu'anonyme : `isDeclared` s'appuie dessus.
    const Info* find (juce::StringRef id)
    {
        const auto wanted = juce::String (id);

        for (const auto& info : kDeclared)
            if (wanted == juce::String (info.id))
                return &info;

        return nullptr;
    }

    bool isDeclared (juce::StringRef id)
    {
        return find (id) != nullptr;
    }

    juce::NormalisableRange<float> rangeFor (const Info& info)
    {
        juce::NormalisableRange<float> range { info.min, info.max, info.step };

        // Un temps de relachement et une frequence de coupure se percoivent de
        // facon logarithmique (PRD.md §3.4 : la release du gate « log »,
        // `lowcut_freq_hz` « log »). `setSkewForCentre` donne ce ressenti tout en
        // gardant la borne basse atteignable, ce qu'une vraie echelle
        // logarithmique interdit — et `lowcut_freq_hz` descend a 20 Hz.
        if (info.unit == Unit::ms || info.unit == Unit::hz)
            range.setSkewForCentre (juce::jmax (info.min + 1.0f, info.defaultValue));

        return range;
    }

    float actualToNormalised (juce::StringRef id, float actual)
    {
        if (const auto* info = find (id))
            return rangeFor (*info).convertTo0to1 (actual);

        return 0.0f;
    }

    float normalisedToActual (juce::StringRef id, float normalised)
    {
        if (const auto* info = find (id))
            return rangeFor (*info).convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised));

        return 0.0f;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;

        AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& info : kDeclared)
        {
            const ParameterID pid { info.id, 1 };

            switch (info.unit)
            {
                case Unit::choice:
                {
                    auto choices = splitChoices (info.choices);
                    jassert (! choices.isEmpty());
                    layout.add (std::make_unique<AudioParameterChoice> (
                        pid, info.name, choices, (int) info.defaultValue));
                    break;
                }

                case Unit::boolean:
                case Unit::action:
                {
                    auto attributes = AudioParameterBoolAttributes()
                                          .withAutomatable (info.automatable);

                    layout.add (std::make_unique<AudioParameterBool> (
                        pid, info.name, info.defaultValue >= 0.5f, attributes));
                    break;
                }

                case Unit::db:
                case Unit::percent:
                case Unit::hz:
                case Unit::ms:
                case Unit::cents:
                default:
                {
                    auto attributes = AudioParameterFloatAttributes()
                                          .withLabel (unitName (info.unit))
                                          .withAutomatable (info.automatable);

                    layout.add (std::make_unique<AudioParameterFloat> (
                        pid, info.name, rangeFor (info), info.defaultValue, attributes));
                    break;
                }
            }
        }

        return layout;
    }
}
