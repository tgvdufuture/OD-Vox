# Composants tiers et licences

OD Vox est distribué sous **AGPL-3.0** (voir `LICENSE`). Ce document recense les
composants tiers embarqués et explique pourquoi cette licence est compatible avec
eux.

## JUCE 8

- **Emplacement** : `external/JUCE` (non vendu dans ce dépôt — voir `README.md`)
- **Licence** : **double** — AGPLv3 **ou** licence commerciale JUCE 8
  (`external/JUCE/LICENSE.md`)

JUCE est utilisé ici sous la branche **AGPLv3**. C'est ce qui autorise la
distribution du plugin : l'AGPLv3 ne demande pas de licence commerciale.

**Conséquence à connaître** : dès lors qu'un binaire d'OD Vox est distribué,
l'ensemble forme une œuvre couverte par l'AGPLv3, avec son code source. Distribuer
OD Vox **sans publier le code** (binaire à code fermé) exigerait une licence
commerciale JUCE en cours de validité — la licence « Personal » de JUCE ne couvre
pas ce cas.

## SDK VST3 de Steinberg

- **Emplacement** : `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/`
- **Licence** : GPLv3, ou accords de licence Steinberg
  (`.../VST3_SDK/LICENSE.txt`)

Le SDK est distribué sous GPLv3 (avec la double option Steinberg propriétaire).
GPLv3 et AGPLv3 se combinent : la GPLv3 autorise explicitement la combinaison avec
une œuvre sous AGPLv3 (§13 de l'AGPL), le résultat restant couvert par l'AGPLv3.

## AudioUnitSDK

- **Emplacement** : `external/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/`
- **Licence** : Apache 2.0 (`.../AudioUnitSDK/LICENSE.txt`)

Apache 2.0 est compatible avec l'AGPLv3 (licence permissive, sans copyleft sur
l'ensemble). Utilisée par les formats AU, non construits ici (VST3 seul).

## Oboe (Android)

- **Emplacement** : `external/JUCE/modules/juce_audio_devices/native/oboe/`
- **Licence** : Apache 2.0 (`.../oboe/LICENSE`)

Idem : permissive, sans effet sur la licence de l'ensemble. Non construite ici
(plateforme Android).

## Outils de développement (non distribués)

Le harnais de vérification (`tools/verify_plugin.py`) utilise Python,
`pedalboard`, `numpy` et `scipy` dans un environnement local `.venv/`. Ces paquets
ne sont **pas** liés au plugin ni distribués avec lui ; leurs licences respectives
s'appliquent à ces outils, pas au binaire.

L'atlas de sprites (`assets/knob_atlas.png`) et le fond (`assets/background.png`)
sont produits par les scripts de `tools/` (Pillow, Playwright) : ce sont des
créations du projet.
