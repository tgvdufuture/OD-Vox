# PRD — OD Vox — chaîne vocale tout-en-un

**Version:** 1.0
**Date:** 2026-09-18
**Status:** Draft

---

## 0. Executive Summary

Un plugin audio unique qui transforme une prise de voix brute en voix mixée, avec deux
niveaux d'accès : un **Mode Essentiel** où l'utilisateur charge un preset et ajuste un
curseur par module, et un **Mode Avancé** qui débloque les réglages réels de chaque
module. Le produit vise un besoin que **l'existant** sert mal, par deux partis pris :
**tous les réglages « dangereux » y sont figés en interne** (compresseur à un curseur,
coupe-bas on/off, EQ à fréquences fixes) et **cinq fonctions attendues y sont
absentes** (gate, de-reverb, correction de justesse, largeur stéréo, oversampling) —
dont **quatre seulement** sont reprises ici : la correction de justesse a été retirée
du périmètre le 2026-09-19 (cf. US-07).

Le succès, c'est un utilisateur qui obtient un rendu vocal qu'il juge diffusable
**sans jamais ouvrir le Mode Avancé**, tout en sachant que le Mode Avancé existe quand
il en a besoin.

Primary success metric:
- **80 % des testeurs obtiennent un rendu vocal jugé diffusable en ≤ 2 minutes sans ouvrir le Mode Avancé.**

---

## 1. Strategic Context

### 1.1 Problem Statement

> **Convention de lecture.** « **L'existant** » désigne la catégorie des plugins de
> chaîne vocale « tout-en-un » à un curseur par module, dont les réglages fins sont
> figés en interne. Aucun produit n'y est nommé : les chiffres cités dans ce document
> sont des **repères de conception retenus pour OD Vox**, pas la description d'un
> logiciel tiers.

- **Situation actuelle** : pour mixer une voix, il faut enchaîner 6 à 10 plugins
  (gate, EQ, compresseur, de-esser, saturation, delay, reverb, limiteur). Les plugins
  « tout-en-un » existants résolvent ce problème en **figeant les réglages en
  interne** : leur `Comp` est un unique curseur 0–100 % (seuil, ratio, attaque et
  release non réglables) et leur `Low Cut` un interrupteur booléen sans réglage de
  fréquence. Ces deux sections, ajoutées après coup, ne sont même utilisées par aucun
  de leurs presets d'usine.
- **Point de douleur utilisateur** : l'utilisateur doit choisir entre un plugin qui
  sonne tout de suite mais qu'il ne peut pas corriger, et une chaîne de plugins qu'il
  ne maîtrise pas. Il n'existe pas d'outil qui fasse **les deux** dans la même fenêtre.
- **Pourquoi maintenant** : deux fonctions sont devenues incontournables sur le
  marché de la voix (réduction de bruit de pièce, contrôle de largeur stéréo) et
  **aucune n'est proposée par l'existant**. Le créneau est ouvert.

### 1.2 Goals

- **G1** : 80 % des testeurs obtiennent un rendu jugé diffusable en ≤ 2 minutes,
  sans ouvrir le Mode Avancé.
- **G2** : 100 % des modules disposent d'un Mode Avancé qui expose les réglages
  réels (compresseur : seuil, ratio, attaque, release, knee, makeup ; coupe-bas :
  fréquence et pente).
- **G3** : la chaîne complète tourne à **≤ 6 % d'un cœur** à 48 kHz stéréo sur un
  desktop de référence (Ryzen 5 5600 / Core i5-12400), Mode HQ 4× inclus à ≤ 18 %.
- **G4** : l'automation de tout paramètre ne produit **aucun clic mesurable**
  (aucune discontinuité > −60 dBFS sur un sweep de paramètre).

### 1.3 Non-Goals

Cette phase ne va PAS :

- Copier du code, des IR de reverb, des échantillons, des assets graphiques, des
  presets, un nom ou une identité visuelle appartenant à un produit tiers.
- Mettre en place une authentification en ligne, un DRM ou une expiration de démo
  (décision de modèle économique en attente, cf. §8).
- Implémenter une **correction de justesse**, sous aucune forme : ni en temps réel,
  ni en édition note par note à la manière d'un éditeur hors-ligne (pas de
  piano-roll de pitch). *Décision du 2026-09-19* : le module avait été déclaré au
  catalogue (F1.2) puis jamais implémenté — trois curseurs inertes valaient moins
  que pas de curseur du tout. Détail en US-07 et §3.4.
- Implémenter un compresseur multibande ni une dé-mixage par source.
- Implémenter un rendu par apprentissage automatique (pas de modèle neuronal).
- Supporter le format **AAX** dans la version 1 (nécessite une licence Avid).
- Fournir une version **standalone** dans la version 1.
- Implémenter une limiteur de sortie avec lookahead.

---

## 2. Users & Context

### 2.1 Primary Persona

- **Rôle** : beatmaker / producteur indépendant qui enregistre ses propres voix.
- **Niveau technique** : maîtrise son DAW et la composition ; sait qu'un compresseur
  existe ; ne sait pas régler un ratio ni un temps d'attaque, et ne veut pas
  apprendre.
- **Contexte** : enregistre dans une pièce non traitée, micro dynamique ou à
  condensateur d'entrée de gamme, voix rap/pop, sessions longues et répétées.
- **Outils/environnement** : FL Studio ou équivalent, Windows 10/11 ou macOS,
  écoute au casque, pas de traitement acoustique.
- **Job-to-be-done** : « obtenir une voix qui tient dans le beat, sans y passer la
  soirée et sans casser ce que j'ai fait ».

### 2.2 Secondary Personas

- **Créateur de contenu / streamer** : micro dynamique (SM7b/SM58), bruit de fond
  constant, besoin de constance du niveau et de silence propre entre les phrases.
- **Chanteur home-studio** : leads mélodiques, pièce non traitée, besoin de
  réduction de réverbération.

### 2.3 User Stories

| ID | User Story | Priority |
|----|------------|----------|
| US-01 | As a beatmaker, I want to load a factory preset matching my microphone and genre, so that my raw vocal becomes mix-ready without me understanding the chain. | P0 |
| US-02 | As a beatmaker, I want to adjust one amount knob per module, so that I can move a preset away from its default without breaking the sound. | P0 |
| US-03 | As a home-studio singer, I want a gate that silences my room noise between phrases, so that breaths and background hum do not sit under the vocal. | P0 |
| US-04 | As a home-studio singer, I want to see a spectrum behind the EQ curve and drag its bands, so that I can remove the resonances I can hear. | P0 |
| US-05 | As a producer, I want the compressor to expose threshold, ratio, attack, release and knee when I need them, so that I can control dynamics beyond the one-knob macro. | P0 |
| US-06 | As a streamer, I want the input gain to be set automatically from my voice, so that my level stays constant without gain-staging knowledge. | P0 |
| US-07 | As a singer, I want real-time pitch correction with adjustable strength and speed, so that my takes are in tune without a separate plugin. | **Retirée** (2026-09-19) |
| US-08 | As a producer, I want a stereo width control with bass mono-ing, so that my vocal sits wide in the mix and still collapses safely in mono. | P0 |
| US-09 | As a producer, I want a switchable oversampled mode, so that the drive stage does not produce audible aliasing on high notes. | P0 |
| US-10 | As a producer, I want a de-esser that treats sibilance and plosives with separate amounts, so that harsh "S" and "P" are both tamed. | P0 |
| US-11 | As a session singer, I want A/B snapshots, so that I can compare two settings without losing either. | P0 |
| US-12 | As a producer, I want the plugin to report its latency, so that I can compensate it in my DAW. | P0 |
| ~~US-14~~ | ~~As a home-studio singer, I want de-reverb...~~ | **retirée le 2026-09-22** : fonction plus au périmètre (avec le lot F2.3) |
| US-15 | As a producer, I want a delay that ducks under the vocal, so that words stay intelligible. | P1 |
| US-16 | As a streamer, I want LUFS and true-peak metering, so that I can hit my platform's loudness target. | P1 |
| US-17 | As a producer, I want to import my own reverb impulse responses, so that I can use spaces I already own. | P2 |
| US-18 | As a producer, I want AU, CLAP and AAX builds, so that I can use the plugin in every host I own. | P2 |

### 2.4 User Story Acceptance Criteria

#### US-01 — Preset d'usine immédiat

- [ ] AC1 : un preset d'usine se charge en un clic depuis un menu de presets et
  l'état audio change en moins de 100 ms.
- [ ] AC2 : au moins **20** presets d'usine sont livrés, dont 6 indexés par type
  de micro (au moins : SM7b, SM58, micro à condensateur, NT1).
