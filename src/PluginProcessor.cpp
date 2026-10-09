#include "PluginProcessor.h"

#include "Parameters.h"

#if defined (ODVOX_HAS_EDITOR) && ODVOX_HAS_EDITOR
  #include "PluginEditor.h"
#endif

#include <cmath>

namespace
{
    /** Position d'un parametre dans le catalogue declare. */
    int indexOf (juce::StringRef id)
    {
        const auto all   = odvox::params::declared();
        const auto wanted = juce::String (id);

        for (int i = 0; i < (int) all.size(); ++i)
            if (wanted == juce::String (all[(size_t) i].id))
                return i;

        return -1;
    }
}

ODVoxAudioProcessor::ODVoxAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ODVox", odvox::params::createLayout())
{
    const auto all = odvox::params::declared();

    targets.assign (all.size(), 0.0f);

    inputGainIndex  = indexOf ("input_gain_db");
    outputGainIndex = indexOf ("output_gain_db");
    outputDcIndex   = indexOf ("output_dc_filter");
    calibrateIndex  = indexOf ("input_calibrate");

    gateAmountIndex    = indexOf ("gate_amount");
    // Seuil, release et range du gate sont des constantes de conception dans
    // `Gate` (rev. suppression du mode Avance) : un seul knob expose.


    // Le coupe-bas n'a plus qu'un interrupteur (F1.5 revu le 2026-09-19) :
    // frequence, pente et profondeur dynamique sont des constantes de conception
    // dans `LowCut`, plus des parametres.
    lowCutAmountIndex  = indexOf ("lowcut_amount");

    compAmountIndex    = indexOf ("comp_amount");


    deessAmountIndex   = indexOf ("deess_amount");

    driveAmountIndex  = indexOf ("drive_amount");
    hqModeIndex = indexOf ("hq_mode");

    // Enables de groupe (amendement UI du 2026-09-29 : la LED de bande est
    // l'interrupteur du groupe).
    fxOnIndex     = indexOf ("fx_on");
    delayOnIndex  = indexOf ("delay_on");
    reverbOnIndex = indexOf ("reverb_on");


    doublerAmountIndex = indexOf ("doubler_amount");
    // Detune (12 cents) et delay (22 ms) du doubler : constantes de conception.
    widthAmountIndex   = indexOf ("width_amount");

    delayAmountIndex  = indexOf ("delay_amount");
    delayTimeIndex    = indexOf ("delay_time");
    delaySyncIndex    = indexOf ("delay_sync");
    delayTimeMsIndex  = indexOf ("delay_time_ms");
    delayDuckingIndex = indexOf ("delay_ducking");
    // Feedback (20 %) et filtre interne du delay : constantes de conception.

    reverbGainIndices[0] = indexOf ("reverb_short_pct");
    reverbGainIndices[1] = indexOf ("reverb_small_pct");
    reverbGainIndices[2] = indexOf ("reverb_big_pct");
    reverbGainIndices[3] = indexOf ("reverb_lush_pct");

    // Les quatre bandes de l'EQ, dans l'ordre du catalogue (F1.6 revu : la bande
    // Low-Mid est retiree). Quatre tableaux ecrits a la main : une inversion
    // accidentelle (Low <-> Mid) serait invisible au compilateur, donc elle est
    // verifiee par un test d'identification des bandes.
    const char* const eqGains[odvox::Eq::kNumBands] = { "eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db" };
    // Frequence, Q et type de chaque bande sont des constantes de conception
    // (ancres mesurees 120/700/1750/10 000 Hz, bandes pitch-suiveuses).

    eqOnIndex = indexOf ("eq_on");

    for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        eqGainIndex[b] = indexOf (eqGains[b]);

    jassert (inputGainIndex >= 0 && outputGainIndex >= 0 && outputDcIndex >= 0
             && calibrateIndex >= 0);
    jassert (gateAmountIndex >= 0);
    jassert (lowCutAmountIndex >= 0);
    jassert (compAmountIndex >= 0);
    jassert (deessAmountIndex >= 0);
    jassert (driveAmountIndex >= 0);
    jassert (doublerAmountIndex >= 0 && widthAmountIndex >= 0);
    jassert (eqOnIndex >= 0);
    jassert (delayAmountIndex >= 0 && delayTimeIndex >= 0 && delaySyncIndex >= 0
             && delayTimeMsIndex >= 0 && delayDuckingIndex >= 0);
    jassert (hqModeIndex >= 0);

    for (int b = 0; b < odvox::Eq::kNumBands; ++b)
        jassert (eqGainIndex[b] >= 0);

    // La regle du macro (US-05 AC5-AC6) : abonne aux parametres pour voir les
    // GESTES, pas seulement les valeurs.
    for (const auto& info : all)
        if (auto* param = apvts.getParameter (info.id))
            param->addListener (this);
}

void ODVoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // La latence du chemin de traitement, reportee a l'hote (F1.13, F2.2) :
    // ZERO en zero-latency. Le mode HQ ajoute celle des filtres half-band du
    // Drive — posee plus bas, apres `drive.prepare`. Un second appel a
    // `setLatencySamples` avec la MEME valeur est sans effet pour l'hote ;
    // c'est le changement de mode HQ, entre deux blocs, qui la met a jour.
    setLatencySamples (0);

    const auto all = odvox::params::declared();

    smoothing.prepare (sampleRate, (int) all.size());

    // Les deux gains portent une rampe plus longue que les autres parametres :
    // AC3 de US-06 exige un fondu d'au moins 50 ms quand la calibration applique
    // un nouveau gain.
    gains.prepare (sampleRate, 2, 0.05);

    // Au demarrage il n'y a rien a lisser : on amorce sur l'etat courant, sinon
    // le premier bloc glisserait depuis zero et produirait un fondu parasite.
    for (size_t i = 0; i < targets.size(); ++i)
        targets[i] = apvts.getRawParameterValue (all[i].id)->load();

    smoothing.snapToTargets (targets.data(), (int) targets.size());

    gainTargets[0] = targets[(size_t) inputGainIndex];
    gainTargets[1] = targets[(size_t) outputGainIndex];
    gains.snapToTargets (gainTargets, 2);

    const int channels = juce::jmax (1, getTotalNumInputChannels());

    gate.prepare (sampleRate, channels);
    lowCut.prepare (sampleRate, channels);
    eq.prepare (sampleRate, channels);
    compressor.prepare (sampleRate, channels);
    deEsser.prepare (sampleRate, channels);
    drive.prepare (sampleRate, channels, samplesPerBlock);   // F2.2 : maxBlock pour les oversampleurs
    image.prepare (sampleRate, channels);
    delay.prepare (sampleRate, channels);
    reverb.prepare (sampleRate, channels);
    dcBlocker.prepare (sampleRate, channels);
    calibrator.prepare (sampleRate);

    spectrumTapBuffer.prepare();
    spectrumAnalyzerEngine.prepare (sampleRate);

    refreshDerivedSettings();

    // Amorce a zero, et non sur la valeur courante du bouton : une action se
    // declenche sur front montant, donc un bouton deja appuye au moment de la
    // preparation doit etre vu comme un front — sinon l'hote qui repreparе le
    // plugin pendant que le bouton est enfonce perd la calibration.
    lastCalibrateValue = 0.0f;
    lastGateReduction  = 0.0f;
    lastInputRms.store (0.0f, std::memory_order_relaxed);
    lastOutputRms.store (0.0f, std::memory_order_relaxed);

    // AMENDEMENT UI (2026-09-29) : le mode HQ est TOUJOURS actif — la decision
    // produit est « Toujours HQ (recommande) », le parametre n'est plus lu
    // (l'editeur ne l'affiche plus). La demande est posee une fois ici, et
    // `refreshDerivedSettings` ne la touche plus : le chemin HQ reste engage,
    // sa latence (filtres half-band) reportee au DAW comme avant. Le Drive
    // inerte (drive_amount 0 %) reste sans latence : le contrat de latence 0
    // aux defauts n'est pas touche.
    drive.setHqRequested (true);
    drive.applyHqMode();
}

