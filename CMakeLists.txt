cmake_minimum_required(VERSION 3.22)

# --- Identite -------------------------------------------------------------
# Famille "OD" + matiere : OD Vox (v1), OD Master, ...
# Seule contrainte : JUCE/VST3 exigent des codes de exactement 4 caracteres,
# uniques et stables (un identifiant VST3 ne se renomme pas une fois utilise).
set(OD_PRODUCT_NAME  "OD Vox")
set(OD_COMPANY_NAME  "OD Audio")
set(OD_COMPANY_CODE  "OdAu")   # fabricant, exactement 4 caracteres
set(OD_PLUGIN_CODE   "Odvx")   # produit, exactement 4 caracteres
set(OD_BUNDLE_ID     "com.odaudio.odvox")

# JUCE 8 exige aussi le langage C pour ses cibles.
project(ODVox VERSION 0.1.0 LANGUAGES C CXX)

# C++20 demande par F1.1. ATTENTION : le toolset local est MSVC 14.29 (VS 2019
# 16.11), qui couvre C++20 partiellement. Si une fonctionnalite manque, le repli
# est C++17 (minimum requis par JUCE 8).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# F1.1 : -ffast-math doit rester DESACTIVE sur les etages non lineaires.
# Ni MSVC ni GCC n'activent fast-math par defaut ; on n'ajoute donc rien.
# Interdiction explicite de /fp:fast et -ffast-math dans ce projet.

add_subdirectory(external/JUCE)

