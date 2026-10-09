#!/usr/bin/env python3
"""Extrait le layout de l'UI dessinee dans Figma.

Convention (docs/FIGMA_KIT.md) :
  - docs/figma/ui_design.png : le design complet, exporte en PNG 2x (1920x1040)
  - docs/figma/ui_mask.png   : la copie ou chaque controle est une couleur plate :
      knob = rouge pur #ff0000, toggle = magenta #ff00ff,
      ecran = cyan #00ffff, vumetre = jaune #ffff00

Sortie : docs/figma/layout.json (positions en coordonnees plugin 960x520 @1x)
Usage :  python tools/extract_layout.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

ROOT = Path(__file__).resolve().parent.parent
FIGMA_DIR = ROOT / "docs" / "figma"
DESIGN = FIGMA_DIR / "ui_design.png"
MASK = FIGMA_DIR / "ui_mask.png"
OUT = FIGMA_DIR / "layout.json"

# Tolerances de reconnaissance des couleurs plates du masque
CLASS_COLORS = {
    "knob":   (255,   0,   0),
    "toggle": (255,   0, 255),
    "screen": (  0, 255, 255),
    "meter":  (255, 255,   0),
}
TOL = 40  # distance max par canal

PLUGIN_W, PLUGIN_H = 960, 520


def classify_mask(mask_img: Image.Image) -> dict[str, np.ndarray]:
    """Renvoie un masque binaire par classe de controle."""
    a = np.array(mask_img.convert("RGB")).astype(int)
    out = {}
    for name, (r, g, b) in CLASS_COLORS.items():
        out[name] = (
            (abs(a[..., 0] - r) < TOL)
            & (abs(a[..., 1] - g) < TOL)
            & (abs(a[..., 2] - b) < TOL)
        )
    return out


def blobs(binary: np.ndarray, min_px: int) -> list[dict]:
    """Composantes connexes -> centres + bboxes, en coordonnees @1x."""
    binary = ndi.binary_opening(binary, structure=np.ones((3, 3)))
    lab, n = ndi.label(binary)
    res = []
    for i, sl in enumerate(ndi.find_objects(lab)):
        px = int((lab[sl] == i + 1).sum())
        if px < min_px:
            continue
        x0, y0 = sl[1].start, sl[0].start
        x1, y1 = sl[1].stop, sl[0].stop
        res.append(
            {
                "x": x0 / 2.0,
                "y": y0 / 2.0,
                "w": (x1 - x0) / 2.0,
                "h": (y1 - y0) / 2.0,
                "cx": (x0 + x1) / 4.0,
                "cy": (y0 + y1) / 4.0,
                "px2x": px,
            }
        )
    res.sort(key=lambda b: (round(b["cy"] / 20), b["cx"]))
    return res


def main() -> int:
    if not DESIGN.exists() or not MASK.exists():
        print("Il manque les exports. Attendus :")
        print(f"  {DESIGN}")
        print(f"  {MASK}")
        print("Procedure : docs/FIGMA_KIT.md section 5.")
        return 1

    design = Image.open(DESIGN)
    mask = Image.open(MASK)

    if design.size != mask.size:
        print(f"Attention : tailles differentes design={design.size} mask={mask.size}")
        return 1
    if design.size != (1920, 1040):
        print(f"Attention : taille attendue 1920x1040 (PNG 2x), trouve {design.size}")
        print("Je continue quand meme (les positions seront normalisees).")
        sx = design.size[0] / 1920.0
        sy = design.size[1] / 1040.0
    else:
        sx = sy = 1.0

    classes = classify_mask(mask)

    layout: dict = {"plugin": {"w": PLUGIN_W, "h": PLUGIN_H}, "controls": {}}

    # Knobs : on garde la bbox comme cercle englobant ; diametre = moyenne(w,h)
    knobs = blobs(classes["knob"], min_px=400)
    layout["controls"]["knobs"] = [
        {
            "id": f"knob_{i + 1:02d}",
            "x": round(b["cx"] * sx, 1),
            "y": round(b["cy"] * sy, 1),
            "diameter": round((b["w"] + b["h"]) / 2 * sx, 1),
            "bbox": [
                round(b["x"] * sx, 1),
                round(b["y"] * sy, 1),
                round(b["w"] * sx, 1),
                round(b["h"] * sy, 1),
            ],
        }
        for i, b in enumerate(knobs)
    ]

    layout["controls"]["toggles"] = [
        {"id": f"toggle_{i + 1:02d}",
         "x": round(b["x"] * sx, 1), "y": round(b["y"] * sy, 1),
         "w": round(b["w"] * sx, 1), "h": round(b["h"] * sy, 1)}
        for i, b in enumerate(blobs(classes["toggle"], min_px=120))
    ]

    layout["controls"]["screens"] = [
        {"id": f"screen_{i + 1:02d}",
         "x": round(b["x"] * sx, 1), "y": round(b["y"] * sy, 1),
         "w": round(b["w"] * sx, 1), "h": round(b["h"] * sy, 1)}
        for i, b in enumerate(blobs(classes["screen"], min_px=1500))
    ]

    layout["controls"]["meters"] = [
        {"id": f"meter_{i + 1:02d}",
         "x": round(b["x"] * sx, 1), "y": round(b["y"] * sy, 1),
         "w": round(b["w"] * sx, 1), "h": round(b["h"] * sy, 1)}
        for i, b in enumerate(blobs(classes["meter"], min_px=150))
    ]

    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(layout, f, indent=2, ensure_ascii=False)

    n = {k: len(v) for k, v in layout["controls"].items()}
    print(f"OK -> {OUT}")
    print(f"  knobs   : {n['knobs']}")
    print(f"  toggles : {n['toggles']}")
    print(f"  screens : {n['screens']}")
    print(f"  meters  : {n['meters']}")
    print("\nVerifie le JSON, puis dis-le-moi : j'integre au projet (fond design +")
    print("controles JUCE vivants poses aux positions extraites).")

    # Apercu texte compact des positions pour verification rapide
    for k, v in layout["controls"].items():
        if not v:
            continue
        print(f"\n{k} :")
        for c in v:
            if "diameter" in c:
                print(f"  {c['id']}  c=({c['x']},{c['y']}) d={c['diameter']}")
            else:
                print(f"  {c['id']}  xy=({c['x']},{c['y']}) {c['w']}x{c['h']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