bool ODVoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ODVoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Le tempo du playhead, memoire relachee : un dernier tempo connu vaut
    // mieux qu'un retour a la valeur par defaut quand l'hote cesse de publier
    // sa position (lecture arretee) — le delay garde son temps musical.
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (position->getBpm().hasValue())
                lastKnownBpm.store ((float) *position->getBpm(), std::memory_order_relaxed);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    const auto all = odvox::params::declared();

    for (size_t i = 0; i < targets.size(); ++i)
        targets[i] = apvts.getRawParameterValue (all[i].id)->load();

    // Les parametres de module sont consommes une fois par bloc (coefficients de
    // filtre, seuils) : on fait avancer leur lissage d'un bloc entier.
    smoothing.setTargets (targets.data(), (int) targets.size());
    smoothing.advanceBy (numSamples);

    gainTargets[0] = targets[(size_t) inputGainIndex];
    gainTargets[1] = targets[(size_t) outputGainIndex];
    gains.setTargets (gainTargets, 2);

    // --- Bouton de calibration : detection des deux fronts ----------------
    const float calibrateNow = targets[(size_t) calibrateIndex];

    if (calibrateNow > 0.5f && lastCalibrateValue <= 0.5f)
        startCalibration();
    else if (calibrateNow <= 0.5f && lastCalibrateValue > 0.5f)
        cancelCalibration();

    lastCalibrateValue = calibrateNow;

    // --- Gain d'entree, echantillon par echantillon -------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = juce::Decibels::decibelsToGain (gains.getNextValue (0));

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    // --- Calibration : mesure du signal APRES le gain d'entree -------------
    calibrator.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    float deltaDb = 0.0f;

    if (calibrator.consumeResult (deltaDb))
        applyCalibrationGain (deltaDb);

    // --- Gate, low cut, EQ, compresseur, de-esser, puis sortie --------------
    refreshDerivedSettings();

    auto* const* write = buffer.getArrayOfWritePointers();

    // Le detecteur de fondamentale vit dans le module EQ (depuis le
    // 2026-09-20) et consomme le signal du BLOC, pas un echantillon : son
    // analyse (autocorrelation) n'a pas de sens a l'echelle d'un echantillon.
    // Il lit le signal au point d'entree du module EQ — le gate et le low cut
    // n'ont pas encore tourne, ce qui est sans consequence : ils ne retirent
    // rien au-dessus de 70 Hz, ou commence la plage du detecteur.
    eq.beginBlock (write, numChannels, numSamples);
    delay.beginBlock (write, numChannels, numSamples);
    reverb.beginBlock();

    // La chaine est SCINDEE en trois passes (F2.2) : le Drive en mode HQ
    // sature sur echantillonne a partir d'un BLOC (les filtres half-band ne
    // se pilotent pas echantillon par echantillon). La chaine est strictement
    // serie, donc passe 1 (avant-Drive, au fil de l'eau), passe 2 (Drive au
    // bloc) et passe 3 (apres-Drive, au fil de l'eau) sont exactement
    // equivalents a la boucle unique d'avant F2.2.

    // --- Passe 1 : gate, low cut, EQ, compresseur, de-esser -----------------
    for (int i = 0; i < numSamples; ++i)
    {
        gate.processSample (write, i);
        lowCut.processSample (write, i);
        eq.processSample (write, i);
        compressor.processSample (write, i);
        deEsser.processSample (write, i);
    }

    // --- Passe 2 : Drive, au bloc (HQ 4x/2x, ou hors HQ echantillon/ech.) ---
    drive.processBlock (write, numChannels, numSamples);

    // --- Passe 3 : image, delay, reverb, gain de sortie ----------------------
    for (int i = 0; i < numSamples; ++i)
    {
        image.processSample (write, i);
        delay.processSample (write, i);
        reverb.processSample (write, i);
        const float g = juce::Decibels::decibelsToGain (gains.getNextValue (1));

        for (int ch = 0; ch < numChannels; ++ch)
            write[ch][i] *= g;

        // Dernier etage avant la sortie (F1.13) : le filtre DC, apres le gain.
        dcBlocker.processSample (write, i);
    }

    // --- Analyseur de spectre : melange mono pousse dans l'anneau ----------
    // Par tranches de taille FIXE : la taille d'un bloc est dictee par l'hote,
    // donc allouer ici serait une allocation dans le thread audio le jour ou il
    // envoie plus gros que prevu. Le tampon est sur la pile, et le poussoir ne
    // bloque jamais.
    {
        constexpr int kChunk = 512;
        float mono[kChunk];

        for (int start = 0; start < numSamples; start += kChunk)
        {
            const int n = juce::jmin (kChunk, numSamples - start);

            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                    sum += write[ch][start + i];

                mono[i] = sum / (float) numChannels;
            }

            spectrumTapBuffer.push (mono, n);
        }
    }

    lastGateReduction = gate.maxReductionDb();
    lastCompReduction = compressor.currentReductionDb();
    // Le max depuis reset, pas l'instantane : la sifflante est passee quand le
    // bloc finit, l'instantane retombe a 0 et le vumetre ne serait jamais vu.
    lastDeEssReduction = deEsser.maxReductionDb();

    // --- Taps RMS des vumetres IN/OUT (refonte UI, cartes V6) ---------------
    // L'entree est mesuree APRES le gain d'entree (ce que la chaine recoit),
    // la sortie avant de rendre le bloc. RMS pur, atomique relachee : le
    // thread de messages lit une valeur approchee d'une frame, peu importe.
    {
        double inSum = 0.0;
        double outSum = 0.0;
        const int samples = numChannels * numSamples;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* inData = buffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                inSum += (double) inData[i] * (double) inData[i];
        }

        // La sortie vit dans `write`, plus de `buffer.getReadPointer` : le
        // tampon est le meme, mais la boucle de spectre vient de le parcourir
        // via les pointeurs d'ecriture — on lit la source veritable.
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                outSum += (double) write[ch][i] * (double) write[ch][i];

        const float inRms = samples > 0
                                ? (float) std::sqrt (inSum / (double) samples)
                                : 0.0f;
        const float outRms = samples > 0
                                 ? (float) std::sqrt (outSum / (double) samples)
                                 : 0.0f;

        lastInputRms.store (inRms, std::memory_order_relaxed);
        lastOutputRms.store (outRms, std::memory_order_relaxed);
    }
}

