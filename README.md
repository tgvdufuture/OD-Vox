# OD Vox

Chaîne vocale tout-en-un — plugin **VST3** (Windows), **C++20 / JUCE 8**.

On charge un preset d'usine, on dose un curseur par module, on écoute. Le produit
expose **29 paramètres, tous visibles** : pas de réglage caché, pas de mode avancé,
un knob par idée.

## La chaîne

Dans l'ordre du traitement :

| Étage | Réglage | Plage |
|---|---|---|
| Input Gain | `input_gain_db` | −24 → +24 dB |
| Calibration d'entrée | `input_calibrate` | action (écoute ~5 s) |
| Gate | `gate_amount` | 0 → 100 % |
| Low Cut | `lowcut_amount` | On / Off — 120 Hz, 24 dB/oct fixes |
| EQ 4 bandes | `eq_low_db`, `eq_mid_db`, `eq_hi_db`, `eq_air_db`, `eq_on` | ±15 dB |
| Comp | `comp_amount` | 0 → 100 % |
| De-ess | `deess_amount` | 0 → 100 % |
| Drive | `drive_amount`, `hq_mode` | 0 → 100 % — HQ 4× toujours actif |
| Doubler / Width | `doubler_amount`, `width_amount` | 0 → 100 % / 0 → 200 % |
| Delay | `delay_amount`, `delay_time`, `delay_sync`, `delay_time_ms`, `delay_ducking` | 21 divisions rythmiques ou 1–2000 ms |
| Reverb | `reverb_short_pct`, `reverb_small_pct`, `reverb_big_pct`, `reverb_lush_pct` | 0 → 100 % |
| Bypass de groupe | `fx_on`, `delay_on`, `reverb_on` | LED de bande |
| Sortie | `output_gain_db`, `output_dc_filter` | ±24 dB — filtre DC à 10 Hz |

**20 presets d'usine** sont livrés, dont 6 indexés par type de micro (SM7b, SM58,
condensateur, NT1).

## Construire

Prérequis :

- **CMake ≥ 3.22** ;
- **Visual Studio (MSVC)** — construit ici avec le toolset 14.29 (VS 2019 16.11) ;
- **JUCE 8** dans `external/JUCE`. Le dossier est ignoré par git : JUCE n'est pas
  vendu dans ce dépôt.

```bash
git clone --branch 8.0.15 --depth 1 https://github.com/juce-framework/JUCE external/JUCE
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Le bundle est produit dans `build/ODVox_artefacts/Release/VST3/OD Vox.vst3`. Le build
active `COPY_PLUGIN_AFTER_BUILD` : il est aussi recopié dans le dossier VST3 du
système (`C:\Program Files\Common Files\VST3\`).

## Vérifier

```bash
cmake -B build -DODVOX_BUILD_TESTS=ON      # cibles de test : OFF par défaut
cmake --build build --config Release
ctest --test-dir build -C Release          # ODVoxTests + ODVoxEditorTests
```

Les lots couvrent la logique (catalogue, presets, DSP, contrat de bus) et
l'interface (dessin et gestes de la courbe d'EQ, hors écran, sans fenêtre).

Le harnais de vérification hors ligne s'exécute sur le **VST3 installé** et couvre
59 contrôles — transparence aux défauts, latence reportée, courbes, seuils :

```bash
python tools/verify_plugin.py     # nécessite .venv : pedalboard, numpy, scipy
```

Il écrit `verification_report.md`.

Captures et stress de l'éditeur (cibles manuelles) :

```bash
cmake --build build --config Release --target ODVoxSnapshot   # capture PNG
cmake --build build --config Release --target ODVoxStress     # création/destruction de l'éditeur
```

## Documentation

- `PRD.md` — spécification produit : catalogue de paramètres, contrats d'interface,
  critères d'acceptation et les mesures qui les établissent.
- `docs/FIGMA_KIT.md` — redessiner les assets d'interface dans Figma.
- `tools/` — harnais de vérification et générateurs d'assets.

## Licence

**AGPL-3.0** — voir `LICENSE`.

Le plugin embarque JUCE 8 et le SDK VST3 de Steinberg, dont les licences sont
détaillées dans `THIRD_PARTY.md`. En résumé : publier OD Vox sous AGPLv3 est
possible ; distribuer une version **à code fermé** ne l'est pas sans licence
commerciale JUCE.

Les binaires publiés (GitHub Releases) sont accompagnés de leur code source
correspondant, qui est ce dépôt — c'est ce qu'exige la licence.

## Provenance

Aucune ligne de code, aucun asset graphique, aucun preset et aucune réponse
impulsionnelle de ce dépôt ne provient d'un autre produit. Les repères de
conception — ancres d'EQ à 120/700/1 750/10 000 Hz, courbe du compresseur,
21 divisions rythmiques du delay, RT60 des quatre moteurs de reverb — sont des
**valeurs retenues pour ce produit**, documentées comme telles dans `PRD.md`.
