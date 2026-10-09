"""Comparaison CAPTURE C++ vs MAQUETTEE validee (refonte cartes V6).

Photographie tools/mockup_cards.html (Playwright, recadre sur la scene 980x516),
charge les captures verifications/ui/*.ppm produites par ODVoxSnapshot, et pose
les MEMES sondes pixels aux MEMES cotes editeur sur les deux images : alcoves,
knobs, LED de bande, ecran de valeur COMP, LCD, toggles, niveaux IN/OUT.

Une sonde compare une PROPRIETE (contraste, teinte violette, presence de
contenu), pas une egalite pixel a pixel : le dessin C++ est une transposition,
pas une copie du navigateur. Structure = ce qui doit tenir sur LES TROIS images
(maquette, capture defauts, capture engage) ; dynamique = ce qui doit differer
entre defauts et engage (vumetres IN/OUT remplis).

Usage : python tools/compare_capture_mockup.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def read_ppm(path):
    """P6 minimal : en-tete 3 lignes (le writer du depot n'ecrit pas de
    commentaire), puis octets RGB."""
    with open(path, "rb") as f:
        data = f.read()

    magic, rest = data.split(b"\n", 1)
    assert magic == b"P6", magic
    dims, rest = rest.split(b"\n", 1)
    maxval, rest = rest.split(b"\n", 1)
    w, h = (int(x) for x in dims.split())
    assert maxval == b"255"

    from PIL import Image
    img = Image.frombytes("RGB", (w, h), rest[: w * h * 3])
    return img


def mockup_image():
    """La scene 980x516 de la maquette, photographiee et recadree."""
    from playwright.sync_api import sync_playwright
    from PIL import Image

    html = os.path.join(HERE, "mockup_cards.html").replace("\\", "/")
    shot = os.path.join(HERE, "mockup_cards.png")

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto("file:///" + html)
        page.wait_for_timeout(500)
        page.screenshot(path=shot)
        box = page.evaluate(
            "() => { const r = document.querySelector('.stage')"
            ".getBoundingClientRect();"
            " return [r.x, r.y, r.width, r.height]; }")
        browser.close()

    img = Image.open(shot).convert("RGB")
    return img.crop((int(box[0]), int(box[1]),
                     int(box[0] + box[2]), int(box[1] + box[3])))


def avg(img, x, y, w, h):
    """Luminance moyenne d'une region."""
    region = img.crop((int(x), int(y), int(x + w), int(y + h)))
    px = list(region.getdata())
    return sum(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in px) / len(px)


def violet_ratio(img, x, y, w, h):
    """Part des pixels violets (R et B dominants, teinte de l'accent)."""
    region = img.crop((int(x), int(y), int(x + w), int(y + h)))
    px = list(region.getdata())
    hits = sum(1 for r, g, b in px if r > 80 and b > 120 and b > g + 20)
    return hits / max(1, len(px))


def probe(name, cond, detail, results):
    results.append({"probe": name, "ok": bool(cond), "detail": detail})
    return bool(cond)