// --- Bypass (F1.13) ---------------------------------------------------------

void ODVoxAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    // Le bypass est l'identite audio : l'hote fournit le tampon d'entree,
    // on ne le modifie PAS. Il n'y a rien a recopier (les canaux d'entree SONT
    // les canaux de sortie en in-place) et rien a remettre a zero — surtout
    // pas le buffer, ce qui couperait le signal. Ne pas appeler
    // `processBlock` : le chemin normal inclut gain d'entree, modules, gain
    // de sortie et filtre DC, que le bypass contourne TOUS (AC3 de F1.13 :
    // difference entree/sortie sous −120 dBFS, y compris tous modules a fond).
    //
    // Le silence en sortie quand l'hote n'envoie pas d'entree (cas effet
    // d'insertion sans flux) est deja ce que fait l'identite : pas d'entree,
    // pas de sortie.
    juce::ignoreUnused (buffer);
}

// --- Etat ------------------------------------------------------------------

void ODVoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ODVoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// --- Bus de parametres ------------------------------------------------------

std::map<juce::String, float> ODVoxAudioProcessor::captureParameters() const
{
    std::map<juce::String, float> values;

    for (const auto& info : odvox::params::declared())
        if (auto* param = apvts.getParameter (info.id))
            values[info.id] = param->getValue();

    return values;
}

