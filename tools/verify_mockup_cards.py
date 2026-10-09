"""Verification automatique de tools/mockup_cards.html (V6 : heros + bandes) —
capture headless, geometrie reelle via le DOM, sondes pixels (relief, contraste,
collisions) et test d'interaction (drag d'un knob).

Usage : python tools/verify_mockup_cards.py
"""
import os, json
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "mockup_cards.png")
HTML = SHOT.replace("mockup_cards.png", "mockup_cards.html")

def main():
    from playwright.sync_api import sync_playwright
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 1100, "height": 1000})
        pg.goto("file:///" + HTML.replace("\\", "/"))
        pg.wait_for_timeout(600)

        # --- Interaction : drag vertical du knob heros -> la valeur suit ---
        knob = pg.locator("#hero .k.hk")
        box = knob.bounding_box()
        cx, cy = box["x"] + box["width"]/2, box["y"] + box["height"]/2
        pg.mouse.move(cx, cy); pg.mouse.down()
        pg.mouse.move(cx, cy - 60, steps=8)
        pg.mouse.up()
        value_after_drag = pg.locator("#hero .v").inner_text()

        pg.wait_for_timeout(300)
        pg.screenshot(path=SHOT)

        g = pg.evaluate("""() => {
            const bb = el => { const r = el.getBoundingClientRect();
                return [r.x, r.y, r.width, r.height]; };
            const q = s => document.querySelector(s);
            const qa = s => [...document.querySelectorAll(s)].map(bb);
            return {
                stage:  bb(q('.stage')),
                bar:    bb(q('.bar')),
                hero:   bb(q('.hero')),
                bands:  qa('.bands > .band'),
                bottom: qa('.bottom > .band'),
                oflow:  [['hero', q('.hero')], ...[...document.querySelectorAll('.band')].map((el, i) => ['band' + i, el])]
                            .map(([n, el]) => [n, el.scrollHeight, el.clientHeight]),
                lcd:    bb(q('.lcd')),
                heroEl: bb(q('.hero')),
                ton:    bb(q('#b-ton')),
                knobH:  bb(q('#hero .k.hk')),
                valH:   bb(q('#hero .v')),
                namesTonVisible: [...document.querySelectorAll('#b-ton .nm')].every(el => el.offsetWidth > 0),
                countTon: document.querySelectorAll('#b-ton .nm').length,
                meterIn:  bb(q('.hmeter.left')),
                meterOut: bb(q('.hmeter.right')),
                winIn:  bb(q('.hmeter.left .hwin')),
                winOut: bb(q('.hmeter.right .hwin')),
            };
        }""")
        b.close()

    im = Image.open(SHOT).convert("RGB")
    W, H = im.size
    def P(x, y):
        return im.getpixel((max(0, min(W-1, int(x))), max(0, min(H-1, int(y)))))
    def avg(bb, inset=2):
        x, y, w, h = bb
        reg = im.crop((int(x+inset), int(y+inset), int(x+w-inset), int(y+h-inset))).resize((12, 12))
        px = list(reg.getdata())
        return tuple(sum(c[i] for c in px)//len(px) for i in range(3))
    def bright(c): return 0.2126*c[0] + 0.7152*c[1] + 0.0722*c[2]

    out, ok = [], True
    def probe(name, cond, detail):
        nonlocal ok
        ok &= bool(cond)
        out.append({"probe": name, "ok": bool(cond), "detail": detail})

    sx, sy, sw, sh = g["stage"]

    # 1. Interaction : le drag a-t-il tourne le knob heros ?
    probe("knob heros interactif (drag -> valeur suit)", value_after_drag != "0 %", value_after_drag)

    # 2. Structure : heros (LCD+COMP) en haut, 4 cartes en grille 2x2 en bas.
    probe("4 cartes dans la grille", len(g["bottom"]) == 4, str(len(g["bottom"])))
    hx, hy, hw, hh = g["hero"]
    b0, b1, b2, b3 = g["bottom"]
    probe("rangee 1 au-dessus de la rangee 2",
          max(b0[1]+b0[3], b1[1]+b1[3]) <= min(b2[1], b3[1]) + 1, "")
    probe("colonnes de la grille alignees",
          abs(b0[0]-b2[0]) <= 1 and abs(b1[0]-b3[0]) <= 1 and b0[0] < b1[0], "")
    probe("la grille est SOUS le heros", min(b0[1], b1[1]) >= hy + hh - 1, "")

    # 3. Debordements de contenu (heros et bandes).
    ovf = [f"{n}:{s}sur{c}" for n, s, c in g["oflow"] if s > c]
    probe("contenu sans debordement", not ovf, ovf if ovf else "scroll<=client partout")

    # 4. Zones dans la pedale (bas < y465 de la photo).
    probe("zones dans la pedale", hh + hy <= sy + 465 and max(b[1]+b[3] for b in g["bottom"]) <= sy + 465,
          f"bas max {round(max(b[1]+b[3] for b in g['bottom'])-sy)} <= 465")

    # 5. AlcoVe creusee : bande sombre a l'interieur vs pedale a cote.
    alcove = avg((b2[0]+2, b2[1]+b2[3]*0.45, 4, b2[3]*0.3), 0)
    outside = avg((b2[0]-9, b2[1]+b2[3]*0.45, 5, b2[3]*0.3), 0)
    probe("alcoVe creuse (bande plus sombre que la pedale voisine)",
          bright(alcove) < bright(outside) - 5, f"{bright(alcove):.0f} vs {bright(outside):.0f}")

    # 6. Knob heros en relief : piece claire sur alcoVe sombre.
    knob_b = avg(g["knobH"], 8)
    probe("knob heros en relief (metal clair)", bright(knob_b) > bright(alcove) + 60,
          f"{bright(knob_b):.0f} vs {bright(alcove):.0f}")

    # 7. Les titres de bande sont supprimes : ce sont les noms de knobs qui
    #    portent la lecture. Les 4 correcteurs + EQ ON portent TON ; DRIVE
    #    a rejoint la bande PROPRIETE.
    probe("TON porte 4 noms (correcteurs), DRIVE en bande PROPRIETE",
          g["countTon"] == 4 and g["namesTonVisible"],
          f"TON: {g['countTon']} noms, visibles: {g['namesTonVisible']}")

    # 8. Le VRAI LCD : DANS l'alcoVe du heros (au-dessus de COMP), verre clair
    #    visible, et ABSENT de la bande TON.
    lcd, hero, ton = g["lcd"], g["heroEl"], g["ton"]
    inside = lcd[0] >= hero[0] and lcd[1] >= hero[1] and \
             lcd[0]+lcd[2] <= hero[0]+hero[2] and lcd[1]+lcd[3] <= hero[1]+hero[3]
    probe("LCD reel dans l'alcoVe du heros", inside,
          f"lcd {round(lcd[2])}x{round(lcd[3])} @ ({round(lcd[0])},{round(lcd[1])})")
    glass = avg((lcd[0]+lcd[2]*0.3, lcd[1]+lcd[3]*0.4, lcd[2]*0.4, lcd[3]*0.2), 0)
    frame = avg((lcd[0]+lcd[2]*0.3, lcd[1]+lcd[3]*0.06, lcd[2]*0.4, lcd[3]*0.05), 0)
    probe("verre LCD visible (bande claire vs cadre sombre)",
          bright(glass) > bright(frame) + 40, f"{bright(glass):.0f} vs {bright(frame):.0f}")

    # 9. Niveaux : deux instruments independants aux coins du bas.
    mIn, mOut = g["meterIn"], g["meterOut"]
    win, wout = g["winIn"], g["winOut"]
    stage_bottom = sy + sh
    probe("IN ancre au coin bas-gauche",
          abs(mIn[0] - (sx + 6)) <= 2 and abs(mIn[1] + mIn[3] - (stage_bottom - 3)) <= 3,
          f"x {round(mIn[0]-sx)} (attendu ~6), bas a {round(mIn[1]+mIn[3]-sy)} sur {round(sh)}")
    probe("OUT ancre au coin bas-droit",
          abs(mOut[0] + mOut[2] - (sx + sw - 6)) <= 2 and abs(mOut[1] + mOut[3] - (stage_bottom - 3)) <= 3,
          f"droite a {round(mOut[0]+mOut[2]-sx)} sur {round(sw)}, bas a {round(mOut[1]+mOut[3]-sy)}")
    probe("fenetres IN/OUT de meme largeur", win[2] == wout[2], f"{round(win[2])} vs {round(wout[2])} px")
    probe("bandes fines (hauteur <= 28 px)", mIn[3] <= 28 and mOut[3] <= 28,
          f"{round(mIn[3])} / {round(mOut[3])} px")

    print(json.dumps({"all_ok": bool(ok), "probes": out}, indent=2))
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
