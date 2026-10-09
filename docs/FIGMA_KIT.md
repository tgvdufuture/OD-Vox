# Kit Figma pour l'UI d'OD Vox

Objectif : tu dessines l'interface dans Figma (gratuit, dans Chrome), tu exportes
**2 images**, tu les déposes dans `docs/figma/`, et **je fais tout le reste**
(extraction des positions, intégration dans l'éditeur JUCE).

Aucune mesure manuelle, aucune annotation : l'export n°2 est un **calque de
mesure** que je compare pixel par pixel avec l'export n°1.

---

## 1. Setup (5 minutes)

1. Crée un compte sur [figma.com](https://figma.com) (gratuit) et ouvre un nouveau fichier.
2. Crée un **Frame** : clic sur l'icône cadre (ou touche `F`), à droite choisis **Custom** et saisis **960 × 520**. C'est la taille exacte de la fenêtre du plugin.
3. Je te recommande de poser en fond un rectangle `#b3afa8` (l'alu de notre palette) pour travailler sur le bon contraste — mais tu es libre : **ce que tu dessines est ce qui sera rendu**.

## 2. La palette officielle (copier-coller)

Ces couleurs sont celles de l'identité actuelle du plugin. Dans Figma, colle
directement les codes hex dans le sélecteur de couleur.

| Nom | Hex | Usage |
|---|---|---|
| background | `#b3afa8` | fond du panneau (alu) |
| backgroundLo | `#98948d` | bas du dégradé du fond |
| card | `#c9c5be` | plaques des cartes/modules |
| cardBorder | `#7f7b73` | contours, gravures |
| text | `#26231e` | textes principaux (charbon) |
| textDim | `#6d675e` | textes secondaires |
| accent | `#b5763a` | cuivre profond (valeurs, arcs actifs, LED) |
| accentHot | `#e0994f` | cuivre brillant (survol, LED allumée) |
| accentCold | `#82878e` | acier (inactif) |
| track | `#26231f` | écrans et fenêtres enfoncées |
| metalHi | `#edeae2` | lumière des biseaux |
| metalLo | `#757068` | ombres des biseaux |
| knobBody | `#bdb9b1` | corps des knobs |
| knobCap | `#aba79f` | chapeau central des knobs |
| bande EQ Low | `#d98a4a` | cuivre |
| bande EQ Mid | `#e0b96a` | laiton |
| bande EQ High | `#c9ccd1` | argent |
| bande EQ Air | `#9db3c4` | acier bleuté |

Typographie conseillée : **Inter** (police par défaut de Figma) — titres en
Bold 12–18 px, labels 9–11 px, valeurs en SemiBold.

## 3. Les tailles des composants

Pour rester cohérent avec ce que le code sait déjà dessiner/renvoyer :

| Composant | Taille conseillée |
|---|---|
| Knob standard | **52 × 52 px** |
| Knob petit | 42 × 42 px |
| Knob héros (gros central) | 120–150 px |
| Toggle | 26 × 15 px (ou dessine-le à ta façon : interrupteur, fuse, pilulier) |
| Écran (fenêtre enfoncée) | libre — il se redimensionnera à la taille de ta zone |
| LED d'activité | 9–10 px |
| Vumètre vertical | 9–14 px de large |
| Barre du haut | 38–44 px de haut |

**Règle d'or : dessine chaque contrôle dans SA case finale.** Ce que je
mesurerai, c'est la position de chaque contrôle sur l'image — donc ce que tu
poses quelque part sera rendu à cet endroit exact.

## 4. Quoi dessiner : les 12 modules

Liste des cartes du plugin (toutes n'ont pas un knob — certaines sont un
simple toggle) :

1. **Input Gain** — 1 knob (±24 dB)
2. **Gate** — 1 knob (%) + LED + vumètre de réduction
3. **Low Cut** — toggle On/Off + LED
4. **EQ** — 4 knobs (Low/Mid/High/Air) + toggle EQ On + écran de courbe (les 4 bandes sont des couleurs métal : cuivre/laiton/argent/acier)
5. **Comp** — 1 knob (%) + LED + vumètre
6. **De-ess** — 1 knob (%) + LED + vumètre
7. **Drive** — 1 knob (%) + toggle HQ + LED
8. **Doubler** — 1 knob (%) + LED
9. **Width** — 1 knob (100 % = centre) + LED
10. **Delay** — 1 knob + menu (division rythmique) + toggles Sync/Duck + LED
11. **Reverb** — 4 knobs (Short/Small/Big/Lush) + LED
12. **Output** — 1 knob (±24 dB)

Barre du haut : logo **OD VOX**, menu de presets (avec flèches ‹ ›),
boutons **A**, **A›B**, **Calibrate**, latence à droite.

Tu n'es **pas obligée** de montrer les 12 modules : l'esprit « épuré »
(sélection B de la maquette précédente — un gros knob + peu de choses) est
parfaitement jouable. C'est TOI qui décides de l'organisation. Les contrôles
non dessinés resteront accessibles au clic droit / presets comme aujourd'hui.

## 5. L'export : 2 images, 5 minutes

Quand ton design est fait, dans Figma :

1. Sélectionne le Frame de 960 × 520 (toute la fenêtre).
2. Ouvre le panneau **Export** (colonne droite, tout en bas) → clique **+** →
   choisis **PNG, 2×** → **Export** → enregistre sous
   `docs/figma/ui_design.png`
3. **Duplique le Frame** (Ctrl/Cmd + D). Sur cette copie :
   - supprime ou masque **tous tes contrôles décoratifs** si tu veux — en fait
     non : ne touche à rien d'autre que les couleurs ;
   - passe chaque contrôle dans une **couleur plate unique**, sans dégradé ni
     texture :
     - chaque **knob** → cercle plein `#ff0000` (rouge pur),
     - chaque **toggle** → rectangle `#ff00ff` (magenta),
     - chaque **écran** → rectangle `#00ffff` (cyan),
     - chaque **vumètre** → rectangle `#ffff00` (jaune),
     - le reste (fond, plaques, textes) → laisse tel quel.
4. Exporte cette copie en PNG 2× aussi → `docs/figma/ui_mask.png`

C'est tout. Dépose les deux fichiers dans `docs/figma/` du projet et dis-moi.

### Ce que je fais ensuite (automatique)

`tools/extract_layout.py` compare les deux images :

- les zones rouges du mask = **positions exactes des knobs** (x, y, diamètre) ;
- magenta = toggles, cyan = écrans, jaune = vumètres ;
- je lis le texte des labels directement sur `ui_design.png` (OCR),
- je te rends un `layout.json` récapitulatif à valider, puis j'intègre
  l'image design comme fond de l'éditeur JUCE avec les contrôles vivants
  posés aux positions extraites.

## 6. Conseils pour un rendu "plugin pro"

- **Un seul point focal** : un gros élément (knob héros ou écran) attire l'œil,
  le reste est discret.
- **Alignements stricts** : Figma affiche les distances en glissant — garde
  des marges constantes (12/16/24 px).
- **Peu de couleurs** : la palette ci-dessus + une seule couleur d'accent.
- **Profondeur** : les écrans enfoncés (ombre interne), les plaques posées
  (ombre externe), le fond neutre — 3 niveaux suffisent.
- Ombres douces larges plutôt que durs bords noirs.
- Teste ta maquette à 100 % et à 150 % de zoom pour vérifier la lisibilité.

## 7. Pourquoi 2× ?

Le PNG 2× (1920 × 1040) donne deux fois plus de pixels : les mesures de
positions sont deux fois plus précises (demi-pixel) et le fond exporté
reste net sur les écrans HiDPI. Le plugin le remettra à l'échelle 1×
automatiquement.

---

## 8. Tutoriel : ton premier layout en 20 minutes

Objectif : un layout « épuré » complet — barre du haut, écran EQ, grand knob
« LE SON », une rangée de 4 knobs EQ, 4 petits knobs, output. Tu recopies
uniquement des valeurs : X, Y, L (largeur), H (hauteur) dans le panneau de
droite. Aucune estimation à l'œil.

### Étape 0 — Repères d'interface Figma

- **Panneau gauche** : les calques. **Panneau droit** : les propriétés
  (Position X/Y et taille W/H tout en haut, Remplissage en dessous).
- Outils (raccourcis) : `F` cadre, `R` rectangle, `O` ellipse, `T` texte,
  `V` sélection. `Ctrl+D` duplique, `Alt+glisser` copie, `Ctrl+G` groupe.
- Pour un cercle parfait : dessine avec `O` puis tape la même valeur en W et H
  (ou maintiens Maj en dessinant).
- Pour centrer un texte : sélectionne-le, clic sur l'icône d'alignement
  centré dans les réglages de texte (panneau droit).

### Étape 1 — Le cadre (1 min)

1. Touche `F`, à droite choisis **Custom**, taille **960 × 520**.
2. Nomme-le `OD Vox UI`. Le remplissage du frame (champ tout en haut du
   panneau droit) : **#b3afa8**.

### Étape 2 — Barre du haut (3 min)

| Élément | X | Y | L | H | Remplissage | Détail |
|---|---|---|---|---|---|---|
| Texte `OD VOX` | 24 | 16 | — | — | #26231e | Inter, Extra Bold, 20 |
| Pilule preset | 360 | 12 | 180 | 30 | #c9c5be | Rayon des coins : 15 |
| Texte `Init` | centré dans la pilule | | | | #26231e | 12, SemiBold |
| Bouton `A` | 770 | 12 | 34 | 30 | #c9c5be | rayon 15 |
| Bouton `A›B` | 810 | 12 | 46 | 30 | #c9c5be | rayon 15 |
| Bouton `Calibrate` | 862 | 12 | 78 | 30 | **#b5763a** | rayon 15, texte #fff6ea |

Astuce : fais UNE pilule propre, puis `Alt+glisser` pour la copier et ne
change que la largeur et le texte.

### Étape 3 — L'écran EQ + vumètre (2 min)

| Élément | X | Y | L | H | Remplissage | Détail |
|---|---|---|---|---|---|---|
| Écran EQ | 24 | 64 | 600 | 210 | **#26231f** | rayon 10 |
| Fente VU | 640 | 64 | 10 | 210 | #26231f | rayon 5 |
| Remplissage VU | 641 | 190 | 8 | 82 | #b5763a | rayon 4 (le quart bas) |

Optionnel : une courbe dans l'écran avec l'outil Plume (`P`) ou un trait
`L`, couleur **#e0994f**, épaisseur 3.

### Étape 4 — Le grand knob « LE SON » (4 min)

| Élément | X | Y | L | H | Remplissage |
|---|---|---|---|---|---|
| Corps (ellipse `O`) | 688 | 84 | 150 | 150 | #bdb9b1 |
| Chapeau (ellipse) | 713 | 109 | 100 | 100 | #aba79f |
| Valeur `62` (texte) | 688 | 145 | 150 | — | #26231e, 20 Bold, centré |
| Label `LE SON` (texte) | 688 | 244 | 150 | — | #26231e, 12 Bold, centré |

Pour l'encoche : petit rectangle 3 × 40 posé en haut du chapeau
(X = 711, Y = 112), #26231e, rayon 2. (La rotation sera gérée par le code.)

### Étape 5 — Les rangées de knobs (6 min)

Fais UN knob de 52, groupe-le (`Ctrl+G` : corps + chapeau + encoche), puis
duplique-le 3 fois avec `Ctrl+D` et change seulement le X.

**Rangée EQ (4 knobs 52 px, Y = 330) :**

| Knob | X corps | X chapeau (34 px) | Label |
|---|---|---|---|
| Low | 48 | 57 | Low |
| Mid | 148 | 157 | Mid |
| High | 248 | 257 | High |
| Air | 348 | 357 | Air |

Labels : texte 10 px, #6d675e, centré sous chaque knob (Y = 388).

**Rangée petits knobs (4 × 42 px, Y = 330, chapeaux 28 px à +7) :**

| Knob | X corps | X chapeau | Label |
|---|---|---|---|
| Gate | 700 | 707 | Gate |
| Comp | 756 | 763 | Comp |
| Drive | 812 | 819 | Drive |
| Delay | 868 | 875 | Delay |

Labels 10 px, Y = 376.

**Output (52 px) :** corps X = 884, Y = 440 ; chapeau X = 893, Y = 449
(34 px) ; label Y = 498.

### Étape 6 — Export des 2 PNG (5 min)

1. Clique le **nom du frame** `OD Vox UI` → panneau droit tout en bas :
   **Export** → `+` → **PNG 2×** → Export → enregistre
   `ui_design.png` dans `docs/figma/`.
2. Duplique le frame (`Ctrl+D`), nomme la copie `OD Vox MASK`. Sur la copie :
   - clic sur un corps de knob → clic droit → **Sélectionner tout avec le
     même remplissage** → remplissage **#ff0000** (répète pour les chapeaux) ;
   - écran EQ → **#00ffff** ;
   - fente + remplissage VU → **#ffff00** ;
   - textes, pilules, fond : ne rien changer.
3. Exporte ce frame en PNG 2× → `ui_mask.png`, même dossier.

C'est tout : dis-le-moi quand les deux fichiers sont dans `docs/figma/`,
je lance `tools/extract_layout.py` et on valide le `layout.json` ensemble.

### Les 5 erreurs classiques

1. Dessiner HORS du frame — vérifie que tes éléments apparaissent dans le
   calque `OD Vox UI` (panneau gauche), pas au niveau racine.
2. Oublier le rayon des coins des pilules (sinon rectangles bruts).
3. Textes non centrés — utilise l'alignement centré + une largeur fixe.
4. Dans le MASK, recolorer aussi le fond → le masque doit garder fond et
   textes d'origine, SEULS les contrôles changent.
5. Exporter en 1× — vérifie bien **2×** (le fichier doit faire 1920 × 1040).

---

## 9. MODE SIMPLE — Assets individuels (recommandé)

Tu n'exportes PAS la fenêtre complète : tu dessines et exportes **chaque
morceau séparément** (fond transparent), et c'est MOI qui les place dans le
plugin et les anime. C'est la méthode des plugins à sprites : des PNG
individuels de knobs, boutons et plaques, posés et animés par le code.

**Avantages** : pas de masque, pas d'extracteur, dessiner un seul knob
servira partout, et déplacer un contrôle = un simple mot de ta part
(la disposition vit dans le code, pas dans l'image).

### Checklist des fichiers (dans `docs/figma/assets/`)

| Fichier | Contenu | Taille conseillée (2×) |
|---|---|---|
| `bg.png` *(optionnel)* | fond complet de la fenêtre | 1920×1040 |
| `knob.png` | knob standard, **encoche dessinée en position midi (haut)** | 104×104 |
| `knob_hero.png` | gros knob central, encoche en haut | ~300×300 |
| `knob_small.png` | variante petite, encoche en haut | 84×84 |
| `toggle_on.png` / `toggle_off.png` | les deux états de l'interrupteur | ~52×30 |
| `button.png` | plaque de bouton (presets, A/B, Calibrate) | libre |
| `led_on.png` / `led_off.png` | LED d'activité, deux états | ~20×20 |
| `screen.png` | texture/cadre d'écran enfoncé (je dessine la courbe par-dessus) | libre |

### Les 3 règles d'export

1. **Fond transparent** : sélectionne UNIQUEMENT l'élément avant Export
   (pas de rectangle de fond derrière). Figma conserve la transparence.
2. **PNG 2×** : dans le panneau Export, choisis 2× — je redimensionne.
3. **Knobs : encoche en position midi** (pointant vers le haut). La rotation
   selon la valeur est faite par le code — comme sur un vrai knob photo.

### Ce que je fais de chaque PNG

- `knob*.png` → dessiné à chaque frame **tourné** autour de son centre selon
  la valeur du paramètre (−135° à +135°).
- `toggle_*`, `led_*` → image choisie selon l'état, avec transition.
- `button.png` → étiré à la taille des boutons, teinté cuivre si "armed".
- `screen.png` → posé aux emplacements d'écran ; la **courbe d'EQ, les
  valeurs et les vumètres restent dessinés en code par-dessus** (dynamiques).
- La **disposition** (qui va où, à quelle taille) est une table dans le code :
  tu me dis « le héros à gauche, l'écran en haut à droite » et je bouge une
  ligne — aucun re-export nécessaire.

Ce mode remplace la convention des 2 frames (sections 5-6) si tu préfères la
simplicité ; l'extracteur `tools/extract_layout.py` reste utile si un jour tu
veux exporter une vue complète.