int ODVoxAudioProcessor::applyParameters (const std::map<juce::String, float>& values,
                                          juce::StringArray& warnings)
{
    int applied = 0;

    for (const auto& [id, value] : values)
    {
        auto* param = apvts.getParameter (id);

        if (param == nullptr || ! odvox::params::isDeclared (id))
        {
            warnings.add ("parametre inconnu ignore : " + id);
            continue;
        }

        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        ++applied;
    }

    return applied;
}

bool ODVoxAudioProcessor::setParameterActual (juce::StringRef id, float actual)
{
    auto* param = apvts.getParameter (id);

    if (param == nullptr || ! odvox::params::isDeclared (id))
        return false;

    param->setValueNotifyingHost (odvox::params::actualToNormalised (id, actual));
    return true;
}

// --- Reglages derives des modules -------------------------------------------

void ODVoxAudioProcessor::refreshDerivedSettings()
{
    // --- EQ 4 bandes, a dynamique relative (F1.6) ---------------------------
    odvox::Eq::Settings eqSettings;
    eqSettings.enabled = smoothing.getCurrent (eqOnIndex) >= 0.5f;

    for (int b = 0; b < odvox::Eq::kNumBands; ++b)
    {
        auto& band = eqSettings.bands[b];
        band.gainDb = smoothing.getCurrent (eqGainIndex[b]);

        // Ancres figurees dans `Eq` (rev. suppression du mode Avance) : chaque
        // bande est pitch-suiveuse autour de son ancre, donc une frequence
        // manuelle contredirait le suivi. Q et types figes pareillement.
        band.freqHz = odvox::Eq::kAnchorHz[b];
        band.q      = odvox::Eq::kAnchorQ[b];
        band.type   = odvox::Eq::kAnchorType[b];
    }

    // L'EQ n'est inerte que si TOUTES les bandes sont neutres (0 dB) et qu'aucune
    // n'est un passe-haut : un passe-haut filtre meme a 0 dB, il ne peut donc pas
    // etre considere comme neutre. C'est ce qui rend la transparence verifiable
    // sans mentir sur l'etat du module.
    bool eqNeutral = true;

    for (const auto& band : eqSettings.bands)
        if (std::abs (band.gainDb) > 0.0001f || band.type == odvox::Eq::kHighPass)
            eqNeutral = false;

    eqSettings.inert = ! eqSettings.enabled || eqNeutral;
    eq.setSettings (eqSettings);

    // UN SEUL curseur public ENGAGEANT : la table mesuree pilote tout (rev du
    // 2026-09-21 : le produit n'expose aucun reglage de comp, et son
    // compresseur est interne et toujours actif). 0 % = transparence par le
    // court-circuit du processeur (amount <= 0), fidele aux autres modules.
    odvox::Compressor::Settings compSettings;
    compSettings.amountPct   = smoothing.getCurrent (compAmountIndex);
    compSettings.enabled     = compSettings.amountPct > 0.0f;
    compressor.setSettings (compSettings);

    // AMENDEMENT UI (2026-09-29) : `fx_on` est le bypass de GROUPE de la
    // bande FX (GATE/DE-ESS/DRIVE/DOUBLER/WIDTH — tranche avec l'utilisateur :
    // bypass, pas valeurs a zero). Un OFF court-circuite chaque module, les
    // reglages sont PRESERVES dans leurs parametres — la LED se rallume et
    // tout revient. Les modules lisent un `amount` annule, jamais leurs
    // parametres.
    const bool fxGroupOn = smoothing.getCurrent (fxOnIndex) >= 0.5f;

    // Seuil (-45 dB), release (150 ms) et range (80 dB) : constantes de
    // conception depuis la suppression du mode Avance — un seul knob expose,
    // comme dans la cible. Le gate derive son inertie de son gain : bypass =
    // amount 0, la valeur du knob reste intacte dans son parametre.
    odvox::Gate::Settings gateSettings;   // defauts du struct = design fige
    gateSettings.amountPct = fxGroupOn ? smoothing.getCurrent (gateAmountIndex) : 0.0f;
    gate.setSettings (gateSettings);

    // Coupe-bas FIGE (F1.5 revu le 2026-09-19) : un interrupteur, et rien
    // d'autre. `lowcut_amount` est un booleen depuis la revision du catalogue.
    // Hors du bypass de groupe : la bande FX ne couvre pas le nettoyage.
    odvox::LowCut::Settings lowCutSettings;
    lowCutSettings.enabled = smoothing.getCurrent (lowCutAmountIndex) >= 0.5f;
    lowCut.setSettings (lowCutSettings);

    // UN SEUL curseur public : croisement fixe 5 kHz, plosives lies au
    // curseur (cap 20 %), listen supprime — simplicite de la cible.
    odvox::DeEsser::Settings deessSettings;
    deessSettings.amount01 = smoothing.getCurrent (deessAmountIndex) * 0.01f;
    // Transparence bit-exacte (AC2 de US-02) : l'inertie est la condition de
    // court-circuit complet du module. Le vumetre expose alors toujours 0.
    deessSettings.inert    = ! fxGroupOn || deessSettings.amount01 <= 0.0f;
    deEsser.setSettings (deessSettings);

    // UN SEUL curseur public : engin fige sur Console (l'engin retenu),
    // mix interne — le `drive` est son seul
    // reglage (rev du 2026-09-21).
    odvox::Drive::Settings driveSettings;
    driveSettings.amount01 = smoothing.getCurrent (driveAmountIndex) * 0.01f;
    // Court-circuit complet a 0 % : transparence bit-exacte aux defauts
    // (AC2 de US-02) — et bypass de groupe `fx_on` (amendement UI).
    driveSettings.inert    = ! fxGroupOn || driveSettings.amount01 <= 0.0f;
    drive.setSettings (driveSettings);

    // Mode haute qualite (F2.2) : AMENDEMENT UI du 2026-09-29 — le parametre
    // n'est plus lu, la demande est figee a ON (posee au prepareToPlay). Le
    // `applyHqMode` reste appele ici : il ne fait alors qu'actualiser la
    // latence du chemin actif. Aucun report si la valeur n'a pas change.
    drive.setHqRequested (true);
    drive.applyHqMode();

    {
        const float hqLatency = drive.hqLatencySamples();
        const int reported = (int) std::lround (hqLatency);

        if (reported != getLatencySamples())
            setLatencySamples (reported);
    }

    // --- Image : doubler puis largeur (F1.10) ------------------------------
    odvox::Image::Settings imageSettings;
    imageSettings.amount01 = smoothing.getCurrent (doublerAmountIndex) * 0.01f;
    // Doubler fige : detune 12 cents, delay 22 ms (defauts du struct, rev.
    // suppression du mode Avance).
    imageSettings.width       = smoothing.getCurrent (widthAmountIndex) * 0.01f;
    imageSettings.monoBassHz  = 120.0f;   // constante de conception (§3.4, voir F1.10)
    // Court-circuit du module ENTIER : doubler a 0 % ET largeur a 100 % (neutre),
    // ou bypass de groupe `fx_on` (amendement UI).
    // Toute deviation de l'un ou de l'autre active le traitement.
    imageSettings.inert       = ! fxGroupOn
                                || (imageSettings.amount01 <= 0.0f
                                    && std::abs (imageSettings.width - 1.0f) <= 0.0001f);
    image.setSettings (imageSettings);

    // --- Delay ping-pong (F1.11) --------------------------------------------
    // La Settings est un MEMBRE (rev. suppression du mode Avance) : le
    // rafraichissement met a jour les champs pilotés par des parametres (et le
    // tempo), et PRESERVE feedback + filtre — des constantes de conception que
    // seul le module possede, ce qui permet aussi aux tests de les exercer.
    delaySettings.amount01 = smoothing.getCurrent (delayAmountIndex) * 0.01f;
    delaySettings.division = juce::jlimit (0, odvox::Delay::kNumDivisions - 1,
                                           (int) std::lround (smoothing.getCurrent (delayTimeIndex)));
    delaySettings.sync     = smoothing.getCurrent (delaySyncIndex) >= 0.5f;
    delaySettings.freeMs   = smoothing.getCurrent (delayTimeMsIndex);
    delaySettings.duck01   = smoothing.getCurrent (delayDuckingIndex) * 0.01f;

    // Le tempo vient du playhead de l'hote ; hors lecture (ou hote sans
    // position), on garde le dernier connu — amorce sur le tempo par defaut
    // des DAW (120 BPM, le tempo conventionnel).
    delaySettings.bpm        = lastKnownBpm.load (std::memory_order_relaxed);

    // Court-circuit complet a 0 % : bit-exact (AC de transparence de la chaine),
    // et `delay_on` : l'interrupteur de la bande DELAY (amendement UI du
    // 2026-09-29 — la LED de bande pilote un enable declare, qui survit a
    // l'A/B, aux presets et a la session).
    delaySettings.inert      = ! (smoothing.getCurrent (delayOnIndex) >= 0.5f)
                               || delaySettings.amount01 <= 0.0f;
    delay.setSettings (delaySettings);

    // --- Reverb a reseau propre (F1.12) --------------------------------------
    // QUATRE curseurs publics, un par moteur : les moteurs sont toujours
    // cumules, l'utilisateur dose chaque couleur. Le pre-delay reste
    // preregle en interne (20 ms).
    odvox::Reverb::Settings reverbSettings;
    for (int m = 0; m < odvox::Reverb::kNumEngines; ++m)
        reverbSettings.gains[m] = smoothing.getCurrent (reverbGainIndices[m]) * 0.01f;
    // `reverb_on` : l'interrupteur de la bande REVERB (amendement UI du
    // 2026-09-29). Sinon, le module derive seul son inertie (tout a 0).
    reverbSettings.inert = ! (smoothing.getCurrent (reverbOnIndex) >= 0.5f);
    reverb.setSettings (reverbSettings);

    // --- Filtre DC de sortie (F1.13) ----------------------------------------
    // Un interrupteur, et rien d'autre : la frequence (10 Hz) est une constante
    // de conception dans `DcBlocker`, comme celle du low cut. Hors du bypass de
    // groupe : la bande FX ne couvre pas la Sortie. Le filtre est le dernier
    // etage, apres le gain de sortie.
    dcBlocker.setEnabled (smoothing.getCurrent (outputDcIndex) >= 0.5f);
}