def main():
    from PIL import Image, ImageStat

    mockup = mockup_image()
    defaults = read_ppm(os.path.join(ROOT, "verifications/ui/editeur_defauts.ppm"))
    engage = read_ppm(os.path.join(ROOT, "verifications/ui/editeur_engage.ppm"))

    for img in (mockup, defaults, engage):
        assert img.size == (980, 516), img.size

    results = []

    # --- Structure : tient sur les TROIS images -----------------------------
    for tag, img in (("maquette", mockup),
                     ("defauts", defaults),
                     ("engage", engage)):
        # 1. L'alcove heros est creusee : sombre au centre.
        probe(f"[{tag}] heros sombre au centre",
              avg(img, 460, 250, 220, 60) < 70,
              f"{avg(img, 460, 250, 220, 60):.0f} < 70", results)

        # 2. L'interieur d'alcove (zone vide de la bande TON) est plus sombre
        #    que la surface de pedale au-dessus (bande x85..890, y104).
        band = avg(img, 385, 350, 40, 30)
        outside = avg(img, 400, 104, 40, 8)
        probe(f"[{tag}] bande TON plus sombre que la pedale",
              band < outside - 5, f"{band:.0f} vs {outside:.0f}", results)

        # 3. Le knob COMP est pose sur l'alcove : la piece se detache du fond,
        #    quel que soit son style (chrome sur la maquette, MXR noir brillant
        #    dans le plug — ~2x la luminance de l'alcove).
        knob = avg(img, 290, 180, 45, 45)
        floor = avg(img, 230, 180, 30, 45)
        probe(f"[{tag}] knob COMP en relief",
              knob > floor + 10, f"{knob:.0f} vs fond {floor:.0f}", results)

        # 4. L'ecran de valeur du COMP : fenetre sombre sous le disque.
        probe(f"[{tag}] ecran de valeur COMP sombre",
              avg(img, 290, 258, 45, 10) < 60,
              f"{avg(img, 290, 258, 45, 10):.0f} < 60", results)

        # 5. Le LCD : du contenu dessine (variance non nulle dans l'ecran).
        stat = ImageStat.Stat(img.crop((390, 130, 700, 290)))
        spread = sum(stat.stddev[:3]) / 3.0
        probe(f"[{tag}] LCD : contenu present",
              spread > 6.0, f"ecart-type {spread:.1f} > 6", results)

        # 6. Les quatre LED de bande sont posees et VIOLETTES (allumees aux
        #    defauts du catalogue : enables On). Colonne gauche x98, droite x504.
        #    Seuil 0,15 : le coeur violet est petit dans la fenetre 11x11 (bague
        #    sombre autour) — ~0,22 sur la maquette COMME sur la capture.
        for i, (lx, ly) in enumerate(((98, 346), (504, 346),
                                      (98, 423), (504, 423))):
            ratio = violet_ratio(img, lx - 5, ly - 5, 11, 11)
            probe(f"[{tag}] LED bande {i} violette",
                  ratio > 0.15, f"{ratio:.2f} > 0.15", results)

        # 7. La vitre de division : bordure violette detectee (bande DELAY,
        #    x756, y411, 41x16 — on sonde l'anneau pour attraper le bord).
        ratio = violet_ratio(img, 754, 409, 45, 20)
        probe(f"[{tag}] vitre de division a bordure violette",
              ratio > 0.04, f"{ratio:.2f} > 0.04", results)

        # 8. Les toggles du heros : du texte grave (pixels clairs) en bas a
        #    droite de l'alcove.
        region = img.crop((700, 278, 884, 300))
        bright = sum(1 for r, g, b in region.getdata()
                     if 0.2126 * r + 0.7152 * g + 0.0722 * b > 110)
        probe(f"[{tag}] toggles LOW CUT / DC FILTER graves",
              bright > 20, f"{bright} px clairs > 20", results)

        # 9. Les chevrons violets du transport preset (caps rondes a (258,56)
        #    et (452,56), 34 px — troncon violett de ~2 px).
        ratio = violet_ratio(img, 258, 56, 34, 34)
        probe(f"[{tag}] chevron preset violet",
              ratio > 0.03, f"{ratio:.2f} > 0.03", results)

    # --- Dynamique : engage DOIT differer de defauts -------------------------
    # Les vumetres IN/OUT sont remplis apres le rendu de la chaine.
    for name, (x0, x1) in (("IN", (100, 450)), ("OUT", (530, 880))):
        before = avg(defaults, x0, 495, x1 - x0, 4)
        after = avg(engage, x0, 495, x1 - x0, 4)
        probe(f"vumetre {name} rempli apres le signal",
              after > before + 2.0, f"{before:.0f} -> {after:.0f}", results)

    ok = all(r["ok"] for r in results)
    print(json.dumps({"all_ok": ok,
                      "images": {"maquette": "tools/mockup_cards.png (scene)",
                                 "defauts": "verifications/ui/editeur_defauts.ppm",
                                 "engage": "verifications/ui/editeur_engage.ppm"},
                      "probes": results}, indent=2, ensure_ascii=False))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