- [ ] AC3 : charger un preset ne modifie **aucun** paramètre hors de la liste
  déclarée dans le fichier preset (vérifié par comparaison exhaustive d'état).

#### US-02 — Un curseur par module

- [ ] AC1 : en Mode Essentiel, chaque module audio expose exactement un curseur
  d'intensité, nommé, avec une valeur affichée en %.
- [ ] AC2 : mettre chaque curseur à 0 % rend le module strictement transparent
  (erreur de recopie ≤ −120 dBFS sur un test en différence).
- [ ] AC3 : aucun curseur du Mode Essentiel ne peut produire un écrêtage interne
  (aucune sortie > 0 dBFS avec un signal d'entrée à −12 dBFS crête).

#### US-03 — Gate

- [ ] AC1 : avec un seuil réglé au-dessus du bruit de fond et un signal de test
  alternant silence et voix, le niveau de sortie pendant les silences est au moins
  **60 dB** sous le niveau pendant la voix.
- [ ] AC2 : l'attaque du gate laisse passer la première consonne : sur un mot
  commençant par une plosive, la première période non nulle n'est pas tronquée
  (aucune atténuation sur les 20 premières ms du mot).
- [ ] AC3 : le paramètre `gate_range_db` limite la réduction maximale (mesuré : la
  réduction en silence égale la valeur réglée, tolérance ±1 dB).

#### US-04 — EQ interactif avec spectre

- [x] AC1 : 4 bandes minimum, chacune avec type (cloche / shelf bas / shelf haut /
  passe-haut), fréquence, gain et Q réglables au glisser-déposer.
  *(DSP vérifié au F1.6, **révisé le 2026-09-20** : **quatre** bandes, la structure
  retenue, chacune avec ses quatre contrôles — 12 paramètres `Advanced` — chaque
  contrôle atteignant réellement son filtre. La cinquième bande (Low-Mid), notre
  ajout, est retirée : elle recouvrait la zone déjà tenue par le Mid, et la sobriété
  commence par ne pas inventer de bande.
  Interface vérifiée au **F1.6c** : un glisser-déposer écrit fréquence ET gain, la
  molette double ou divise le Q (butoir au catalogue), un menu contextuel pose le
  type, et le double-clic rend les défauts du catalogue. Exercé avec de **vrais
  événements souris** envoyés au composant dessiné hors écran, et par mutation :
  débrancher `mouseDrag` fait échouer le test du geste.)*
- [x] AC2 : la réponse mesurée correspond à la courbe affichée à **±0.5 dB** entre
  40 Hz et 16 kHz (mode cloche, gain ±12 dB).
  **Précision du 2026-09-20** : la courbe affichée est la réponse **à plein
  régime** — c'est aussi ce que mesure AC1 du harnais et ce que mesurent AC1/AC2
  des tests unitaires, à −23 dBFS RMS. En dessous de −25 dBFS, la **dynamique
  relative** (AC4) raccourcit la réponse : toutes les bandes sont multipliées par
  la même fraction, donc la réponse reste **proportionnelle** à la courbe, et le
  critère se mesure là où la fraction vaut 1.
  *(Mesuré sur 14 fréquences de 40 Hz à 15,8 kHz, cloches de +12 à −12 dB : pire
  écart **0,001 dB** au harnais, ≤ 0,5 dB aux tests unitaires. Depuis le F1.6c, la
  courbe affichée vient de `Eq::responseDb`, qui interroge **les mêmes fabriques de
  coefficients** que les filtres qui traitent le son — le trace et l'entendu ne
  peuvent pas diverger par construction. Vérifié après GESTE aussi : la mesure suit
  la courbe à ≤ 0,5 dB sur 10 fréquences dont 16 kHz (là où le shelf Air atteint son
  plateau).)*
- [x] AC3 : l'analyseur de spectre affiche une résolution d'au moins 1/12 d'octave
  et se rafraîchit à ≥ 20 images/s sans provoquer de décrochage audio (aucun
  dépassement de bloc détecté sur 10 min d'utilisation).
  *(Livré au **F1.6c**, mesuré : bandes d'analyse de 1/12 d'octave **vérifiées sur
  leurs bornes réelles** (2^(1/12) à 1e-4 près) ; FFT 16384 points, fenêtre de Hann,
  recouvrement 87,5 % → **23 mises à jour de données neuves par seconde à 48 kHz**
  (21 à 44,1 ; 47 à 96), mesurées sur un flux réel et non déduites de la constante ;
  **10 minutes à 30 images/s sans un seul échantillon abandonné** (anneau sans
  verrou : le thread audio ne bloque jamais, il compte les abandons — le critère
  « aucun dépassement de bloc » est donc mesurable) ; la FFT vit dans le thread de
  messages (§7) ; calibration vérifiée : un sinus à −12 dBFS se lit à −12 dB et son
  pic tombe dans la bande de 1/12 d'octave qui le contient. **Limite consignée** :
  sous ~50 Hz, une bande de 1/12 d'octave (1,16 Hz à 20 Hz) est plus étroite que la
  raie de la FFT (2,93 Hz à 48 kHz) — c'est la borne physique de toute fenêtre
  finie, pas un choix ; l'analyseur y retombe sur la raie la plus proche.)*

- [x] AC4 (**ajouté le 2026-09-20**) : chaque bande porte la dynamique relative —
  **dynamique relative pilotée par le niveau du programme**, accrochage harmonique
  borné, AutoGain d'étage. Le gain **appliqué** est une fraction du gain **réglé**,
  la **même** pour les quatre bandes : sous le plancher (−55 dBFS) le module ne
  fait rien, au-dessus du plein (−25 dBFS) il applique les réglages entiers.
  *(Mesuré au signal, sur une **sonde à deux raies** — la seconde raie sert de
  témoin, le makeup étant large bande : 0,00 dB à −60 dBFS, **3,00 dB** à
  −40 dBFS pour un réglage de +6 dB, **6,00 dB** à −20 dBFS. La mesure à deux
  raies est indispensable : deux rendus séparés verraient deux makeups différents
  et lisaient 3,96 dB là où la bande en appliquait 3,00.)*
- [x] AC5 (**ajouté le 2026-09-20**) : l'**AutoGain d'étage** ne compense que
  l'effet de la dynamique relative — jamais les réglages de l'utilisateur — il est
  pondéré par la sonie du programme et par l'engagement, et il vaut donc **0 dB à
  plein régime** comme **sous le plancher**.
  *(Mesuré hors de la bande, à −20 / −40 / −60 dBFS : **0,00 / 0,73 / 0,00 dB**.
  Deux réglages successifs ont été corrigés parce que mesurés faux : le makeup
  sortait **+3,87 dB** de gain large bande sous le plancher, où l'EQ ne filtre
  rien du tout — un niveau qui bouge sans que rien n'agisse est un pompage, pas un
  AutoGain — puis encore **+3,38 dB** après correction du seul dénominateur.)*

#### US-05 — Compresseur **à UN SEUL curseur (rev 2026-09-21)**

*L'existant n'expose AUCUN réglage de compresseur. L'US-05 « Mode Avancé » d'origine
(threshold/ratio/release/makeup/autogain/Custom) est **retirée** — les paramètres
`comp_threshold_db`, `comp_ratio`, `comp_release_ms`, `comp_makeup_db`,
`comp_autogain`, `comp_on` sortent du catalogue. Un seul curseur `comp_amount`
pilote la table macro retenue (seuil fixe −50 dBFS, ratio et make-up croissants),
le curseur ENGAGE le module (0 % = court-circuit), et le make-up est amorcé sur
l'attaque du détecteur (le transitoire +20 dBFS mesuré le 2026-09-21 est corrigé).
Même logique pour la saturation : `drive_amount` seul, saveur **figée sur
Console** (l'engin retenu), mix interne à 100 %.*

- [x] AC1 (rev) : la table macro reproduit les cinq points retenus
  (±0,8 dB) et le make-up est croissant de 0 à 100 %.
- [x] AC2 (rev) : le vumètre rapporte la GR de compression de table.
- [x] AC3 (rev) : `comp_amount` 0 % est transparent au bit près (le curseur
  engage le module).
- [x] AC4 (rev) : à 100 % sur un sinus fort, la crête de sortie reste bornée
  (< 2,0) — l'amorce du make-up supprime le transitoire d'attaque.

#### US-06 — Gain d'entrée automatique

- [ ] AC1 : après 5 secondes d'entrée vocale, le gain appliqué place le niveau
  crête mesuré entre −12 et −6 dBFS, dans au moins 90 % des essais sur un corpus
  de 10 prises de niveaux différents.
- [ ] AC2 : la calibration est annulable en cours de mesure et l'annulation laisse
  le gain d'entrée inchangé.
- [ ] AC3 : la calibration ne produit aucun échantillon > 0 dBFS pendant son
  application (fondu de gain ≥ 50 ms).

#### US-07 — Correction de justesse — **RETIRÉE (2026-09-19)**

Cette story est **abandonnée**, pas reportée : OD Vox ne propose pas de correction
 de justesse, sous aucune forme.

Motif : le module avait été déclaré au catalogue au F1.2 (`pitch_amount`,
`pitch_speed_ms`, `pitch_key`, plus `pitch_scale` et `pitch_tolerance_cents` restés
au PRD) puis **jamais implémenté** — aucun module DSP, aucune lecture de ces
paramètres dans la chaîne. Il en résultait **trois curseurs inertes à l'écran**, ce
qui est pire qu'un manque : une interface qui promet un traitement qu'elle n'applique
pas. Le PRD plaçait déjà la fonction en Phase 2 (F2.1) en la jugeant trop risquée
pour le gain attendu — le warble et les clics d'un pitch-shifter temps réel sont le
risque n°1 du produit (§7). Décision : **supprimer plutôt que laisser une promesse
non tenue.**

Les critères AC1–AC4 sont donc sans objet, et l'identifiant `pitch_amount` **ne sera
pas réutilisé** : il a été exposé par une version publiée du catalogue, et le
réutiliser pour autre chose casserait ce contrat (§3.2).

#### US-08 — Largeur stéréo

- [x] AC1 : `width` à 100 % laisse le signal strictement inchangé (différence
  ≤ −120 dBFS) ; `width` à 0 % rend le signal strictement mono (corrélation L/R = 1.0).
  Mesuré : **identique au bit près** à 100 % (module court-circuité) ; corrélation
  à 0 % = **1,0000**.
- [x] AC2 : `width_mono_bass_freq` garantit qu'en dessous de la fréquence réglée,
  les canaux sont identiques (corrélation ≥ 0.999). Mesuré : corrélation à 30 Hz
  = **0,99993** à 200 % de largeur. La sonde est placée une octave et demie
  **sous** la coupure de 120 Hz : au coin, aucune pente finie ne peut tenir
  0,999 — c'est une propriété du filtre, pas un défaut du module. C'est le
  passe-haut Linkwitz-Riley 4 du side qui l'assure, et le PRD admet ici la même
  lecture qu'au F1.6c : un critère chiffré vaut par le **point de mesure** qui
  l'accompagne.
- [x] AC3 : le contrôle de largeur ne crée aucune fréquence inaudible ajoutée :
  aucune composante > −80 dBFS entre 0 et 20 Hz. Mesuré : pire ajout **−113,8
  dBFS** à 11 Hz (onde fenêtrée de Hann : une corrélation rectangulaire sur une
  longueur finie lit les lobes de la sonde elle-même, −64 dB à 17 Hz, et
  noierait le plancher exigé).
- [x] Le **doubler** (module Image, même lot) : à 100 %, la matière ajoutée est
  décorrélée entre L et R — corrélation de (sortie 100 % − sortie 0 %) = **−0,345**,
  et l'énergie quitte la raie exacte de la sonde (−1,84 dB à 220 Hz).

Constraints:
- `width_mono_bass_freq` est une **constante de conception** de cette version
  (120 Hz) ; le PRD le reporte en paramètre au Mode Avancé en Phase 2. US-08 le
  nomme déjà pour poser le contrat, pas pour livrer le contrôle.

#### US-09 — Mode haute qualité / oversampling *(livré F2.2 le 2026-09-22)*

- [x] AC1 : en Mode HQ 4×, avec une entrée sinusoïdale à 8 kHz et `drive_amount`
  à 80 %, **aucun repliement** au-dessus de −80 dBFS n'est mesuré dans la bande
  0–20 kHz.
- [x] AC2 : en mode zero-latency (oversampling désactivé), la latence totale
  reportée à l'hôte est **0 échantillon**.
- [x] AC3 : le basculement HQ on/off ne provoque pas de clic (variation de niveau
  < 1 dB sur 20 ms autour de la transition).

#### US-10 — De-esser sifflantes + plosives (amendé le 2026-09-21 : UN SEUL curseur)

- [x] AC1 : sur un signal contenant des « S » à 8 kHz posés sur un grave de
  plosive à 100 Hz, le curseur unique réduit la bande sifflante ; la bande
  grave ne voit que le shelf de plosives LIÉ au curseur
  (`min(0,20 ; 0,6·amount01)` — la valeur qu'appliquaient les presets), jamais
  la réduction dynamique. Vérifié au signal C++ ET au niveau hôte.
- [x] AC2 (amendé) : le mode « listen » est supprimé — absent de l'existant,
  dont la simplicité d'emploi fait partie de la fidélité. L'isolement de bande
  reste vérifié dans les tests C++ (les voies HP4/LP4 recombinées en
  passe-tout exact).
- [x] AC3 : sur une voix continue sans sifflante, `deess_amount` à 100 % réduit le
  niveau total de moins de 0.5 dB. Mesuré : 0,003 dB sur une voix à trois
  harmoniques (220/440/880 Hz), et le vumètre reste à 0.

#### US-11 — Snapshots A/B

- [ ] AC1 : deux emplacements A et B coexistent ; basculer de A à B et retour à A
  restitue exactement l'état de départ (comparaison exhaustive des paramètres
  alors déclarés).
- [ ] AC2 : le basculement A/B ne produit aucune **discontinuité**. Mesuré sur
  un signal continu — où la sortie *est* la courbe de gain — le plus grand écart
  entre deux échantillons consécutifs pendant la transition reste **< 0,2**, et la
  cible est atteinte en moins de 25 ms. Repère : sans lissage, la même bascule de
  48 dB vaudrait ~15,8.
  > **Correction du 2026-09-18.** La formulation initiale était « variation < 1 dB
  > sur 20 ms », et elle était **insatisfiable** : deux emplacements qui diffèrent
  > de 48 dB ne peuvent pas ne varier que de 1 dB sur 20 ms. Ce qu'il faut
  > interdire, c'est la *discontinuité*, pas la variation. Le critère a été
  > reformulé pour être mesurable, et il est vérifié par un test de signal.

#### US-12 — Latence reportée

- [x] AC1 : la latence réellement mesurée par corrélation (impulsion en entrée
  contre impulsion en sortie) est égale à la latence reportée à l'hôte, à
  ±1 échantillon près, dans les modes zero-latency et HQ. *(Vérifié le
  2026-09-22 en mode zero-latency : latence reportée 0, impulsion au centre
  retrouvée à ±0 échantillon. Rev. F2.2 le même jour : invariant généralisé —
  le chemin HQ reporte 4 échantillons (half-band 4×) et le chemin sec mesuré
  revient à entrée + report, y compris sous la PDC d'un hôte réel.)*
- [x] AC2 : la latence affichée dans l'interface est en échantillons et en ms.
  *(Barre supérieure de l'éditeur, rafraîchie au timer : « 0 smp / 0,0 ms ».)*

#### US-13 — Module « mise en forme dynamique » : EQ à dynamique relative et suivi de pitch

**Statut : retirée le 2026-09-20, décision produit.** L'étage de mise en forme
autonome (5 zones à 0,67 / 2 / 3,5 / 6 / 10 × f0 + AutoGain perceptuel, curseur
`lift_amount`) a été supprimé : son action recouvrait celle de l'EQ à dynamique
relative, et l'EQ porte déjà la logique qui compte — dynamique relative pilotée par
le programme (AC4 de US-04), accrochage harmonique borné, AutoGain d'étage. Le
**suivi de pitch** est conservé : le détecteur de fondamentale, unique du produit,
vit désormais dans le module EQ et alimente l'accrochage des bandes.

Les AC ci-dessous sont archivés pour mémoire — ils décrivaient l'étage supprimé,
tous vérifiés au jour de son retrait :

<details><summary>AC archivés de l'étage supprimé</summary>

- ~~AC1~~ : curseur unique, transparence au bit près à 0 %.
- ~~AC2~~ : réduction de la zone boue croissant avec le niveau (0,00 dB à −40 dBFS,
  7,29 dB à −6 dBFS, sonde à 300 Hz).
- ~~AC3~~ : suivi de pitch, écart de 3,9 dB qui s'inverse quand le suivi est figé
  (attrapé par mutation : le balayage seul passait avec un suivi figé).
- ~~AC4~~ : AutoGain perceptuel, 0,07 LU sur 0 → 100 %.
- ~~AC5~~ : contrôle unique, aucune zone éditable.
- ~~AC7~~ : somme des contributions étage + EQ mesurée à mieux que 1 dB.

</details>

- [x] AC6 : le détecteur de fondamentale est **unique** — aucune seconde instance
  n'existe dans le processeur. *(Reformulé le 2026-09-20 : le détecteur appartient
  désormais au module EQ, où il a été **absorbé** avec la disparition de l'étage
  autonome. L'unicité reste vraie et vérifiable : `pitchDetector()` expose l'unique
  instance, celui de l'EQ.)*

---

## 3. Technical Specification

### 3.1 Architecture Overview

- **Stack** : C++20, **JUCE 8** (framework audio/plugin/UI).
  *Rationnel* : JUCE est le framework des plugins de cette catégorie, et ses
  primitives `juce::AudioParameterFloat` / `AudioProcessorValueTreeState` couvrent
  exactement le contrat de paramètres et d'automation dont ce produit a besoin. JUCE
  est donc un choix éprouvé pour ce type exact de produit.
- **Frameworks** : JUCE `audio_processors`, `dsp`, `gui_extra`.
- **Base de données** : aucune. Aucun service externe. Aucune télémétrie.
- **APIs** : aucune API réseau.
- **External services** : aucun.
- **Deployment target** : Windows 10+ (x86-64) et macOS 11+ (universal) en VST3 ;
  format de build additionnel U2 (AU) et U3 (CLAP) hors V1.
- **Outils de vérification** : Python 3.13 + `pedalboard` + `numpy`/`scipy`
  (déjà installés dans `.venv/`) pour les tests hors ligne automatisés.

**Pipeline de traitement, dans cet ordre :**

```
Input Gain (+ Calibration automatique)
  → Gate / Expander
  → ~~De-reverb~~                          ~~[US-14, P1]~~ (retiré le 2026-09-22)
  → Low Cut (fréquence, pente, dynamique)
  → EQ 4 bandes + Air (dynamique relative, accrochage harmonique)  [US-04]
  → Compressor (+ Mode Avancé)             [US-05]
  → De-esser (sifflantes) + Plosives       [US-10]
  → Drive (saveurs, oversampling)          [US-09]
  → Doubler + Largeur stéréo (M/S)         [US-08]
  → Delay (sync/libre, ducking)
  → Reverb (moteurs + pre-delay/decay/damping/width)
  → Output Gain → Bypass
```

### 3.2 DO NOT CHANGE

Le coding agent ne doit pas modifier :

- **`tools/verify_plugin.py`** — le harnais de vérification (59 contrôles). Son
  ensemble de contrôles **est** le contrat de sortie : en retirer un, c'est retirer
  une preuve.
- **`assets/knob_atlas_meta.h`** — généré avec `assets/knob_atlas.png` par
  `tools/generate_knob_frames.py` ; les deux se régénèrent ensemble.
- **`.venv/`** — environnement Python local, non versionné.
- **Le contrat de paramètres de §3.4** — une fois figé, tout renommage d'identifiant
  casse la compatibilité des presets et de l'automation.
- **L'interdiction de copier** : aucun asset, IR, preset ni fragment de binaire
  appartenant à un produit tiers ne doit entrer dans le dépôt.

### 3.3 Data Models

#### Entité `ParameterDefinition`

| Field | Type | Notes |
|-------|------|-------|
| `id` | `String` | identifiant stable, `snake_case`, jamais renommé |
| `name` | `String` | nom affiché |
| `module` | `enum` | `input, gate, cleanup, eq, comp, deess, drive, image, delay, reverb, output` |
| `mode` | `enum` | `essential` ou `advanced` |
| `unit` | `enum` | `dB, %, Hz, ms, cents, choice, bool` |
| `range` | `NormalisableRange<float>` | min, max, pas, skew |
| `default` | `float` | valeur par défaut |
| `visibleWhen` | `optional<String>` | id de paramètre booléen conditionnant la visibilité |
| `choiceLabels` | `optional<StringArray>` | uniquement si `unit == choice` |
| `automatable` | `bool` | `false` pour les actions (bouton de calibration) |

#### Entité `Preset`

| Field | Type | Notes |
|-------|------|-------|
| `schemaVersion` | `int` | version du format, commence à 1 |
| `name` | `String` | nom affiché |
| `author` | `String` | auteur |
| `category` | `enum` | `stream, lead, adlib, fx, space, user` |
| `tags` | `StringArray` | ex. `SM7b`, `rap`, `femme` |
| `parameters` | `map<String, float>` | id → valeur **normalisée 0–1** |
| `mode` | `enum` | mode actif au chargement (`essential` ou `advanced`) |

Sérialisation : **JSON** dans un unique fichier `.odvoxpreset`. Le choix du JSON
(structurellement proche de l'`AudioProcessorValueTreeState` de JUCE) permet à un
lecteur humain de diagnostiquer un preset sans lancer le plugin.

#### Entité `ModuleState` (interne, non sérialisée)

| Field | Type | Notes |
|-------|------|-------|
| `prepared` | `bool` | un module non préparé doit recopier son entrée vers sa sortie |
| `sampleRate` | `double` | cache du taux courant |
| `maxBlockSize` | `int` | taille maximale vue à la préparation |
| `latencySamples` | `int` | latence introduite, remontée à l'hôte |

### 3.4 API / Interface Contracts

#### Interface interne de module DSP

Tout module implémente cette interface. Aucune allocation mémoire, aucun verrou,
aucun appel système dans `process`.

```
prepare (sampleRate: double, maxBlockSize: int) -> void
reset () -> void
process (buffer: AudioBuffer<float>&, modulation: const ModMatrix&) -> void
latencySamples () -> int
bypassable () -> bool
```

- `process` reçoit un bloc de taille ≤ `maxBlockSize`.
- `modulation` porte les valeurs de paramètres lissées (une valeur lissée par
  paramètre, période de lissage ≥ 10 ms) pour éviter les clics.
- Erreur contractuelle : tout module qui n'applique pas le lissage déclenche un
  échec du test d'automation (G4).

#### Format de preset `.odvoxpreset`

- Input schema : objet JSON `Preset` (§3.3).
- Output schema : identique, relu en écriture (aller-retour sans perte).
- Error cases :
  - `schemaVersion` inconnu → refus de chargement, message « preset trop récent »,
    état audio inchangé.
  - identifiant de paramètre inconnu → ignoré, les autres appliqués, avertissement
    journalisé.
  - JSON invalide → refus, état audio inchangé.
  - fichier absent ou illisible → refus, état audio inchangé.

#### Contrat de paramètres avec l'hôte (VST3)

- Nombre total de paramètres v1 : **55** — **21 en Mode Essentiel + 34 en Mode
  Avancé**.
  *Ajout de 2 paramètres décidé le 2026-09-18* : `gate_range_db` et
  `lowcut_dynamic_pct`, exigés par AC3 de US-03 et AC3 de F1.5 mais absents du
  catalogue. Le compteur est passé de 50 à 52, le Mode Essentiel est resté à 25.
  *Ajout de 10 paramètres décidé le 2026-09-19 (F1.6)* : les **Q et les types des
  cinq bandes d'EQ** (5 + 5, les cinq bandes d'alors), exigés par AC1 de US-04 (« type, fréquence, gain et
  Q réglables ») et jusqu'ici reportés en Phase 2. Le compteur passe de 57 à 67.
  *Retrait de 5 paramètres décidé le 2026-09-19* : les **5 lignes du module
  Justesse** (3 `Essential` + 2 `Advanced`), jamais implémentées — voir US-07 et
  §3.4. Le compteur passe de 67 à 62, le Mode Essentiel de 25 à 22.
  *Retrait de 7 paramètres décidé le 2026-09-20 (low cut et EQ)* : le **low cut
  passe à une valeur fixe** — 120 Hz, 24 dB/oct, un simple interrupteur — et perd
  ses trois `Advanced` (`lowcut_freq_hz`, `lowcut_slope_db_oct`,
  `lowcut_dynamic_pct`) ; l'**EQ passe à quatre bandes**, la structure retenue,
  et perd la bande **Low-Mid** avec ses quatre lignes (1 `Essential`
  et 3 `Advanced`).  Le compteur v1 passe de 62 à **55**, le Mode Essentiel de 22 à
  **21**, le catalogue déclaré de 57 à **50**.
  *Retraits cumulés du 2026-09-21 (De-ess, Comp, Drive)* : le **De-ess passe à
  un seul curseur** (croisement fixe 5 kHz, plosives liées — 4 `Advanced` en
  moins) ; le **Comp perd son mode Custom** (`comp_on`, seuil, ratio, release,
  makeup, autogain — 6 en moins) ; le **Drive est figé sur Console**
  (`drive_flavor`, `drive_mix` — 2 en moins). Le catalogue déclaré passe de 50
  à **44**, le Mode Essentiel reste à 25.
  *Retrait du Mode Avancé décidé le 2026-09-22* : les **19 paramètres
  `Advanced` restants sont supprimés** — seuil/release/range du gate (3),
  fréquence/Q/type des quatre bandes d'EQ (12), detune/delay du doubler (2),
  feedback/filtre du delay (2). L'existant n'a pas de Mode Avancé du tout :
  un knob par idée, tout le reste est de conception, réglé DANS les modules
  (ancres EQ pitch-suiveuses 120/700/1 750/10 000 Hz — une fréquence manuelle
  contredirait le suivi). La courbe d'EQ passe au geste « gain seul », la
  molette-Q et le menu-type sont des no-op. Le catalogue déclaré passe de 44 à
  **25**, **100 % Essential, zéro caché** — exactement la surface de la
  l'existant. La bascule Essentiel/Avancé de F1.13 disparaît purement et
  simplement (AC2 « sans variation audio » n'a plus d'objet).
  *Ajout d'1 paramètre décidé le 2026-09-22 (F2.2)* : `hq_mode` (Drive,
  `Essential`, booléen, défaut **Off**), l'interrupteur du mode haute qualité
  (US-09) — un ajout APRÈS les figés, sur le modèle de l'amendement Gate du
  2026-09-18. Le catalogue déclaré passe de 25 à **26**, toujours 100 %
  Essential. La latence des filtres half-band (4 échantillons à 48 kHz) est
  reportée à l'hôte quand le mode est actif ET le drive engagé.

  *Amendement décidé le 2026-09-29 (refonte UI « cartes V6 », maquette
  `tools/mockup_cards.html` validée)* :
  1. **`hq_mode` toujours actif.** Le processeur pose `setHqRequested(true)` au
     `prepareToPlay` et n'est plus lu le paramètre : plus aucun réglage HQ dans
     l'interface (décision utilisateur : « Toujours HQ (recommandé) »). Le
     paramètre reste déclaré pour la compatibilité des sessions ; sa latence
     half-band continue d'être reportée quand le drive est engagé (un drive
     inerte reste bit-exact, donc sans latence : le zéro aux défauts tient).
  2. **`lowcut_amount` défaut Off → On**, comme l'existant. Assouplissement
     assumé du contrat de transparence (§3.4) : il porte désormais sur les
     CURSEURS d'intensité (gate/comp/de-ess/drive à 0 % = transparence), les
     interrupteurs de nettoyage étant des réglages TONAUX du défaut (même
     nature que l'Air +2,5 dB). Le toggle vit dans le héros de l'interface.
  3. **Trois enables de groupe** (`Essential`, booléens, défaut **On**) : la
     LED de bande de la nouvelle interface EST l'interrupteur du groupe —
     `fx_on` (bypass de groupe GATE/DE-ESS/DRIVE/DOUBLER/WIDTH, tranché avec
     l'utilisateur : bypass par inertie, aucune valeur perdue), `delay_on`,
     `reverb_on` (avant l'amendement, aucun enable n'existait pour ces deux
     modules). L'état OFF survit aux presets, à l'A/B et à la session. Le
     catalogue déclaré passe de 26 à **29**, toujours 100 % Essential.
  4. **Deux taps de niveaux** (hors catalogue) : `inputLevelDb()`/
     `outputLevelDb()` — RMS du dernier bloc, atomiques, alimentés dans
     `processBlock`, lus par les vumètres IN/OUT aux coins bas de l'interface.
     Les vumètres de réduction des cartes (Comp/Gate/De-ess) disparaissent de
     l'UI ; les sources (`lastGate/Comp/DeEssReductionDb`) restent au
     processeur pour les tests.

  **Deux compteurs, à ne pas confondre.** Le **total v1** ci-dessus (62) décrit le
  produit fini, modules futurs compris. Le  **catalogue déclaré dans le code**
  compte **50** paramètres : c'est le total moins les 5 `Advanced` des modules pas
  encore écrits (Delay, Reverb, Sortie). Il est vérifié par `static_assert` dans
  `src/Parameters.cpp` et par le harnais, qui lit le catalogue dans le source au
  lieu de le recopier.
  *Réduction de périmètre décidée le 2026-09-18* : la version initiale de ce
  document en prévoyait 90. Le catalogue doit être spécifié (plage, pas, défaut)
  **avant** l'écriture de F1.2, et §3.2 interdit de renommer un identifiant après
  figeage. Un catalogue de 55 entrées est spécifiable et validable ; les réglages
  retirés reviennent en Phase 2, une fois les modules validés.
- Paramètres par module (état du 2026-09-22 : tout Essential, mode Avancé
  supprimé — les lignes « Mode Avancé » d'avant le 2026-09-22 sont
  historiques) :

| Module | Paramètres exposés |
|---|---|
| Entrée & Gate | 3 (`input_gain_db`, `input_calibrate`, `gate`) |
| Nettoyage (low cut) | 1 (interrupteur) |
| EQ | 5 (4 gains + `eq_on`) |
| Compresseur | 1 |
| De-esser / Plosives | 1 |
| Drive | 2 (`drive_amount` + `hq_mode`) |
| Image | 2 (doubler + width) |
| Delay | 5 (amount, division, sync, libre, ducking) |
| Reverb | 4 (Short/Small/Big/Lush) |
| Enables de groupe (LED de bande) | 3 (`fx_on`, `delay_on`, `reverb_on` — amendement 2026-09-29) |
| Sortie | 2 (`output_gain_db`, `dc_filter`) |
| **Total** | **29** (26 + les 3 enables de l'amendement UI du 2026-09-29) |

- Ce qui a été retiré du Mode Avancé et reporté en Phase 2 : le `knee` du
  compresseur, les contrôles fins de reverb (damping, largeur) et les réglages de
  hold du gate. Le **Mode Essentiel** est le contrat de sortie du produit : ses
  **21 curseurs** (25 jusqu'au 2026-09-19, date du retrait du module Justesse —
  ses 3 curseurs et les paramètres d'échelle qui l'accompagnaient ; 22 jusqu'au
  2026-09-20, date du retrait de la bande Low-Mid) ne se renomment jamais.
  Les **Q des bandes d'EQ** en sont sortis le 2026-09-19 : AC1 de US-04 exige un
  Q réglable, donc ils font partie de V1 (F1.6).

#### Catalogue des 54 paramètres (v1)

**Ce catalogue est conçu ici — il n'est extrait d'aucun produit.** Les 54 entrées
ci-dessous sont celles d'**OD Vox**, dont **deux modules n'existent pas du tout
dans l'existant** (Gate, De-reverb) et dont **les paramètres du Mode Avancé sont
structurellement absents de l'existant**, qui n'a pas de Mode Avancé : c'est
précisément son défaut central.

**Statut de figeage (tranché le 2026-09-18, révisé le 2026-09-19 puis le
2026-09-20).** Les **21 identifiants `Essential` sont figés** : ils sont le contrat de sortie du
produit, et §3.2 interdit de les renommer.

*Révision du 2026-09-19* : les **5 lignes du module Justesse** (3 `Essential` —
`pitch_amount`, `pitch_speed_ms`, `pitch_key` — et 2 `Advanced` — `pitch_scale`,
`pitch_tolerance_cents`) ont été **retirées du catalogue**. Le module n'a jamais été
implémenté : les trois `Essential` étaient visibles dans l'interface et **inertes**
(aucune lecture de leur valeur dans la chaîne de traitement). US-07 est retirée avec
eux, et la fonction n'est plus au périmètre du produit. Le compteur `Essential`
passe de 25 à 22, le total v1 de 67 à 62. **Les identifiants retirés ne sont pas
réutilisables** : ils ont été exposés par une version publiée du catalogue, et les
recycler pour autre chose casserait le contrat qu'ils portaient.

*Révision du 2026-09-20* : sept lignes retirées, **aucun identifiant recyclable**
pour la même raison. Le **low cut** perd ses trois réglages (`lowcut_freq_hz`,
`lowcut_slope_db_oct`, `lowcut_dynamic_pct`) : l'existant n'a qu'un interrupteur,
et sa position est **retenue** — un passe-haut à **120 Hz, 24 dB/oct** (coin
−3 dB à 120 Hz, pente 24 dB/oct). `lowcut_amount` **garde son identifiant**
et change de nature : il devient un booléen (On / Off, défaut Off), ce qui préserve
les presets et l'automation qui le désignent. L'**EQ** perd la bande **Low-Mid**
(notre ajout, absent de l'existant) et ses quatre lignes ; les quatre bandes
restantes portent la structure retenue (Low ≈ 120 Hz, Mid ≈ 700 Hz, High plein à
3,5 kHz — coin 1,75 kHz, Air > 10 kHz), **recalée au signal le 2026-09-20** :
à cette occasion, le coin du High est passé de 3,5 kHz (où nous ne réalisions que
la moitié du gain ; 3,5 k est le **centre** du plateau voulu) à 1,75 kHz, le coin
de l'Air de 11 kHz à 10 kHz, le Q nominal du Mid à 1,15 (Q effectif 0,95) et celui
du High à 1,2 — un shelf résonnant à +16,8 dB réalisés à 3,5 kHz pour +15 demandés.
Les identifiants `Advanced` ne sont pas figés — ils peuvent encore être renommés,
fusionnés ou remplacés jusqu'à ce que leur module soit implémenté.

Attention à ce que « figé » recouvre : c'est l'**identifiant** qui est
irréversible, pas la plage. Une plage, un pas ou un défaut se corrigent à tout
moment sans rien casser. Un id renommé casse les presets et l'automation.

*Provenance des valeurs* : certaines plages reprennent des repères ergonomiques
usuels (EQ ±15 dB, `comp_amount` 0–100 %, delay feedback plafonné à 95 %,
`delay_filter` neutre à 39 %, Air par défaut à +2.5 dB). Les autres sont des choix
de conception, à valider au ressenti.

| # | `id` | Nom affiché | Module | Mode | Unité | Plage | Défaut |
|---|---|---|---|---|---|---|---|
| 1 | `input_gain_db` | Input Gain | Entrée & Gate | Essential | dB | −24 → +24, pas 0.1 | 0 |
| 2 | `input_calibrate` | Calibrate | Entrée & Gate | Essential | action | non automatisable | — |
| 3 | `gate_amount` | Gate | Entrée & Gate | Essential | % | 0 → 100, pas 1 | 0 |
| 4 | `gate_threshold_db` | Gate Threshold | Entrée & Gate | Advanced | dB | −80 → 0, pas 0.5 | −45 |
| 5 | `gate_release_ms` | Gate Release | Entrée & Gate | Advanced | ms | 10 → 2000, log | 150 |
| 6 | `gate_range_db` | Gate Range | Entrée & Gate | Advanced | dB | 0 → 90, pas 1 | 80 |
| 7 | `lowcut_amount` | Low Cut | Nettoyage | Essential | interrupteur | On / Off | **On** *(amendement 2026-09-29 : comme l'existant ; assouplissement assumé du contrat de transparence §3.4 — réglage tonal, hors curseurs)* |
| 8 | `eq_low_db` | Low | EQ | Essential | dB | −15 → +15, pas 0.1 | 0 |
| 9 | `eq_mid_db` | Mid | EQ | Essential | dB | −15 → +15, pas 0.1 | 0 |
| 10 | `eq_hi_db` | High | EQ | Essential | dB | −15 → +15, pas 0.1 | 0 |
| 11 | `eq_air_db` | Air | EQ | Essential | dB | −15 → +15, pas 0.1 | +2.5 |
| 12 | `eq_on` | EQ On | EQ | Essential | booléen | — | On |
| 13 | `eq_low_freq_hz` | Low Freq | EQ | Advanced | Hz | 40 → 400, log | 120 |
| 14 | `eq_mid_freq_hz` | Mid Freq | EQ | Advanced | Hz | 300 → 5000, log | 700 |
| 15 | `eq_hi_freq_hz` | High Freq | EQ | Advanced | Hz | 1500 → 12000, log | 3500 |
| 16 | `eq_air_freq_hz` | Air Freq | EQ | Advanced | Hz | 6000 → 20000, log | 11000 |
| 17 | `eq_low_q` | Low Q | EQ | Advanced | Q (sans dimension) | 0.2 → 8, pas 0.01 | 1.0 |
| 18 | `eq_mid_q` | Mid Q | EQ | Advanced | Q (sans dimension) | 0.2 → 8, pas 0.01 | 1.0 |
| 19 | `eq_hi_q` | High Q | EQ | Advanced | Q (sans dimension) | 0.2 → 8, pas 0.01 | 1.0 |
| 20 | `eq_air_q` | Air Q | EQ | Advanced | Q (sans dimension) | 0.2 → 8, pas 0.01 | 1.0 |
| 21 | `eq_low_type` | Low Type | EQ | Advanced | choix | Bell / Low Shelf / High Shelf / High Pass | Low Shelf |
| 22 | `eq_mid_type` | Mid Type | EQ | Advanced | choix | Bell / Low Shelf / High Shelf / High Pass | Bell |
| 23 | `eq_hi_type` | High Type | EQ | Advanced | choix | Bell / Low Shelf / High Shelf / High Pass | High Shelf |
| 24 | `eq_air_type` | Air Type | EQ | Advanced | choix | Bell / Low Shelf / High Shelf / High Pass | High Shelf |
| 25 | `comp_amount` | Comp | Compresseur | Essential | % | 0 → 100, pas 1 | **0** |
| 27 | `comp_threshold_db` | Threshold | Compresseur | Advanced | dB | −60 → 0, pas 0.5 | −18 |
| 28 | `comp_ratio` | Ratio | Compresseur | Advanced | x (continu) | 1.5 → 20, pas 0.01 | 3 |
| 29 | `comp_release_ms` | Release | Compresseur | Advanced | ms | 20 → 1000, log | 150 |
| 30 | `comp_makeup_db` | Makeup | Compresseur | Advanced | dB | 0 → 30, pas 0.1 | 0 |
| 31 | `comp_autogain` | Comp AutoGain | Compresseur | Advanced | booléen | — | On |
| 32 | `deess_amount` | De‑ess | De‑esser | Essential | % | 0 → 100, pas 1 | **0** |
| — | *(croisement 5 kHz, plosives liées au curseur cap 20 %, shelf 120 Hz : préréglés en interne)* | — | De‑esser | — | — | fidélité à la simplicité de l'existant | — |
| ~~33~~ | ~~`deess_freq_hz`~~ | ~~De‑ess Freq~~ | ~~De‑esser~~ | ~~Advanced~~ | ~~Hz~~ | retiré le 2026-09-21 : croisement FIXE 5 kHz (sous la zone 6–9 kHz) | — |
| ~~34~~ | ~~`plosive_amount`~~ | ~~Plosives~~ | ~~De‑esser~~ | ~~Advanced~~ | ~~%~~ | retiré le 2026-09-21 : plosives LIÉES au curseur (0,6·amount, cap 20 %) | — |
| ~~35~~ | ~~`plosive_freq_hz`~~ | ~~Plosive Freq~~ | ~~De‑esser~~ | ~~Advanced~~ | ~~Hz~~ | retiré le 2026-09-21 (fixé à 120 Hz) | — |
| ~~36~~ | ~~`deess_listen`~~ | ~~De‑ess Listen~~ | ~~De‑esser~~ | ~~Advanced~~ | ~~booléen~~ | retiré le 2026-09-21 : absent de l'existant | — |
| 37 | `drive_amount` | Drive | Drive | Essential | % | 0 → 100, pas 1 | 0 |
| 38a | `hq_mode` | HQ Mode | Drive | Essential | booléen | — | **toujours actif** *(amendement 2026-09-29 : le paramètre n'est plus lu — `setHqRequested(true)` au `prepareToPlay` ; déclaré pour la compatibilité des sessions)* |
| ~~38~~ | ~~`drive_flavor`~~ | ~~Drive Flavor~~ | ~~Drive~~ | ~~Advanced~~ | ~~choix~~ | retiré le 2026-09-21 : figé sur Console (l'engin retenu) | — |
| ~~39~~ | ~~`drive_mix`~~ | ~~Drive Mix~~ | ~~Drive~~ | ~~Advanced~~ | ~~%~~ | retiré le 2026-09-21 : mix interne 100 % | — |
| 40 | `doubler_amount` | Doubler | Image | Essential | % | 0 → 100, pas 1 | 0 |
| 41 | `width_amount` | Width | Image | Essential | % | 0 → 200, pas 1 | 100 |
| 42 | `doubler_detune_cents` | Doubler Detune | Image | Advanced | cents | 0 → 50, pas 1 | 12 |
| 43 | `doubler_delay_ms` | Doubler Delay | Image | Advanced | ms | 5 → 60, pas 1 | 22 |
| 44 | `delay_amount` | Delay | Delay | Essential | % | 0 → 100, pas 1 | 0 |
| 45 | `delay_time` | Delay Time | Delay | Essential | choix | 21 divisions rythmiques | `1/4` |
| 46 | `delay_feedback_pct` | Feedback | Delay | Advanced | % | 0 → 95, pas 1 | 20 |
| 47 | `delay_filter_pct` | Delay Filter | Delay | Advanced | % | 0 → 100, pas 1 | 39 |
| 48a | `reverb_short_pct` | Short | Reverb | Essential | % | 0 → 100, pas 1 | 0 |
| 48b | `reverb_small_pct` | Small | Reverb | Essential | % | 0 → 100, pas 1 | 0 |
| 48c | `reverb_big_pct` | Big | Reverb | Essential | % | 0 → 100, pas 1 | 0 |
| 48d | `reverb_lush_pct` | Lush | Reverb | Essential | % | 0 → 100, pas 1 | 0 |
| 48e | `fx_on` | FX On | De-esser *(groupe FX)* | Essential | booléen | — | On *(amendement 2026-09-29 : LED-interrupteur de la bande FX — bypass de groupe GATE/DE-ESS/DRIVE/DOUBLER/WIDTH par inertie, valeurs préservées)* |
| 48f | `delay_on` | Delay On | Delay | Essential | booléen | — | On *(amendement 2026-09-29 : LED-interrupteur de la bande DELAY)* |
| 48g | `reverb_on` | Reverb On | Reverb | Essential | booléen | — | On *(amendement 2026-09-29 : LED-interrupteur de la bande REVERB)* |
| — | *(pre-delay préréglé en interne à 20 ms)* | — | Reverb | — | — | valeur du preset « Clean Vocals Reverb » | — |
| ~~49~~ | ~~`reverb_engine`~~ | ~~Reverb Engine~~ | ~~Reverb~~ | ~~Essential~~ | ~~choix~~ | retiré le 2026-09-21 : remplacé par QUATRE curseurs (un par moteur) | — |
| ~~50~~ | ~~`reverb_decay_pct`~~ | ~~Decay~~ | ~~Reverb~~ | ~~Advanced~~ | ~~%~~ | retiré le 2026-09-21 : chaque moteur porte désormais son propre RT60 nominal (0,4 / 1,1 / 2,2 / 4,2 s) | — |
| ~~51~~ | ~~`reverb_predelay_ms`~~ | ~~Pre-delay~~ | ~~Reverb~~ | ~~Advanced~~ | ~~ms~~ | retiré le 2026-09-21 (fixé à 20 ms) | — |
| 52 | `output_gain_db` | Output Gain | Sortie | Essential | dB | −24 → +24, pas 0.1 | 0 |
| 53 | `output_dc_filter` | DC Filter | Sortie | Essential | booléen | — | On |
| 54 | `output_ceiling_db` | Ceiling | Sortie | Advanced | dB | −12 → 0, pas 0.1 | 0 |

Trois points de conception à trancher, rencontrés en écrivant ce catalogue :

1. **Pas de paramètre `bypass` maison.** VST3 en fournit un, et JUCE le déclare
   automatiquement — vérifié empiriquement : `tools/probe_plugin.py` liste un
   paramètre `Bypass` sur OD Vox **sans qu'on l'ait déclaré**. En déclarer un
   second créerait un doublon. C'est pour ça que la Sortie porte un filtre DC à la
   place.
2. **Le macro pilote les paramètres avancés — tranché le 2026-09-18.** Le curseur
   d'intensité d'un module **recalcule** les réglages avancés de ce module depuis
   une table interne. Dès que l'utilisateur bouge un seul réglage avancé, le
   curseur passe en état **`Custom`** et cesse de piloter : les deux ne se
   superposent **jamais**. Règle commune aux quatre modules concernés — `comp`,
   `gate`, `lowcut`, `deess`.
   Corollaire : rebouger le curseur d'intensité **quitte `Custom`, reprend le
   pilotage et écrase** les réglages avancés. Le changement d'état doit être
   annoncé dans l'interface, sinon l'utilisateur perd ses réglages sans comprendre
   pourquoi. Vérifié par AC4 à AC6 de US-05.
3. **« Un curseur par module » (US-02) ne couvre pas les gains d'entrée et de
   sortie**, qui ne sont pas des modules de traitement. Le catalogue applique
   US-02 aux 10 modules audio seulement.
4. **Les noms affichés sont un contrat, pas du texte libre — mesuré le 2026-09-18.**
   L'hôte n'expose **pas** l'identifiant du catalogue : il expose un **slug du nom
   affiché**, suivi de l'unité. Relevé sur le plugin construit : `deess_amount` →
   `de_ess`, `doubler_detune_cents` → `doubler_detune_cents` (le suffixe d'unité
   s'ajoute au slug du nom affiché — et il a fallu **mesurer** pour le savoir :
   l'unité `cents` n'existait pas quand la liste d'unités à suffixe a été écrite).
   Conséquence à ne jamais perdre de vue : **deux paramètres qui partagent un nom
   affiché se recouvrent**, et l'un des deux devient inatteignable depuis l'hôte.
   C'est la cause exacte d'un paramètre devenu inatteignable depuis l'hôte —
   31 listés pour 32 réellement présents ; le harnais en fait un contrôle.
   Deux garde-fous : les 21 noms affichés sont **uniques** (vérifié par le test
   `Les cles vues par l'hote sont figees et sans recouvrement`), et le harnais
   compare le jeu de clés attendu au jeu réellement exposé en nommant tout
   recouvrement.
   Nuance utile : les presets et l'automation se lient aux **identifiants**, pas à
   ces clés. Un renommage à l'écran ne casse donc pas un preset déjà écrit — il
   change seulement ce que voient les hôtes et les utilisateurs.

5. **De-esser : croisement par défaut ramené à 5000 Hz — mesuré le 2026-09-19.**
   La plage 4000 → 12000 est un choix de conception ; le défaut de 6500 Hz, lui,
   s'est révélé **faux à l'usage**. Le de-esser est un split-band à référence
   adaptative : sa réduction vaut l'excès du niveau de la bande haute sur celui de
   la bande basse, et un croisement placé *dans* la zone de sifflance (6–9 kHz,
   celle que la conception retient comme utile) décape cet excès.
   Mesuré sur un « S » à 7 kHz posé sur une voix : à 6500 Hz de croisement il ne
   reste que **−0,55 dB** d'excès, donc aucune réduction ; à 5000 Hz il en reste
   5,8 dB. Le défaut doit donc se trouver **sous** la zone à traiter : 5000 Hz.
6. **`deess_amount` passe de 25 % à 0 % par défaut**, et `deess_listen` entre au
   catalogue. Mêmes raisons qu'au compresseur : AC2 de US-02 exige la transparence
   bit-exacte aux défauts du catalogue, qu'un de-esser adaptatif actif à 25 %
   violerait ; et AC2 de US-10 exige un mode « listen » qui n'existait pas au
   catalogue initial. Les presets d'usine reprennent la valeur 25 %.
   *(Amendement du 2026-09-21 : `deess_listen` ressort ensuite du catalogue —
   absent de l'existant, un seul curseur retenu.)*
7. **`drive_flavor` : le défaut passe de `Tube` à `Console` — mesuré le
   2026-09-19.** L'engin retenu donne une saturation
   **symétrique** (3f à −10,1 dB et 5f à −15,8 dB sous la fondamentale à 100 %,
   harmoniques **pairs au plancher** à −75 dB) : ni un `tanh` déguisé en lampe, ni
   le caractère asymétrique d'un étage à lampe. `Console` est la saveur qui
   reproduit cet engin ; `Tube` en est précisément l'inverse spectral. Garder
   `Tube` par défaut aurait rendu le réglage le plus accessible le moins fidèle.
   **L'intention du produit est de partir de l'engin retenu, d'ajouter ensuite** :
   le défaut est cet engin, les trois autres saveurs sont l'amélioration.
   L'**ordre des libellés** (`Tube|Tape|Console|Fuzz`) ne change pas : le
   réordonner changerait silencieusement ce que désigne l'index enregistré dans
   les presets et l'automation.8. **Le contrat de transparence porte sur les CURSEURS D'INTENSITÉ, pas sur les
   réglages tonaux — tranché le 2026-09-19, à l'écriture du F1.6.** AC2 de US-02
   (« mettre chaque curseur à 0 % rend le module transparent ») visait les curseurs
   en %, et l'esprit de la V1 (« le plug s'ouvre neutre », appliqué à
   `comp_amount`, `deess_amount`, `gate_amount`, `lowcut_amount` et
   `drive_amount`) se heurte à `eq_air_db` : ce n'est pas un curseur
   d'intensité, c'est un **réglage tonal**, et sa valeur par défaut de **+2,5 dB**
   est un repère de conception, voulu par défaut ici.
   **Conséquence, vérifiée et assumée** : aux défauts du catalogue la chaîne n'est
   **pas** bit-exacte — elle rend l'air voulu. Mesuré : écart nul à
   100 et 300 Hz, **+2,67 dB à 15 kHz**, et remettre `eq_air_db` à 0 dB rend la
   chaîne bit-exacte. Les tests de transparence des autres modules posent donc
   l'EQ au réglage neutre avant de mesurer, et le disent — six tests existants
   ont dû être amendés, plus quatre contrôles du harnais.
9. **Les Q des bandes d'EQ valent 1,0 par défaut, et ce n'est pas arbitraire —
   mesuré le 2026-09-19.** Pour un shelf de JUCE, le paramètre Q n'est pas une
   résonance mais un **réglage de pente**, et le **coin vaut exactement la moitié
   du plateau en dB** : un shelf de +2,5 dB donne +1,25 dB à son coin, et
   n'atteint son plateau qu'une demi-octave au-dessus. Deux conséquences
   chiffrées, toutes deux reproduites par un modèle numérique du filtre écrit à
   part :
   - **Low à 120 Hz / +15 dB : Q 1,0 donne +15,42 dB à 28 Hz**, là où la
     cible est de **+15,4 dB** au même endroit (Q 0,71 n'en donnerait que
     +14,94) ;
   - **Air à 10 kHz / +2,5 dB : Q 1,0 donne ~+2,3 dB à 12 kHz et +2,6 dB à
     15 kHz** (recalé le 2026-09-20, le coin étant passé de 11 k à 10 k) ; à
     Q 0,71 l'Air ne vaut que **+1,57 dB à 12 kHz** et n'atteint son plateau
     qu'au-delà de 19 kHz — le réglage le plus utilisé aurait
     été le plus inaudible des nôtres.
   Q 1,0 place le plateau **dans la bande audio** sans survol résonnant, et
   correspond au Q ≈ 1 retenu pour le medium (Q nominal 1,15 chez
   nous : le Q **effectif** à −3 dB vaut alors 0,95, mesuré — voir plus bas).
- Contrainte : le **mode** (`essential` / `advanced`) ne change **jamais** la valeur
d'un paramètre ni le rendu audio. C'est un filtre de visibilité d'interface
  uniquement. Vérifié par le test d'AC3 de US-05.

#### Interface du harnais de vérification hors ligne

- Point d'entrée : `.venv/Scripts/python.exe tools/verify_plugin.py <chemin.vst3>`
- Rôle : charger le plugin construit via `pedalboard`, appliquer des signaux de test
  déterministes, et produire un rapport des métriques de §5.
- Sortie : un fichier `verification_report.md` + un code de sortie non nul si un
  seuil est manqué.
- Error cases : plugin introuvable → code 2 ; plugin qui ne traite pas l'audio
  (sortie nulle) → code 3 avec diagnostic explicite.
- **Le catalogue est lu dans `src/Parameters.cpp`, jamais recopié** dans le
  harnais : c'est ce qui garantit que les deux ne divergent pas.
- **Tous les contrôles audio tournent sur l'état par défaut**, avant le balayage
  des paramètres. Un harnais qui balaie d'abord laisse les contrôles à mi-course
  et mesure autre chose que ce que l'utilisateur entend à l'ouverture — erreur
  commise puis corrigée le 2026-09-18.

---

## 4. Features & Requirements

### Phase 1 — Fondations et chaîne complète

**Goal:** livrer un plugin VST3 qui transforme une voix brute en voix mixée de bout en bout, avec Mode Essentiel et Mode Avancé fonctionnels.
**Dependency:** aucune

#### F1.1 — Squelette de projet et build

Related user stories:
- US-01, US-02, US-03, US-04, US-05, US-06, US-08, US-09, US-10,
  US-11, US-12 (prérequis technique : aucune story n'est vérifiable sans
  une build chargeable. US-07 et US-13 en sont absentes : retirées le 2026-09-19
  et le 2026-09-20)

Description:
- Projet JUCE 8 / C++20, cibles VST3 Windows et macOS, CMake.
- Build en deux configurations : `Debug` (assertions et validation) et
  `Release` avec `-ffast-math` désactivé sur les étages non linéaires.

Acceptance Criteria:
- [ ] AC1 : `cmake --build` produit un `.vst3` qui se charge dans un hôte (vérifié
  par `tools/probe_plugin.py`, qui doit lister les paramètres).
- [ ] AC2 : le build Release atteint G3 (≤ 6 % d'un cœur à 48 kHz stéréo, chaîne
  complète, sans oversampling).
- [ ] AC3 : le plugin fonctionne à 44.1, 48, 88.2, 96 et 192 kHz sans dépassement
  de génération de blocs.

Edge Cases:
- Si l'hôte demande un taux non standard, alors les fréquences de filtre sont
  recalculées à partir du taux reçu et non de constantes figées.
- Si `maxBlockSize` change entre deux appels, alors réallouer à la préparation
  uniquement, jamais dans `process`.

Constraints:
- Interdiction d'allocation mémoire, de verrou et d'entrée/sortie fichier dans
  `processBlock`.

#### F1.2 — Bus de paramètres, presets, A/B

Related user stories:
- US-01, US-02, US-11

Description:
- `AudioProcessorValueTreeState`, lissage ≥ 10 ms sur chaque paramètre continu.
  **Mise en oeuvre par étapes** : F1.2 déclare les **21 identifiants `Essential`**,
  qui sont figés (§3.4). Les `Advanced` sont ajoutés module par module, quand
  le lot de Phase 1 qui les implémente est écrit — leurs identifiants ne sont donc
  pas encore figés.
- Menu de presets : chargement et écriture au format `.odvoxpreset` (§3.3).
  Cible de presets d'usine révisée au §8 (projet non commercialisé).
- Deux emplacements A/B, comparables et duplicables.

Acceptance Criteria:
- [ ] AC1 : AC1–AC3 de US-01 vérifiés.
- [ ] AC2 : AC1–AC2 de US-11 vérifiés.
- [ ] AC3 : un aller-retour écriture puis relecture d'un preset restitue
  exactement tous les paramètres **alors déclarés** (comparaison bit à bit des
  valeurs normalisées). Au terme de F1.2 cela porte sur les 25 `Essential` ; le
  test s'étend automatiquement à mesure que les `Advanced` sont déclarés.

Edge Cases:
- Si un preset référence un paramètre inconnu, alors ignorer ce paramètre, charger
  les autres, et écrire un avertissement dans le journal.
- Si le dossier utilisateur est non inscriptible, alors signaler l'échec et
  conserver l'état audio courant.

Constraints:
- Le mode (`essential` / `advanced`) ne doit jamais être sérialisé comme valeur de
  paramètre audio.

#### F1.3 — Entrée : gain et calibration automatique

Related user stories:
- US-06

Acceptance Criteria:
- [ ] AC1–AC3 de US-06 vérifiés.

Edge Cases:
- Si aucun son n'est détecté pendant 10 s, alors abandonner la calibration et
  laisser le gain inchangé, avec message dans l'interface.
- Si le signal détecté est déjà au-dessus de −6 dBFS crête, alors la calibration
  atténue au lieu de renforcer.

Constraints:
- Paramètres : `input_gain_db` (Essentiel), `input_calibrate` (action,
  non automatisable). Les réglages du gate vivent dans le module suivant, sous
  `gate_amount` et ses trois paramètres avancés.
- **Implémenté le 2026-09-18.** Le signal est mesuré **après** le gain d'entrée, et
  la calibration passe par une rampe de **50 ms** sur le gain — c'est ce qui tient
  AC3. La mesure dure 5 s et n'aboutit que si du son a été détecté ; sinon elle
  abandonne au bout de 10 s sans toucher au gain.

#### F1.4 — Gate / Expander

Related user stories:
- US-03

Acceptance Criteria:
- [ ] AC1–AC3 de US-03 vérifiés.

Edge Cases:
- Si un signal est en permanence sous le seuil, alors appliquer `gate_range_db` et
  non un silence total.
- Si le maintien est plus court que l'occlusion d'une plosive, alors la consonne
  est coupée en deux : le maintien est donc fixé à **50 ms**, au-dessus de la durée
  d'une occlusion typique.

Constraints:
- Détection par enveloppe avec hystérésis (seuil de fermeture ≤ seuil d'ouverture),
  écart de 3 dB.
- **Paramètres : `gate_amount` (Essentiel) plus `gate_threshold_db`,
  `gate_release_ms` et `gate_range_db`.** `gate_amount` à 0 % doit rendre le module
  strictement transparent (AC2 de US-02) : la profondeur effective vaut donc
  `gate_range_db` × `gate_amount`, ce qui satisfait AC1 de US-03 (≥ 60 dB) et AC3
  (la réduction égale la valeur réglée) dès que le curseur est à 100 %.
- **L'attaque (0,5 ms) et le maintien (50 ms) sont des constantes de conception**,
  comme `attack` et `knee` pour le compresseur : §3.4 ne leur alloue aucun
  emplacement. Vérifié le 2026-09-18 — l'atténuation sur les 20 premières ms d'une
  plosive reste sous 0,5 dB.

#### F1.5 — Nettoyage : Low Cut

Related user stories:
- US-02

Description:
- Un passe-haut à **valeur figée** : **120 Hz, 24 dB/oct**, commandé par un simple
  interrupteur (`lowcut_amount`). Aucun réglage de fréquence, de pente ni de
  dynamique.

Pourquoi figé, et pourquoi ces valeurs (décision et livraison du 2026-09-20) : le
module tient **un interrupteur, et rien d'autre** — deux valeurs suffisent à le
décrire, un coin à −3 dB ≈ **110–120 Hz** et une pente de **19,8 dB/oct**. 120 Hz /
24 dB/oct est l'arrondi Butterworth le plus proche. Nos trois `Advanced` (fréquence,
pente, dynamique) n'apportaient **aucun repère de conception** : ils sont retirés,
avec leurs identifiants (voir §3.4).

Acceptance Criteria:
- [x] AC1 : la coupure mesurée à −3 dB vaut **120 Hz ±10 %** et la pente
  **24 dB/oct ±3**. *(Mesuré : −3,01 dB à 120 Hz et −48,2 dB à 30 Hz au harnais ;
  −3 dB au coin et 24 dB/oct aux tests unitaires.)*
- [x] AC2 : à 1 kHz le module ne touche à rien (±0,5 dB). *(Mesuré : −0,01 dB.)*
- [x] AC3 : `lowcut_amount` **Off** est transparent **au bit près** — et c'est le
  défaut du catalogue. *(Vérifié : sortie identique à l'entrée, bit à bit, aux
  tests unitaires comme au harnais.)*
- [x] AC4 : le module **n'expose aucun réglage à côté de son interrupteur**.
  *(Vérifié : `lowcut_freq_hz`, `lowcut_slope_db_oct` et `lowcut_dynamic_pct` ne
  sont plus déclarés — 5 assertions de contrat, plus le `static_assert` du
  catalogue.)*

Constraints:
- **L'identifiant `lowcut_amount` est conservé** malgré le changement de nature
  (% → booléen) : il est `Essential`, donc figé (§3.2), et des presets et de
  l'automation le désignent. Le défaut passe de 0 % à **Off** : l'ancien défaut
  (0 %) rendait le module transparent, le nouveau aussi — un état enregistré garde
  donc son sens.
- Le filtre est un **Butterworth d'ordre 4** (deux biquads aux Q de Butterworth,
  0,5412 et 1,3066) : c'est ce qui donne exactement −3 dB au coin **et** −24 dB par
  octave. Un Linkwitz-Riley aurait donné −6 dB au coin, pas −3.
- L'ancienne version était un mélange sec/effet avec une dynamique (low-shelf à
  120 Hz) : les deux sont partis avec les paramètres qui les réglaient.

#### F1.6 — EQ interactif avec spectre

Related user stories:
- US-04

Acceptance Criteria:
- [x] AC1–AC2 de US-04 vérifiés (DSP).
- [x] AC3 de US-04 vérifié par l'interface de courbe et d'analyseur — lot F1.6c
  (2026-09-19).
- [x] AC4 de US-04 vérifié (dynamique relative) — livré le **2026-09-20** avec le
  passage à quatre bandes : fraction commune aux quatre bandes, suivi du niveau du
  programme, **0 / 3 / 6 dB** mesurés à −60 / −40 / −20 dBFS pour un réglage de
  +6 dB.
- [x] AC5 de US-04 vérifié (AutoGain d'étage) — livré le **2026-09-20** : **0,00 dB
  à plein régime** (la courbe reste la réponse) comme **sous le plancher**, 0,73 dB
  pour un manque de 3,00 au milieu. Deux lois fausses ont été mesurées puis
  corrigées (+3,87 dB puis +3,38 dB de gain plat sous le plancher).
- [x] Reproductibilité : `reset()` rend un état qui ne dépend que du réglage, donc
  deux rendus du même signal sont identiques **au bit près**. *(La fréquence
  accrochée survivait au reset — écart 6,2e-4 dès l'échantillon 4, trouvé par le
  harnais ; corrigé, et tenu par un test unitaire que la mutation du re-ancrage
  fait échouer.)*

Edge Cases:
- [x] Si deux bandes ont la même fréquence, alors la courbe affichée est leur somme
  et non une superposition. *(Vérifié : deux cloches de +6 dB à 1 kHz donnent
  +12 dB.)*
- [x] Un passe-haut à 0 dB n'est PAS neutre, et c'est dit : le module n'est
  court-circuité que si toutes les bandes sont à 0 dB **et** qu'aucune n'est un
  passe-haut. Un passe-haut filtre à 0 dB comme à +12 (vérifié : −3 dB au coin,
  12 dB/octave).

Constraints:
- [x] Fréquences et Q par bande sont réglables par l'utilisateur, là où l'existant
  a des bandes à fréquence et Q figés. *(Livré : 4 fréquences + 4 Q + 4 types, soit
  12 paramètres `Advanced`. Les défauts reprennent la structure retenue — Low
  shelf ≈ 120 Hz, Mid cloche ≈ 700 Hz / Q effectif ≈ 1, Hi shelf plein à 3,5 kHz
  (coin 1,75 kHz, Q 1,2), Air shelf au-dessus de ≈ 10 kHz — recalée au signal le
  2026-09-20 : le principe est de mesurer ce qu'on livre, puis d'améliorer.)*
- **Le suivi de pitch est borné, et la borne est appliquée** : une bande ne
  s'accroche qu'à une harmonique **résolue** (n ≤ 5), c'est-à-dire quand la grille
  harmonique est au moins aussi large que la fenêtre de ±150 cents. Sinon il y a
  toujours une harmonique dans la fenêtre et la bande glisserait en permanence,
  commandée par la note chantée plutôt que par le réglage. *(Mesuré : voix à
  120 Hz sur une ancre de 700 Hz — la 6e harmonique, à 720 Hz, est à 49 cents de
  l'ancre — la bande reste à 700 Hz ; sans la garde elle partait à 720, attrapé par
  mutation.)*
- **La dynamique relative est calibrée sur le programme**, pas sur le contenu de la
  bande : plancher −55 dBFS, plein à −25 (30 dB d'étendue). Un réglage d'EQ est une
  intention **explicite** : à −30 dBFS, l'utilisateur s'attend à entendre ce qu'il a
  posé — là où un curseur « d'intention artistique » (seuil −34 dBFS) peut vouloir
  agir moins sur les passages faibles.

#### F1.6c — Interface de courbe d'EQ et analyseur de spectre

Related user stories:
- US-04 (AC1 « glisser-déposer », AC3)

Description:
- Courbe d'égalisation **interactive** : les quatre bandes s'y déplacent au
glisser-déposer (fréquence en horizontal, gain en vertical), la largeur au
pincement ou à la molette, et l'analyseur de spectre se dessine derrière.

Pourquoi ce lot existe : la décision du 2026-09-19 a été de livrer **le DSP
avant l'interface**, pour que les critères de fidélité de l'EQ soient mesurables
et validés avant de dessiner quoi que ce soit dessus.

Acceptance Criteria:
- [x] AC1 de US-04 vérifié **par le geste** : un glisser-déposer sur la courbe
  modifie bien les quatre paramètres de la bande visée, et la mesure au signal
  suit la courbe à ±0,5 dB (AC2, déjà vérifié au niveau DSP).
  *(Mesuré 2026-09-19 : gestes exercés avec de vrais `juce::MouseEvent` envoyés au
  composant rendu hors écran — construction d'éditeur, dessin, glisser, molette,
  double-clic. Geste → fréquence + gain écrits dans les paramètres ; molette → Q
  (facteur 2 par encoche, butoir au catalogue) ; menu contextuel → type ; mesure au
  signal après geste ≤ 0,5 dB de la courbe sur 10 fréquences. Validé par mutation :
  débrancher `mouseDrag` fait échouer le geste, geler l'état dessiné fait échouer le
  re-dessin.)*
- [x] AC3 de US-04 vérifié : analyseur ≥ 1/12 d'octave, ≥ 20 images/s, aucun
  dépassement de bloc sur 10 min.
  *(Mesuré 2026-09-19, détails dans US-04 : bornes réelles des bandes à 2^(1/12)
  près, **23 FFT/s de données neuves** à 48 kHz mesurées sur un flux réel, **10 min
  à 30 images/s sans un seul échantillon abandonné**, calibration vérifiée à ±2 dB
  sur −12/−30/−60 dBFS.)*

Constraints:
- **La FFT de l'analyseur ne vit pas dans le thread audio** (§7, risques) : elle
  lit une copie du bus de sortie depuis le thread de messages, au rythme de
  l'affichage.
  *(Respecté : un anneau `AbstractFifo` sans verrou porte le mixage mono de la
  sortie du thread audio vers l'analyseur ; en cas de retard du lecteur, les
  échantillons en trop sont abandonnés et **comptés** — `droppedSamples()` doit
  rester à 0, et c'est ce compteur qui rend « aucun dépassement de bloc »
  mesurable.)*
- Les 21 curseurs du Mode Essentiel restent inchangés (US-02) : la courbe est un
  outil du Mode Avancé, et les quatre gains d'EQ restent accessibles en Essentiel.
  *(Respecté : la courbe est un panneau au-dessus des cartes, aucun paramètre
  ajouté ni retiré ; la fenêtre passe de 340 à 516 px de haut pour le loger.)*

#### F1.6b — Étage de mise en forme

**Statut : supprimé le 2026-09-20, décision produit (voir US-13).** L'étage
autonome est retiré au profit de l'EQ à dynamique relative (F1.6), qui porte la
logique qui compte. Le suivi de pitch est conservé : le détecteur de fondamentale
vit dans le module EQ et alimente l'accrochage harmonique des bandes. Les lots
F1.11 et suivants ne sont pas renumérotés.

#### F1.7b — Compresseur : un curseur, table macro (révision du F1.7)

Related user stories:
- US-05

Le F1.7 initial avait interprété « un curseur Comp » comme un compresseur
classique (seuil exposé, courbe à ratio). La **mesure au signal** a établi un
modèle différent, que le F1.7b reproduit :

- **seuil fixe ≈ −50 dBFS**, jamais exposé, jamais bougé ;
- **ratio croissant** avec le curseur : 1,50 / 1,63 / 1,79 / 2,05 / 4,37:1 ;
- **make-up croissant** : +1,7 / +7,3 / +13,1 / +18,2 / +29,2 dB — c'est un
  **nivellement automatique** qui remonte fortement les signaux faibles ;
- **jamais désactivé par le curseur** : à 0 % il compresse déjà à 1,5:1.

Acceptance Criteria:
- [x] AC1–AC3 de US-05 vérifiés. *(Courbe statique en `Custom` : 5,0/6,7/8,3 dB
  théoriques suivis à ±1 dB ; vumètre ≤ 1 dB de la réduction appliquée ;
  cohérence Macro/Custom vérifiée en comparant deux processeurs isolés. Précision
  de mise en œuvre : la détection Macro/Custom repose sur les **gestes**
  (`parameterGestureChanged`), pas sur les valeurs — une automation du curseur
  ne doit jamais faire sortir du mode `Custom` pendant qu'elle le pilote.)*
- [x] AC4 (fidélité F1.7b) : le macro reproduit exactement la table retenue,
  seuil −50 dBFS inclus, et le niveau de sortie **monte** avec le curseur
  (nivellement). Vérifié au harnais en régime établi, ±1,5 dB sur six mesures
  (3 positions × signal fort/faible), et aux tests par une copie de la table
  indépendante du code testé.

Edge Cases:
- Si `comp_on` est Off, alors aucun traitement, quel que soit `comp_amount` —
  transparence bit-à-bit. C'est le seul chemin vers la transparence : la table
  macro, elle, n'est jamais désactivée par son curseur.
- Si `comp_amount` est à 0 % en mode macro, alors le compresseur reste actif
  à 1,5:1 avec +1,7 dB de make-up, comme la cible.
- Si `comp_autogain` est actif (mode `Custom`), alors le niveau de crête reste
  constant à ±1 dB entre deux réglages avancés d'intensité croissante.
  *(Vérifié : crête −6,02 dBFS d'entrée préservée.)*

Constraints:
- Mode Essentiel : un curseur `comp_amount` + interrupteur `comp_on`. Les 6
  paramètres avancés (`comp_on`, `comp_threshold_db`, `comp_ratio`,
  `comp_release_ms`, `comp_makeup_db`, `comp_autogain`) sont cachés, mais
  actifs et automatisables. *(Vérifié : AC4 — le macro à 70 % reproduit la
  table à 0,5 dB près au travers du processeur, AutoGain coupé et make-up de
  la table posés en `Custom` ; AC5 — geste avancé ⇒ `Custom`, curseur gelé,
  mesuré à 0,0 dB d'écart ; AC6 — geste sur le curseur ⇒ retour au pilotage,
  badge `CUSTOM` affiché dans l'interface.)*
- Défauts : `comp_on` Off, `comp_amount` 0 % — AC2 de US-02 exige la
  transparence aux défauts du catalogue, et un compresseur actif par défaut la
  violerait. Les presets d'usine qui demandent du compresseur posent les deux.
- `comp_ratio` est un **flottant continu** (1,5 → 20,0) et non un choix figé :
  la table mesurée contient des rapports non standards (1,63 / 2,05 / 4,37)
  qu'un choix ne peut pas exprimer, et AC4 exige ±0,5 dB. Son unité est `x`,
  suffixe d'hôte `ratio_x` (relevé réel sur l'hôte de vérification).
- Le make-up de la table fait remonter le bruit de fond avec les signaux
  faibles (+29,2 dB à 100 %) : c'est l'architecture de la table, où le
  gate en amont n'est pas un luxe.

#### F1.8 — De-esser et plosives

Related user stories:
- US-10

Acceptance Criteria:
- [x] AC1–AC3 de US-10 vérifiés.
- [x] Fidélité à la cible : plage utile étroite et saturation précoce
  (−3,12 dB à 50 %, −3,64 dB à 100 % sur la cible). Mesuré chez nous **au
  niveau hôte** sur une sonde franche au-dessus du croisement (9 kHz pour un
  croisement à 5 kHz) : −2,93 dB à 50 %, −3,22 dB à 100 % — soit −0,19 et
  −0,42 dB des valeurs visées, et 0,29 dB gagnés entre 50 et 100 %
  (elle en gagne 0,52 : la saturation est bien précoce). Le vumètre du module
  colle à la réduction appliquée à la voie haute à moins de 0,3 dB.

Edge Cases:
- [x] Si la bande de détection ne contient aucun contenu, alors aucune réduction
  n'est appliquée (pas de réduction à vide). Vérifié sur un silence **et** sur une
  voix sans sifflante ; l'attaque de 2 ms du détecteur est ce qui rend le critère
  vrai (un détecteur à attaque instantanée réduisait à vide pendant la première
  milliseconde de tout signal — attrapé par ce test le 2026-09-19).

Constraints:
- Détection adaptative au niveau du signal — le seuil suit l'enveloppe de la voie
  basse — mais avec bande de détection réglable et mode split-band.

#### F1.9 — Drive / saturation

Related user stories:
- US-02

Acceptance Criteria:
- [x] AC1 : `drive_flavor` offre au moins 4 modèles (bande, lampe, console, fuzz) ;
  les 4 produisent des spectres de distorsion mesurablement distincts (différence
  > 3 dB sur au moins une bande de 1/3 d'octave à 80 % d'intensité). Mesuré au
  niveau hôte sur sonde à 1 kHz : pire paire **Tape vs Console, 5,0 dB** — les
  harmoniques sont repliées dans leurs bandes de 1/3 d'octave avant comparaison
  (10f et 11f partagent une bande à 1 kHz, elles y sont sommées, comme le ferait
  un analyseur).
- [x] AC2 : `drive_mix` à 0 % rend le module transparent (différence ≤ −120 dBFS).
  Mesuré : écart **nul** (module court-circuité, pas fondu). Satisfait deux fois
  plutot qu'une : le fondu à 0 % est *exactement* sec, et le processeur
  court-circuite le module quand le mix est à 0 — vérifié par mutation
  (casser le fondu est attrapé par le test d'interpolation du mix).
- [x] AC3 : aucun échantillon de sortie ne dépasse +6 dBFS à `drive_amount` 100 %.
  avec une entrée à −12 dBFS crête. Mesuré sur les 4 saveurs : crêtes entre
  **−12,1 et −11,7 dBFS**, soit 17 dB sous le plafond. Le drive est calibre pour
  ramener une entrée au niveau nominal à ce meme niveau — un signal plus faible
  est remonte (compression vers le haut), un signal plus fort est compresse.
- [x] Cas limite : `drive_amount` à 0 % court-circuite le module (transparence
  bit-exacte aux défauts, AC2 de US-02).

Edge Cases:
- [x] Un signal faible (−24 dBFS) est remonté de plus de 3 dB à 100 % : c'est le
  comportement attendu d'un drive, et c'est ce qui borne la sortie.

#### F1.10 — Image : doubler et largeur stéréo

Related user stories:
- US-08

Acceptance Criteria:
- [x] AC1–AC3 de US-08 vérifiés au niveau hôte (`check_width` du harnais) :
  transparence bit-exacte à 100 %, mono strict à 0 % (corrélation **1,0000**),
  mono bass sous la coupure (corrélation à 30 Hz **0,99993**), aucune composante
  subsonique ajoutée (pire ajout **−113,8 dBFS**).
- [x] AC4 : `doubler_amount` à 0 % rend le doubler transparent (différence
  ≤ −120 dBFS) ; à 100 %, la matière **ajoutée** est décorrélée. Mesuré au niveau
  hôte : corrélation de (sortie 100 % − sortie 0 %) entre L et R = **−0,345**.

Constraints:
- C'est la matière **ajoutée** qui porte le critère, pas la sortie entière. Le
  sec reste entier (« sans effet de niveau »), donc sur une voix
  mono il domine `corr(L,R)` : exiger < 0,5 sur la sortie complète serait
  inatteignable **par construction**, quelle que soit la qualité du doubler. Le
  test unitaire qui l'exigeait a été amendé le 2026-09-19 — il était faux, pas le
  module.
- Le doubler crée QUATRE voix : deux par côté, de délais décalés *dans* un côté et
  symétriques *entre* côtés, de désaccordage **opposé** entre côtés, chacune
  dérivant sur son propre LFO (0,40 / 0,53 / 0,47 / 0,36 Hz). L et R ne partagent
  aucune voix : c'est ce qui décorelle, et un chorus classique (même LFO des deux
  côtés) ne le satisfait pas.
- Le mono bass est un **passe-haut Linkwitz-Riley 4** appliqué au side, à 120 Hz
  (constante de conception, §3.4). Sa pente de −24 dB/octave fait tomber le side
  sous 1 % une octave et demie sous la coupure.

Edge Cases:
- [x] Un signal strictement mono n'est pas modifié par la largeur (le side est
  nul, il n'y a rien à multiplier) ; le doubler, lui, y reste actif — c'est lui
  qui crée la matière stéréo. Vérifié : sortie bit-exacte à 200 % de largeur.
- [x] Le module entier est court-circuité à `doubler_amount` 0 % ET `width_amount`
  100 % : la sortie est identique au bit près.

#### F1.11 — Delay

Related user stories:
- US-02

Acceptance Criteria:
- [ ] AC1 : `delay_sync` offre les 21 divisions rythmiques retenues
  (`1/32T` … `2/1D`) et `delay_time_ms` couvre 1–2000 ms en mode libre.
- [ ] AC2 : `delay_ducking` à 100 % réduit la contribution du delay d'au moins 12 dB
  pendant la voix, et le delay remonte en moins de 300 ms après la fin du mot.
- [ ] AC3 : `delay_feedback` est plafonné à 95 % et le delay ne s'emballe jamais
  (aucune sortie croissante au-delà de 10 s de silence en entrée).

Edge Cases:
- Si le tempo de l'hôte change, alors recalculer le temps de delay sans clic
  (variation < 1 dB sur 20 ms).

#### F1.12 — Reverb

Related user stories:
- US-02

Acceptance Criteria (amendé deux fois le 2026-09-21 ; retour utilisateur final :
« je veux toujours mes 4 knobs pour modifier les différentes reverb (short,
small, big, lush) comme dans le plug de base ») :
- [x] AC1 : QUATRE curseurs publics (Short / Small / Big / Lush), un par
  sous-réseau — quatre moteurs cumulés, un curseur chacun. Chaque moteur porte
  son RT60 nominal (0,4 / 1,1 / 2,2 /
  4,2 s) et son damping de boucle (Short sombre → Lush clair) ; les queues sont
  deux à deux distinctes d'au moins 30 % (persistance à plancher absolu,
  vérifiée au signal C++ ET au niveau hôte).
- [x] AC1 bis : les moteurs sont TOUJOURS cumulés (structure retenue) ;
  Small+Big produit une queue dense en attaque ET longue (fenêtres 0,15–0,24 s
  et 0,9–1,2 s).
- [x] AC2 : le pre-delay préréglé vaut 20 ms (valeur retenue), vérifié
  au signal à ±5 ms (onset = impulsion + pre-delay + délai de réseau).
- [x] AC3 : les 4 curseurs à 0 % rendent le module transparent (bit-exact).
- [x] AC4 : aucun IR embarqué : réseau algorithmique de notre fabrication
  (combs à longueurs premières + allpass + damping), aucune donnée tierce.

Constraints:
- Aucune IR tierce. Génération par synthèse ou par enregistrement propre.
- QUATRE paramètres publics (`reverb_short_pct`, `reverb_small_pct`,
  `reverb_big_pct`, `reverb_lush_pct`) ; le pre-delay reste préréglé en interne.

#### F1.13 — Sortie, latence et interface Essentiel/Avancé

Related user stories:
- US-02, US-12

Acceptance Criteria:
- [x] AC1 : AC1–AC2 de US-12 vérifiés (latence reportée = mesurée = 0, ±1
  échantillon, impulsion au centre de la chaîne tous paramètres poussés ;
  affichage smp + ms dans la barre supérieure de l'éditeur).
- [x] ~~AC2 : le basculement Mode Essentiel / Mode Avancé n'entraîne aucune
  variation audio (différence ≤ −120 dBFS) et ne réinitialise aucun paramètre.~~
  **Supprimé le 2026-09-22** : le Mode Avancé n'existe plus, la bascule n'a plus
  d'objet (voir le retrait des 19 paramètres `Advanced` au §3.3).
- [x] AC3 : `bypass` est un vrai contournement : différencier l'entrée et la sortie
  avec `bypass` actif donne ≤ −120 dBFS, **y compris quand tous les modules sont
  actifs** (mesuré au hôte : −156,5 dB ; au test C++ : bit-exact — le
  `processBlockBypassed` est l'identité audio, aucun état, aucun traitement).
- [x] AC4 : `output_gain` couvre −24 à +24 dB ; le gain mesuré correspond à ±0.1 dB
  (vérifié à −24/−12/−6/0/+6/+12/+24 dB, au harnais hôte et au test C++).

#### Phase 1 Completion Checklist

- [ ] Toutes les user stories P0 de la Phase 1 implémentées
- [ ] Le flux principal fonctionne de bout en bout sur une vraie prise de voix
- [ ] Les 20 presets d'usine se chargent et produisent un signal non silencieux
- [ ] Aucune régression sur les outils de `tools/` (DO NOT CHANGE)
- [ ] Le rapport `verification_report.md` est généré avec tous les seuils atteints

---

### Phase 2 — Qualité et pièce

**Goal:** ajouter les deux fonctions qui n'existent pas dans l'existant et qui
conditionnent l'adoption : mode haute qualité et dé-reverb.
**Dependency:** Phase 1 complète

*(Le lot **F2.1 « Correction de justesse » a été retiré le 2026-09-19** avec la story
US-07 qu'il servait : la fonction n'est plus au périmètre du produit. Les lots
suivants ne sont pas renumérotés — un numéro de lot est une référence, pas un
compteur, et le réutiliser rendrait illisibles les notes de progression déjà
écrites.)*

#### F2.2 — Mode haute qualité (oversampling)

Related user stories:
- US-09

Acceptance Criteria:
- [x] AC1–AC3 de US-09 vérifiés (tests C++ + harnais hôte, 2026-09-22).

Edge Cases:
- Si l'hôte tourne à 192 kHz, alors le facteur d'oversampling est plafonné à 2×
  pour respecter G3.

Constraints:
- L'oversampling ne s'applique qu'aux étages non linéaires (Drive, et le
  waveshaper interne du De-esser si présent). L'EQ et les filtres linéaires ne sont
  pas suréchantillonnés.

#### F2.3 — Dé-reverb

*(Le lot **F2.3 « Dé-reverb » est retiré le 2026-09-22** avec la story US-14
qu'il servait : la fonction n'est plus au périmètre du produit — décision du
détenteur du produit. Le lot F2.2 était le seul de Phase 2 ; les numéros de lot
ne sont pas renumérotés, un numéro est une référence, pas un compteur.)*

#### Phase 2 Completion Checklist

- [ ] Le rendu est vérifié sur un corpus de voix réel *(le flux bout en bout
  sur une vraie prise de voix — en attente d'un fichier vocal du détenteur)*
- [ ] La latence reportée reste exacte dans les deux modes (zero-latency / HQ)
  *(vérifié le 2026-09-22, F2.2 : invariant « mesurée = reportée » en C++,
  rapport honnête au harnais via la PDC de l'hôte)*
- [ ] G3 tenu avec HQ 4× actif *(4× sous 96 kHz, 2× au-delà — implémenté en
  F2.2 ; charge CPU non encore mesurée)*

---

### Phase 3 — Après V1 (hors périmètre de cette phase)

Ces éléments ne font pas partie de V1. Ils ne doivent pas être implémentés
maintenant.

- Métrologie LUFS, true peak et spectre détaillé (US-16, P1)
- Presets cloud, étiquettes et recherche
- Formats AU (U2) et CLAP (U3)
- Version standalone
- Import d'IR utilisateur (US-17, P2)
- Build AAX (US-18, P2)
- EQ dynamique par bande, compresseur multibande
- Accessibilité clavier complète et navigation lecteur d'écran

---

## 5. Non-Functional Requirements

| Category | Requirement | Target |
|----------|-------------|--------|
| Performance | Charge CPU chaîne complète, 48 kHz stéréo, zero-latency | ≤ 6 % d'un cœur (Ryzen 5 5600 / Core i5-12400) |
| Performance | Charge CPU chaîne complète, Mode HQ 4× | ≤ 18 % d'un cœur, mêmes conditions |
| Performance | Latence en mode zero-latency | 0 échantillon |
| Performance | Temps de chargement d'un preset | ≤ 100 ms |
| Performance | Allocations mémoire dans `processBlock` | 0 |
| Reliability | Stabilité sur 10 min de traitement continu à 48 kHz | 0 dépassement de bloc, 0 NaN, 0 dénormale persistante |
| Reliability | Taux d'échantillonnage supportés | 44.1, 48, 88.2, 96, 176.4, 192 kHz |
| Reliability | Sensibilité aux dénormales | flush-to-zero activé sur les filtres récursifs |
| Security | Surface réseau | aucune connexion sortante |
| Privacy | Collecte de données | aucune ; aucun identifiant machine, aucune télémétrie |
| Privacy | Traitement des fichiers utilisateur | lecture/écriture limitée au dossier de presets de l'utilisateur |
| Error handling | État audio sur preset invalide | inchangé, erreur signalée dans l'interface |
| Error handling | État audio sur dépassement de bloc | inchangé, compteur interne incrémenté, message journalisé |
| Accessibility | Navigation clavier sur tous les contrôles | 100 % des contrôles atteignables |
| Accessibility | Contraste du texte de l'interface | ≥ 4.5:1 |

---

## 6. Success Metrics

### Launch Criteria

Must all be true before shipping:

- [ ] **Metric 1** : 80 % des testeurs (n ≥ 10) obtiennent un rendu jugé diffusable
  en ≤ 2 minutes sans ouvrir le Mode Avancé.
- [ ] **Metric 2** : 100 % des critères d'acceptation des stories P0 passent sur la
  build Release.
- [ ] **Metric 3** : aliasing en Mode HQ 4× : aucune composante > −80 dBFS dans la
  bande 0–20 kHz avec une entrée à 8 kHz et `drive_amount` 80 %.
- [ ] **Metric 4** : transparence à 0 % — chaque module à 0 % donne une différence
  ≤ −120 dBFS.
- [ ] **Metric 5** : latence reportée égale à la latence mesurée, ±1 échantillon,
  dans les deux modes.
- [ ] **Metric 6** : 20 presets d'usine livrés, tous produisant un signal non
  silencieux et aucun écrêtage au-dessus de 0 dBFS.

### Ongoing KPIs

- Primary : taux de sessions où un preset d'usine est utilisé sans passer en Mode
  Avancé (cible ≥ 70 %), mesuré par un formulaire de retour utilisateur volontaire,
  puisque aucune télémétrie n'est collectée.
- Secondary : nombre de presets utilisateur sauvegardés par utilisateur actif
  (cible ≥ 2 au premier mois).
- Guardrail metrics : taux de rapports de plantage par session (< 0.1 %), et
  latence reportée incorrecte signalée (0 cas).

---

## 7. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| La correction de justesse introduit des artefacts audibles (warble, clics) | High | High | **Écarté le 2026-09-19 : la fonction est retirée du produit** (US-07). Le risque était réel et aucun des critères AC1–AC4 n'a jamais été satisfait ; le module était de toute façon resté non implémenté. *(Mitigation d'origine, gardée pour mémoire : `tune_strength=0` strictement transparent, et essai sur corpus vocal avant d'exposer le P0.)* |
| La charge CPU dépasse G3 avec l'EQ interactif et le spectre actifs | Medium | Medium | Calculer l'analyseur de spectre à 20 images/s sur un thread séparé, jamais dans `processBlock` ; budgeter la FFT hors du thread audio |
| Le périmètre V1 (55 paramètres) dépasse la capacité de livraison | Medium | High | Le Mode Essentiel (21 paramètres) est le contrat de sortie ; le Mode Avancé a déjà été réduit le 2026-09-18 (§3.4), le module Justesse retiré le 2026-09-19, le low cut figé et la bande Low-Mid retirée le 2026-09-20 ; les modules de Phase 2 peuvent être désactivés par défaut sans casser le produit |
| Une IR de reverb ou un preset dérive d'un produit tiers et crée un risque juridique | Low | High | Interdiction explicite (§1.3, §3.2) ; chaque IR est générée ou enregistrée par nos soins, avec traçabilité dans le dépôt |
| Les repères de conception sont traités comme une cible sonore à répliquer | Medium | Medium | Ce sont des **repères ergonomiques**, pas une cible sonore : les plages sont à valider au ressenti testeur |
| L'ergonomie de l'existant est recopiée au lieu d'être comprise | Medium | High | Chaque repère sert de **point de départ**, et les défauts connus de la catégorie (compression jamais désactivée à 0 %, gain caché, dépendance à une activation en ligne) sont explicitement **corrigés**, pas reproduits |
| Projet personnel : le temps disponible de l'auteur limite la livraison | High | Medium | Le Mode Essentiel (22 paramètres) est le contrat de sortie ; les lots de Phase 1 sont indépendants et livrables séparément (§5) |

---

## 8. Open Questions

- [x] ~~**Nom du produit et système de nommage**~~ **Tranché le 2026-09-18 :
  famille `OD` + matière** — `OD Vox` (v1, chaîne vocale), `OD Master`
  (mastering), puis les matières suivantes. Le produit n'étant **pas
  commercialisé**, aucune recherche d'antériorité de marque n'est requise ; la
  seule contrainte restante est technique — JUCE/VST3 exigent des codes de
  **exactement 4 caractères**, uniques et stables. Retenus : `OdAu` (fabricant) et
  `Odvx` (produit).
  *Historique de la recherche, conservé parce qu'il reste réutilisable :* la piste
  française (`Sève`, `Pupitre`, `Mélisme`, `Cantilène`, `Ambre`) a été abandonnée
  au profit de l'anglais. Le marché anglophone s'est révélé **saturé** — chaque mot
  court et évocateur essayé était déjà pris, dans notre catégorie ou juste à côté :
  `Halo` (Halo Effect), `Loom` (AIR Music Technology), `Onyx` (Mackie Onyx),
  `Forge` (Drumforge), `Ember` (Yum Audio), `Timbre` (zplane), `Vocalise`
  (Heavyocity), `Atelier` (GRM Tools), `Nacre`, `Aube`, `Diapason`, `Tessitura`,
  `Vocalane`. La preuve la plus parlante : **« Burnish Vocal Chain » de Vitric
  Audio**, publié **six jours** avant cette analyse — notre catégorie exacte, et
  exactement le mot qu'on aurait proposé.
  **Conséquence** : ne pas chercher un mot anglais libre, mais **construire un
  token**. C'est ce qu'est `OD` — deux lettres, aucune prétention à être un mot,
  une **construction** propre et déclinable, une signature qui fonctionne devant
  n'importe quelle matière.
  Structures écartées au passage : `Vireo Voice` / `Vireo Aria` (maison + matière),
  tout aussi valables, mais sans intérêt dès lors qu'il n'y a pas de mise sur le
  marché à protéger.
- [x] ~~**Modèle économique et licence**~~ **Tranché le 2026-09-18 : projet
  personnel, non commercialisé.** Donc : aucune authentification, aucun DRM,
  aucune télémétrie, aucune période d'essai, aucune distribution à prévoir. Les
  questions de prix, de support et de compatibilité DAW exhaustive tombent.
- [x] ~~**Valeurs numériques de l'étage de mise en forme**~~ **Livré et calibré le
  2026-09-19 (F1.6b), puis **étage retiré le 2026-09-20** (voir US-13) : les
  valeurs restent documentées pour l'historique, le module n'existe plus.** Cinq zones relatives à la fondamentale (0,67 / 2 / 3,5 / 6 /
  10 × f0), seuil commun à **−34 dBFS de bande** (une passe-bande à Q = 1 lit 4 à
  5 dB au-dessus du niveau de chaque harmonique d'une voix) et 34 dB de dynamique :
  aucune zone n'agit à −40 dBFS, toutes sont à plein régime à −6 (AC2 de US-13).
  *Reste ouvert, sans impact sur la V1* : un seul jeu de zones, ou des variantes
  par tessiture ? La question est reportée à la Phase 2, une fois le module
  entendu sur des voix réelles.
- [x] ~~**Formats de V1**~~ **Tranché** : **VST3 seul, Windows**. AU, CLAP et AAX
  sont sans objet pour un usage personnel.
- [x] ~~**Nature convolutive ou algorithmique de la reverb**~~ **Tranché :
  réseau algorithmique**, sans aucune IR tierce — sur trois indices convergents, avec
  la limite de principe documentée.
- [ ] **Position du Low Cut dans la chaîne** : l'ordre du pipeline (§3.1) est une
  décision de conception, à valider au ressenti sur des voix réelles.
- [ ] **Macro globale** : faut-il un curseur unique qui dose toute la chaîne, en
  plus des curseurs par module ? Sans impact sur la V1 ; à trancher si un usage s'en
  fait sentir.
- [x] ~~**Nombre de presets d'usine**~~ **Tranché le 2026-09-18 : 20 presets.** La
  cible de 60 venait d'une exigence de mise sur le marché, sans objet pour un projet
  personnel. Aligné en AC2 de US-01, dans la checklist de fin de Phase 1 et dans la
  Metric 6.

---

## 9. Appendix

### Design references

- `docs/FIGMA_KIT.md` — palette, tailles des composants et procédure d'export pour
  redessiner les assets d'interface.
- `tools/verify_plugin.py` — le harnais de vérification hors ligne : c'est lui qui
  porte les seuils cités dans ce document, contrôle par contrôle.
- `tools/generate_knob_frames.py` — génère `assets/knob_atlas.png` et
  `assets/knob_atlas_meta.h`, l'atlas de frames des knobs.
- `verification_report.md` — la dernière exécution du harnais sur le VST3 installé.

### Related docs

- `README.md` — construire le plugin, lancer les tests et le harnais.
- `THIRD_PARTY.md` — licences des composants embarqués.
- `LICENSE` — AGPL-3.0.

### Glossary

| Terme | Définition |
|---|---|
| **Mode Essentiel** | Niveau d'interface exposant 21 paramètres, un curseur d'intensité par module. |
| **Mode Avancé** | Niveau d'interface exposant 33 réglages supplémentaires — soit 54 paramètres au total avec le Mode Essentiel (49 déclarés à ce jour, la différence étant les réglages fins des modules pas encore écrits). |
| **L'existant** | La catégorie des plugins de chaîne vocale « tout-en-un » à un curseur par module, dont les réglages fins sont figés en interne (voir §1.1). Aucun produit n'est nommé par ce document. |
| **Étage de mise en forme** | *(Retiré le 2026-09-20)* Étage autonome de la première conception (curseur `lift_amount`, 5 zones relatives à f0, AutoGain perceptuel) : son action recouvrait celle de l'EQ à dynamique relative, et il est supprimé. Le suivi de pitch qu'il hébergeait est conservé dans le module EQ. |
| **AutoGain perceptuel** | Makeup lent (300 ms) qui ramène la sonie perçue de la sortie sur celle de l'entrée, mesurée par une pondération K. Dans l'**EQ à dynamique relative**, il ne compense que l'effet de la dynamique relative — jamais les réglages de l'utilisateur — et il est pondéré par l'énergie du **programme** puis par l'**engagement** : 0 dB à plein régime (la courbe reste la réponse) comme sous le plancher (le module ne fait rien). |
| **Dynamique relative (EQ)** | Le gain **appliqué** par une bande est une fraction de son gain **réglé** ; la fraction est la même pour les quatre bandes et suit le niveau du programme (plancher −55 dBFS, plein à −25). La **forme** de la réponse suit donc la matière ; le **niveau**, lui, suit le réglage. |
| **Zero-latency** | Mode sans oversampling ni lookahead, latence reportée à 0 échantillon. |
| **Mode HQ** | Mode avec oversampling 2× ou 4× sur les étages non linéaires. |
| **Plosive** | Consonne explosive (P, B, T) produisant un pic d'énergie sous 150 Hz. |
| **GR (gain reduction)** | Atténuation appliquée par le compresseur, affichée en dB. |
| **Null test** | Test de transparence : on soustrait la sortie de l'entrée ; un résultat ≤ −120 dBFS signifie « aucun effet audible ». |

### Next Skill Handoff

Next recommended skill: **Architecture Planner**

Handoff input: ce PRD, en particulier le résumé produit, la persona principale, les
user stories, le périmètre V1, les non‑goals, la spécification technique, la section
DO NOT CHANGE, les modèles de données, les contrats d'interface, les exigences de la
Phase 1, et les questions ouvertes.

---

## Coding Agent Handoff Prompt

```txt
Read the PRD below carefully before writing any code.

Start with Phase 1 only.
Do not implement anything from Phase 2 or Phase 3.
Do not modify anything listed in the DO NOT CHANGE section.
If any requirement is ambiguous, ask before implementing.

After Phase 1 is complete, provide:
- files changed
- implementation summary
- validation steps
- risks
- suggested tests

[PASTE PRD HERE]
```