// --- Calibration du gain d'entree (F1.3) ------------------------------------

void ODVoxAudioProcessor::startCalibration()
{
    if (calibrator.isRunning())
        return;

    calibrator.start();
}

void ODVoxAudioProcessor::cancelCalibration()
{
    calibrator.cancel();
}

// --- Regle du macro (US-05 AC5-AC6) -----------------------------------------

void ODVoxAudioProcessor::parameterValueChanged (int, float)
{
    // Les valeurs seules ne changent pas l'etat : une automation du curseur ne
    // doit pas faire sortir du mode `Custom` (AC5).
}

void ODVoxAudioProcessor::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    // Le mode Custom du compresseur est supprime (rev du 2026-09-21 : la
    // le produit n'expose aucun reglage de comp) — l'etat reste pour les
    // recherches futures mais ne bascule plus jamais.
    juce::ignoreUnused (gestureIsStarting);
    lastGestureIndex.store (parameterIndex);
}

void ODVoxAudioProcessor::applyCalibrationGain (float deltaDb)
{
    const auto all = odvox::params::declared();

    if (! juce::isPositiveAndBelow (inputGainIndex, (int) all.size()))
        return;

    const auto& info = all[(size_t) inputGainIndex];
    auto* param = apvts.getParameter (info.id);

    if (param == nullptr)
        return;

    const float current = odvox::params::normalisedToActual (info.id, param->getValue());
    const float wanted  = juce::jlimit (info.min, info.max, current + deltaDb);

    // L'application passe par la rampe de 50 ms du lissage des gains, ce qui
    // satisfait AC3 de US-06 : aucun echantillon au-dessus de 0 dBFS pendant
    // l'application.
    setParameterActual (info.id, wanted);

    // Le bouton retombe, sinon il ne pourrait pas etre redeclenche.
    if (calibrateIndex >= 0)
        if (auto* button = apvts.getParameter (all[(size_t) calibrateIndex].id))
            button->setValueNotifyingHost (0.0f);
}

