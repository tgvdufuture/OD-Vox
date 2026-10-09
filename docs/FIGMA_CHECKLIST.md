# Checklist Figma — tous les assets d'OD Vox

Fenêtre réelle : **980 × 516 px** (barre du haut 44 + écran EQ 168 + cartes 340).
Fenêtre de travail Figma conseillée : dessine chaque asset dans son propre cadre, exporte en **PNG 2×**, fond transparent.

Convention générale : **sélectionne uniquement l'élément** avant Export (jamais un rectangle de fond derrière).
Pour les knobs : encoche **pointant vers le haut** (position midi) — la rotation se fait en code.

---

## 1. FOND & STRUCTURE (le plus important — c'est ça qui donne l'ambiance)

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 1 | `bg.png` | 980 × 516 | Fond complet : alu brossé + patine + ombres d'ambiance. Sans AUCUN contrôle dessus |
| 2 | `card.png` | ~150 × 160 (9-slice possible) | Plaque de carte de module : biseau, bord, ombre portée |
| 3 | `topbar.png` *(optionnel)* | 980 × 44 | Bande du haut si tu veux la différencier du fond |

## 2. ÉCRANS (encarts enfoncés — la courbe EQ reste dessinée par code par-dessus)

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 4 | `screen_eq.png` | 956 × 168 | Grand écran de courbe : vitre noire, liseron enfoncé, reflets subtils |
| 5 | `vu_window.png` | ~14 × 90 (vertical) | Fenêtre de vumètre enfoncée (le remplissage niveau reste en code) |

## 3. KNOBS (rotatifs — ton `Knob.png` est déjà intégré ✔)

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 6 | `knob.png` | 64 × 64 | ✔ **Déjà fait** (ton disque machiné) |
| 7 | `knob_hero.png` | ~150 × 150 | GROS knob central si tu veux un point focal (knob « macro ») |
| 8 | `knob_small.png` *(optionnel)* | 48 × 48 | Variante compacte pour les cartes denses |

Astuce knob : dessine le corps + l'encoche, mais **pas** l'arc de valeur ni le texte — je les dessine en code par-dessus (couleur accent configurable).

## 4. TOGGLES (interrupteurs 2 états)

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 9 | `toggle_on.png` | 44 × 22 | État activé |
| 10 | `toggle_off.png` | 44 × 22 | État désactivé |

## 5. BOUTONS

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 11 | `button.png` | 86 × 26 | Bouton standard (Calibrate, Copy…) — texte dessiné par code |
| 12 | `button_small.png` | 44 × 26 | Petits boutons (‹ › du menu presets, A/B) |
| 13 | `preset_menu.png` | 420 × 28 | Plaque du menu de presets (le nom du preset est dessiné par code) |

## 6. LED (activité des modules)

| # | Asset | Taille utile (px) | Détails |
|---|-------|-------------------|---------|
| 14 | `led_on.png` | 12 × 12 | Dôme allumé (cuivre) — la respiration reste animée en code |
| 15 | `led_off.png` | 12 × 12 | Dôme éteint |

---

## Rappel : ce qui reste TOUJOURS en code (ne pas dessiner)

- La **courbe d'EQ** et l'**analyseur de spectre** (dynamiques)
- Les **valeurs** affichées (%, dB, ms) et les labels
- La **rotation** des knobs et l'**arc de progression**
- Le **remplissage** des vumètres (chute amortie) et des LED (respiration)
- Le texte en général (gravé par le LookAndFeel)

---

## Palette officielle (à recopier dans Figma)

| Nom | Hex |
|---|---|
| background | `#b3afa8` |
| backgroundLo | `#98948d` |
| card | `#c9c5be` |
| cardBorder | `#7f7b73` |
| text | `#26231e` |
| textDim | `#6d675e` |
| accent (cuivre) | `#b5763a` |
| accentHot | `#e0994f` |
| accentCold (acier) | `#82878e` |
| track (écrans/pistes) | `#26231f` |
| metalHi | `#edeae2` |

## Modules à prévoir sur les cartes (13 cartes, 2 rangées)

Entrée : **Input Gain** · Calibrate · **Gate** — Propreté : **Low Cut** —
EQ : **Low · Mid · High · Air** + EQ On — Dynamique : **Comp** · **De-ess** —
Matière : **Drive** (+ HQ) · **Doubler** · **Width** —
Espace : **Delay** (+ Time/Sync/Ducking) · **Reverb Short/Small/Big/Lush** · **Output Gain**.

## Ordre de priorité de livraison

1. `bg.png` → changement d'ambiance immédiat
2. `screen_eq.png` + `card.png` → la structure prend forme
3. `knob_hero.png`, toggles, LED → les éléments vivants
4. Boutons + menu presets → la finition