# --- Logique partagee ------------------------------------------------------
# INTERFACE et non STATIQUE : chaque cible (plugin, tests) compile ces sources
# avec ses propres modules JUCE. Une bibliotheque statique lierait un second
# exemplaire des modules et provoquerait des symboles dupliques.
add_library(ODVoxCore INTERFACE)
target_include_directories(ODVoxCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Skin.h inclut <knob_atlas_meta.h> (constantes de l'atlas de frames,
    # generees a cote du PNG) : tous les cibles qui compilent l'editeur
    # linkent ODVoxCore, donc un seul endroit suffit.
    ${CMAKE_CURRENT_SOURCE_DIR}/assets)
target_sources(ODVoxCore INTERFACE
    src/Parameters.cpp
    src/Presets.cpp
    src/Smoothing.cpp
    src/Gate.cpp
    src/LowCut.cpp
    src/Compressor.cpp
    src/DeEsser.cpp
    src/Drive.cpp
    src/Image.cpp
    src/PitchDetector.cpp
    src/Eq.cpp
    src/EqCurve.cpp
    src/Delay.cpp
    src/Reverb.cpp
    src/Spectrum.cpp
    src/Calibrator.cpp
)
target_link_libraries(ODVoxCore INTERFACE
    juce::juce_audio_processors
    # La FFT de l'analyseur de spectre (F1.6c) vient de juce_dsp. Elle est liee
    # ici et non sur le seul plugin : la cible de tests compile Spectrum.cpp, et
    # un module manquant ne se verrait qu'a l'edition de liens.
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

juce_add_plugin(${PROJECT_NAME}
    PRODUCT_NAME                "${OD_PRODUCT_NAME}"
    COMPANY_NAME                "${OD_COMPANY_NAME}"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "${OD_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    ${OD_COMPANY_CODE}
    PLUGIN_CODE                 ${OD_PLUGIN_CODE}
    FORMATS                     VST3
    VST3_CATEGORIES             "Fx"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

target_sources(${PROJECT_NAME} PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/EqCurveView.cpp
)

# --- Assets d'interface (mode « sprites » de docs/FIGMA_KIT.md §9) ----------
# Les graphismes dessines dans Figma (docs/figma/) sont encodes dans le
# binaire. L'editeur y accede via BinaryData et retombe sur le dessin maison
# si un sprite manque. Un seul cote : la cible du plugin — les tests
# d'interface gardent le dessin vectoriel, qui reste la reference.
juce_add_binary_data(ODVoxAssets
    NAMESPACE                ODVoxBinary
    HEADER_NAME              ODVoxBinaryData.h
    SOURCES
        assets/knob.png
        assets/knob_atlas.png
        assets/background.png
        assets/lcd_bezel.png
)

# L'editeur est compile uniquement dans la cible du plugin (F1.5b) : la cible de
# tests compile PluginProcessor.cpp sans ODVOX_HAS_EDITOR, createEditor() y
# retourne donc nullptr et l'UI n'est jamais liee aux tests.
target_compile_definitions(${PROJECT_NAME} PRIVATE ODVOX_HAS_EDITOR=1)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ODVoxCore
    ODVoxAssets
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# --- Tests unitaires -------------------------------------------------------
# La verification hors ligne via pedalboard reste le harnais principal (§3.4).
# Les tests unitaires couvrent la logique pure : catalogue de parametres,
# presets et emplacements A/B. Desactive par defaut pour ne pas alourdir un
# build de plugin ordinaire.
option(ODVOX_BUILD_TESTS "Construire le lanceur de tests unitaires" OFF)

if (ODVOX_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(ODVoxTests PRODUCT_NAME "OD Vox Tests")

    # PluginProcessor.cpp est compile ici aussi : sans lui, le bus de parametres
    # et les emplacements A/B ne seraient verifiables qu'a travers un hote, et
    # AC3 de US-01 (un preset ne touche que ce qu'il cite) n'aurait aucune
    # couverture du tout.
    target_sources(ODVoxTests PRIVATE
        tests/TestMain.cpp
        tests/ParameterBusTests.cpp
        tests/DspModuleTests.cpp
        tests/CompressorTests.cpp
        tests/DeEsserTests.cpp
        tests/DriveTests.cpp
        tests/EqTests.cpp
        tests/DelayTests.cpp
        tests/ReverbTests.cpp
        tests/ImageTests.cpp
        tests/EqCurveTests.cpp
        tests/SpectrumTests.cpp
        tests/OutputTests.cpp
        src/PluginProcessor.cpp
    )

    # PluginProcessor.cpp s'appuie sur les macros JucePlugin_* generees par
    # juce_add_plugin. On les reprend telles quelles au lieu de les recopier,
    # sinon les deux cibles divergeraient au premier changement d'identite.
    # EXCEPTION : ODVOX_HAS_EDITOR est filtre — defini PRIVATE sur le plugin,
    # il passerait sinon par ce genex et ferait referencer l'editeur par les
    # tests, qui ne lient pas PluginEditor.cpp.
    target_compile_definitions(ODVoxTests PRIVATE
        $<FILTER:$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>,EXCLUDE,^ODVOX_HAS_EDITOR=.*$>
    )

    target_link_libraries(ODVoxTests PRIVATE
        ODVoxCore
        juce::juce_audio_processors
        juce::juce_events
    )

    add_test(NAME odvox-unit COMMAND $<TARGET_FILE:ODVoxTests>)

    # --- Tests d'interface --------------------------------------------------
    # La cible precedente ne lie PAS l'interface (voir le commentaire plus haut) :
    # elle verifie la logique. Ce second lanceur la lie, et c'est le seul endroit
    # ou le dessin et les gestes de la courbe d'EQ (F1.6c) sont exerces POUR DE
    # VRAI. Un composant JUCE se construit et se dessine hors ecran : aucun
    # fenetre n'est ouverte, et les gestes sont des `juce::MouseEvent` fabriques.
    juce_add_console_app(ODVoxEditorTests PRODUCT_NAME "OD Vox Editor Tests")

    target_sources(ODVoxEditorTests PRIVATE
        tests/EditorTestMain.cpp
        tests/EditorTests.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/EqCurveView.cpp
    )

    # Les macros JucePlugin_* viennent de juce_add_plugin, comme pour la cible
    # precedente. ODVOX_HAS_EDITOR est ici DEFINI (l'interface est liee) : c'est
    # l'inverse de la cible precedente, et ce sont les deux seules valeurs
    # possibles de la macro.
    target_compile_definitions(ODVoxEditorTests PRIVATE
        ODVOX_HAS_EDITOR=1
        $<FILTER:$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>,EXCLUDE,^ODVOX_HAS_EDITOR=.*$>
    )

    target_link_libraries(ODVoxEditorTests PRIVATE
        ODVoxCore
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_events
        ODVoxAssets
    )

    add_test(NAME odvox-editor COMMAND $<TARGET_FILE:ODVoxEditorTests>)

    # Capture hors ecran de l'editeur (PNG) : l'identite visuelle se juge a
    # l'oeil. Cible manuelle — `cmake --build build --target ODVoxSnapshot`.
    juce_add_console_app(ODVoxSnapshot PRODUCT_NAME "OD Vox Snapshot")

    target_sources(ODVoxSnapshot PRIVATE
        tools/editor_snapshot.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/EqCurveView.cpp
    )

    target_compile_definitions(ODVoxSnapshot PRIVATE
        ODVOX_HAS_EDITOR=1
        $<FILTER:$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>,EXCLUDE,^ODVOX_HAS_EDITOR=.*$>
    )

    target_link_libraries(ODVoxSnapshot PRIVATE
        ODVoxCore
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_events
        ODVoxAssets
    )

    # --- Harnais de stress (le crash FL a la retraite du plugin) -------------
    # L'editeur est cree/detruit en boucle PENDANT que le thread audio
    # continue : tout crash est le bug cherche. Cible manuelle —
    # `cmake --build build --config Debug --target ODVoxStress`.
    juce_add_console_app(ODVoxStress PRODUCT_NAME "OD Vox Stress")

    target_sources(ODVoxStress PRIVATE
        tools/editor_stress.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/EqCurveView.cpp
    )

    target_compile_definitions(ODVoxStress PRIVATE
        ODVOX_HAS_EDITOR=1
        # Le harnais pompe la boucle de messages entre deux cycles (timers,
        # repaints — le rythme reel chez l'hote) : outil console uniquement.
        JUCE_MODAL_LOOPS_PERMITTED=1
        $<FILTER:$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>,EXCLUDE,^ODVOX_HAS_EDITOR=.*$>
    )

    target_link_libraries(ODVoxStress PRIVATE
        ODVoxCore
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_events
        ODVoxAssets
    )

    # --- Reproduction REELLE du crash FL : le plugin installé est hébergé
    # comme chez un DAW (module chargé/déchargé, éditeur natif) en boucle.
    juce_add_console_app(ODVoxHostStress PRODUCT_NAME "OD Vox Host Stress")

    target_sources(ODVoxHostStress PRIVATE
        tools/host_stress.cpp
    )

    target_compile_definitions(ODVoxHostStress PRIVATE
        # Le harnais pompe la boucle de messages (éditeur natif vivant).
        JUCE_MODAL_LOOPS_PERMITTED=1
        # Il héberge un VST3 : il faut le format côté hôte.
        JUCE_PLUGINHOST_VST3=1
    )

    target_link_libraries(ODVoxHostStress PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_events
    )
endif()