// --- Presets ----------------------------------------------------------------

int ODVoxAudioProcessor::getNumFactoryPresets() const
{
    return (int) odvox::factoryPresets().size();
}

juce::String ODVoxAudioProcessor::getFactoryPresetName (int index) const
{
    const auto& presets = odvox::factoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return {};

    return presets[(size_t) index].name;
}

bool ODVoxAudioProcessor::loadFactoryPreset (int index)
{
    const auto& presets = odvox::factoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return false;

    juce::StringArray warnings;
    return loadPreset (presets[(size_t) index], warnings);
}

bool ODVoxAudioProcessor::loadPreset (const odvox::Preset& preset, juce::StringArray& warnings)
{
    // Un preset d'usine embarque peut referencer un parametre qui n'est plus
    // declare : on le filtre avant d'appliquer, pour ne jamais ecrire ailleurs
    // que dans les parametres declares (AC3 de US-01).
    const auto filtered = odvox::filterToDeclared (preset.parameters, warnings);

    // Charger un preset ne traverse aucun geste : l'etat Macro/Custom est
    // laisse tel quel, seuls les gestes utilisateur le changent.
    applyParameters (filtered, warnings);
    return true;
}

odvox::Preset ODVoxAudioProcessor::capturePreset (const juce::String& name) const
{
    odvox::Preset p;
    p.name       = name;
    p.author     = "OD Audio";
    p.category   = "user";
    p.mode       = "essential";
    p.parameters = captureParameters();
    return p;
}

