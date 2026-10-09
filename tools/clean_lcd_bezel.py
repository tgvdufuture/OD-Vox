#!/usr/bin/env python3
"""Bezel LCD nettoye pour le mockup EQ.

Part l'image Figma `docs/assets/LCD_fige.png` (LCD avec courbe, handles et
pointilles dessines) et produit `docs/assets/LCD_bezel.png` : la MEME image
mais avec l'interieur du plot vide (verre reconstruit), pret a recevoir la
partie dynamique dessinee en code (courbe, handles, pointilles verticaux).

Ce qui est PRESERVE (statique, vit dans l'image) :
  - le bezel metal + vis, la vitre et sa texture
  - le cadre du plot (3 px)
  - les labels +12 / 0 / -12 et 100 / 1k / 10k

Ce qui est EFFACE (dynamique, sera dessine en code) :
  - la courbe, les 4 handles carres, les pointilles verticaux
  - la ligne 0 dB pointillee (son rythme, dessine a la main dans la
    maquette, est irregulier ; le code la redessine proprement)

Le remplissage reconstruit le verre : mediane par ligne (le verre a un
degrade vertical) + texture residuelle tuilee depuis une zone propre.
"""

from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "docs" / "assets" / "LCD_fige.png"
OUT = HERE.parent / "docs" / "assets" / "LCD_bezel.png"

# Geometrie mesuree au pixel sur LCD_fige.png
PLOT = dict(x0=198, x1=618, y0=119, y1=270)   # interieur du cadre

# Zone propre (verre sans rien dessus) pour echantillonner la texture :
# en haut a droite du plot, au-dessus de la courbe et loin des pointilles
# verticaux (x 256 / 354 / 474 / 582).
TEXTURE_PATCH = (480, 122, 570, 158)            # x0, y0, x1, y1


def glass_fill(img: np.ndarray) -> np.ndarray:
    """Remplit l'interieur du plot avec le verre reconstruit.

    1. mediane par ligne -> le degrade vertical du verre
    2. + texture residuelle (original - mediane) tuilee en miroir
    """
    x0, x1, y0, y1 = PLOT["x0"], PLOT["x1"], PLOT["y0"], PLOT["y1"]
    inner = img[y0:y1, x0:x1]
    lum = inner.mean(axis=2)

    # La mediane par ligne ne doit compter que le verre (pas l'encre) :
    # on prend les pixels clairs de chaque ligne.
    row_med = np.zeros((y1 - y0, 3))
    for i in range(y1 - y0):
        row = inner[i]
        bright = row[lum[i] > 110]
        row_med[i] = np.median(bright, axis=0) if len(bright) else 160.0

    # Texture residuelle depuis une zone propre de la vitre
    tx0, ty0, tx1, ty1 = TEXTURE_PATCH
    patch = img[ty0:ty1, tx0:tx1].astype(float)
    patch_lum = patch.mean(axis=2)
    # residu = ecart a la mediane de chaque ligne du patch (meme logique)
    patch_res = np.zeros_like(patch)
    for i in range(patch.shape[0]):
        bright = patch[i][patch_lum[i] > 110]
        med = np.median(bright, axis=0) if len(bright) else patch[i].mean(axis=0)
        patch_res[i] = patch[i] - med

    # Tuilage miroir pour eviter les coutures
    ph, pw = patch_res.shape[:2]
    fill = np.zeros_like(inner, dtype=float)
    for i in range(y1 - y0):
        base = row_med[i]
        # index miroir dans le patch
        py = i % (2 * ph)
        py = py if py < ph else 2 * ph - 1 - py
        row_res = patch_res[py]
        # repetition horizontale miroir
        tile = np.resize(row_res, (x1 - x0, 3))
        fill[i] = np.clip(base + tile, 0, 255)

    return fill.astype(np.uint8)


def main() -> None:
    im = Image.open(SRC).convert("RGB")
    img = np.array(im)

    x0, x1, y0, y1 = PLOT["x0"], PLOT["x1"], PLOT["y0"], PLOT["y1"]
    fill = glass_fill(img)
    img[y0:y1, x0:x1] = fill

    Image.fromarray(img).save(OUT)
    print(f"OK {OUT} ({im.size[0]}x{im.size[1]})")


if __name__ == "__main__":
    main()