// --- Emplacements A/B -------------------------------------------------------

void ODVoxAudioProcessor::captureSlot (int slot)
{
    snapshots.capture (slot, captureParameters());
}

void ODVoxAudioProcessor::recallSlot (int slot)
{
    if (! snapshots.has (slot))
        return;

    juce::StringArray warnings;
    applyParameters (snapshots.get (slot), warnings);
    activeSlot = slot;
}

void ODVoxAudioProcessor::copySlot (int from, int to)
{
    snapshots.copy (from, to);
}

bool ODVoxAudioProcessor::slotHasContent (int slot) const
{
    return snapshots.has (slot);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ODVoxAudioProcessor();
}

// --- Editeur (F1.5b) ---------------------------------------------------------
// Compiled seulement si la cible definit ODVOX_HAS_EDITOR=1 : la cible de tests
// compile aussi ce fichier, et on ne veut pas y embarquer l'UI.
#if defined (ODVOX_HAS_EDITOR) && ODVOX_HAS_EDITOR

juce::AudioProcessorEditor* ODVoxAudioProcessor::createEditor()
{
    return new ODVoxAudioProcessorEditor (*this);
}

#else

juce::AudioProcessorEditor* ODVoxAudioProcessor::createEditor()
{
    return nullptr;
}

#endif
