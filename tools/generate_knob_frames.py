# -*- coding: utf-8 -*-
"""Genere l'atlas de frames du knob OD Vox — ALUMINIUM BROSSE, PLEIN ROND.

Demande utilisateur du 2026-10-02 : « full rond mais avec des liserais tout
autour comme un bouton physique », en aluminium. Ce qui change par rapport au
look MXR « Cosmod III » blanc (qui reste dans l'historique de git) :

  - la SILHOUETTE est un PLEIN CERCLE : plus de lobes (l'ancienne silhouette
    etait lobee, les vallees flambaient au bord — c'etait le signe du relief
    qu'on ne veut plus) ;
  - le MOLETAGE est fin et fait le tour : 24 cannelures (au lieu de 8), sur
    toute la jupe, sans anneau net entre le dessus et la jupe ;
  - la MATIERE est de l'aluminium brosse : face grise, stries de tournage
    concentriques + brossage horizontal, gorge sombre au raccord du moletage,
    occlusion au bord externe ;
  - le moletage TOURNE avec la piece (principe inchangé depuis le sprite Figma) :
    chaque cannelure s'eclaire selon qu'elle fait face a la source FIXE A
    L'ECRAN, l'indice 3D que l'oeil attend ;
  - indicateur : LIGNE radiale gravee + POINT, sombres sur le metal clair,
    meme azimut, toujours visibles.

Le COMP est trace a 200 px, les knobs de grille a 44 : le moletage doit rester
lisible sur les deux, d'ou des cannelures fines et regulieres (24) plutot que
de larges crevures.

Rendu 100% logiciel (numpy), pas de Blender requis.
Sortie : assets/knob_atlas.png (grille 8 x N, 256 px/frame @2x = 128 @1x)
         + assets/knob_atlas_meta.h (constantes lues par Skin.h).

Convention d'angle : frame 0 = valeur 0 (ligne a 7 h 30), derniere frame =
valeur 1 (17 h). La ligne est a midi dans la frame du milieu (valeur 0.5).
"""

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# --- Constantes de rendu -----------------------------------------------------
WORK = 1024          # taille de rendu par frame (qualite), downscale ensuite
OUT_FRAME = 256      # taille de frame stockee (@2x ; le dessin @1x = 128 px)
COLS = 8             # colonnes de l'atlas
N_FRAMES = 61        # 61 frames sur 270 deg : pas de 4,5 deg, fluide au drag
SIZE_1X = 128        # taille de dessin declaree dans la meta (moitie de 256)

# 24 cannelures : le meme atlas sert le COMP (200 px) et les knobs de grille
# (44 px), ou 24 font 26 px de cannelure sur le COMP mais encore 5,7 px sur le
# petit — en dessous de ~2 px, le bicubique de l'hote (256 px -> 44 px, sans
# mipmap) scintille des que le knob tourne. 36 serait trop fin pour le petit.
FLUTES = 24                          # moletage fin, fait le tour
TOP_OUT = 0.84                       # rayon du dessus : la jupe = 16 % de R
LOBE = 0.0                           # PLEIN ROND : silhouette circulaire
LINE_IN, LINE_OUT = 0.52, 0.80       # ligne d'indicateur : plage radiale
LINE_HALF_W = 0.034                  # demi-largeur lineaire de la ligne (R)
DOT_C, DOT_R = 0.90, 0.042           # point d'indicateur, pres du bord
THETA_KEY = -3.0 * np.pi / 4.0       # azimut de la key (haut-gauche)
SHADOW_SIGMA = 0.05                  # largeur de l'ombre (fraction de R)
SHADOW_DY = 0.030                    # decalage vertical de l'ombre (fraction de R)
KNOB_IN_FRAME = 0.86                 # diametre du knob / taille de frame
                                     # (marge pour l'ombre, jamais rognee)

IND_DARK = np.array([0.05, 0.05, 0.06])  # gravure, sombre sur aluminium
TINT = np.array([0.99, 1.00, 1.02])     # aluminium legerement froid


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


_BRUSH_CACHE = {}


def brush_rows(size):
    """Brossage horizontal de l'aluminium : une realisation par ligne, lissee
        sur 11 px (la largeur du grain), normalisee. Cache par taille pour que
        les 61 frames d'un atlas aient EXACTEMENT le meme grain — sinon le
        metal grillerait d'une frame a l'autre quand on bouge le knob."""
    if size not in _BRUSH_CACHE:
        rng = np.random.default_rng(11)
        n = rng.normal(0.0, 1.0, size).astype(np.float32)
        n = np.convolve(n, np.ones(11, np.float32) / 11.0, mode="same")
        _BRUSH_CACHE[size] = n / (float(np.abs(n).max()) + 1e-6)
    return _BRUSH_CACHE[size]


def render_frame(angle_deg: float, size: int = WORK) -> np.ndarray:
    """Rend une frame : knob ECB071 vu de dessus, indicateur a `angle_deg`.

    angle_deg : 0 = ligne a midi ; positif = sens horaire.
    Lumiere FIXE a l'ecran (key haut-gauche). Les cannelures tournent avec
    la piece : leurs arcs brillants balaient la source fixe.
    Retourne un tableau (size, size, 4) float32 [0..1].
    """
    s = size
    c = (s - 1) / 2.0
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    dx, dy = xx - c, yy - c
    R = s * 0.5 * KNOB_IN_FRAME
    u, v = dx / R, dy / R
    r = np.hypot(u, v)                  # 1.0 = arete de cannelure
    th = np.arctan2(dx, -dy)            # 0 a midi, sens horaire

    a = angle_deg * np.pi / 180.0

    alpha = np.zeros((s, s), dtype=np.float32)

    # --- Ombre portee (FIXE : le sol la porte, pas la piece) ----------------
    rho_o = np.hypot(dx / R, (dy - SHADOW_DY * R) / R)
    shadow = np.exp(-((rho_o - 1.02) ** 2) / (2 * SHADOW_SIGMA ** 2))
    shadow *= (rho_o > 0.96).astype(np.float32)
    # fenetre : l'ombre meurt avant le bord de la frame (sinon coupure nette
    # visible quand les frames sont juxtaposees dans l'atlas).
    shadow *= 1.0 - smoothstep(1.08, 1.16, rho_o)
    alpha = shadow * 0.42

    # --- Lumiere fixe a l'ecran ---------------------------------------------
    key = np.clip(-(u * 0.55 + v * 0.85), -1.0, 1.0)          # -1..1
    brush = brush_rows(s)

    # --- Geometrie : silhouette PLEIN ROND + moletage fin --------------------
    # Les cannelures TOURNENT avec la piece (phase `a`) ; la silhouette ne suit
    # plus les cannelures (LOBE = 0) : le contour est un cercle PARFAIT, c'est
    # la demande « full rond ».
    fp = 0.5 + 0.5 * np.cos(FLUTES * (th - a))     # 1 crete, 0 creux
    r_out = 1.0

    # --- Dessus : aluminium brosse (0 -> TOP_OUT) ----------------------------
    m_top = r <= TOP_OUT
    broad = np.clip(key, 0.0, 1.0)
    top = 0.56 + 0.17 * broad ** 1.1
    # reflet large de la source (haut-gauche)
    top = top + 0.13 * np.exp(-(((u + 0.30) ** 2 + (v + 0.40) ** 2)) / (2 * 0.34 ** 2))
    # stries de TOURNAGE concentriques (l'aluminium se tourne au tour) + brossage
    top = top + 0.020 * np.sin(r * 190.0) * np.clip(1.15 - r, 0.0, 1.0)
    top = top + 0.030 * brush[:, None] * (0.35 + 0.65 * np.clip(1.1 - r, 0.0, 1.0))
    # le bord du dessus plonge dans le moletage
    top = top * (1.0 - 0.30 * smoothstep(TOP_OUT - 0.10, TOP_OUT, r))
    # lisere clair qui accroche la key sur l'arete du dessus
    facing = np.clip(np.cos(th - THETA_KEY), 0.0, 1.0)
    top = top + 0.18 * smoothstep(TOP_OUT - 0.05, TOP_OUT - 0.004, r) * facing
    # face ombree cote bas-droite (l'aluminium tombe au gris)
    top = top * (1.0 - 0.34 * smoothstep(0.40, TOP_OUT, r)
                       * np.clip(-key, 0.0, 1.0) ** 0.8)

    # --- Jupe moletee (TOP_OUT -> r_out) : 24 cannelures tout autour ---------
    m_ring = (r > TOP_OUT) & (r <= r_out)
    dphi = th - a
    # Azimut du CENTRE de la cannelure la plus proche : c'est lui qui
    # s'eclaire ou non selon la source fixe — d'ou les 24 stries radiales qui
    # balaient la piece quand elle tourne.
    step = 2.0 * np.pi / FLUTES
    mf = np.round(dphi / step)
    phi_f = a + mf * step
    facing_f = np.clip(np.cos(phi_f - THETA_KEY), 0.0, 1.0)
    ring = 0.40 + 0.46 * facing_f ** 1.2
    # le creux renvoie plus que la crete
    ring = ring + 0.07 * (1.0 - fp)
    ring = ring * (0.90 + 0.10 * fp)
    # gorge sombre au raccord du moletage (le dessus plonge dans la jupe)
    ring = ring * (1.0 - 0.45 * np.exp(-((r - TOP_OUT) / 0.030) ** 2))
    # occlusion douce au bord externe : le metal se detache du panneau
    ring = ring * (1.0 - 0.26 * smoothstep(0.975, 1.0, r))

    # --- Assemblage ----------------------------------------------------------
    rgb = np.zeros((s, s, 3), dtype=np.float32)
    rgb += (shadow * 0.45)[..., None] * np.array([0.9, 0.9, 0.95])

    body = r <= r_out
    w_top = m_top * smoothstep(0.000, 0.006, TOP_OUT - r + 0.006)
    w_ring = m_ring.astype(np.float32)
    w_ring = np.where(m_top, 0.0, w_ring)
    aa_edge = smoothstep(0.004, -0.004, r - r_out)          # 1 dedans, 0 dehors
    w_ring = w_ring * aa_edge
    w_top = np.clip(w_top, 0.0, 1.0)

    rgb = rgb * (1.0 - w_ring[..., None]) + (ring[..., None] * TINT) * w_ring[..., None]
    rgb = rgb * (1.0 - w_top[..., None]) + (top[..., None] * TINT) * w_top[..., None]
    alpha = np.where(body, 1.0, alpha)
    alpha = np.where(body, alpha * aa_edge + shadow * 0.42 * (1 - aa_edge), alpha)

    # --- Indicateur usine (tourne avec la piece) ------------------------------
    ang = (th - a + np.pi) % (2 * np.pi) - np.pi
    # LIGNE sombre radiale sur le dessus (toujours visible, vue de dessus)
    line_t = smoothstep(LINE_HALF_W, LINE_HALF_W * 0.45, np.abs(ang)) \
        * smoothstep(LINE_IN - 0.012, LINE_IN + 0.012, r) \
        * smoothstep(LINE_OUT + 0.012, LINE_OUT - 0.012, r) \
        * w_top
    rgb = rgb * (1.0 - line_t[..., None]) + (IND_DARK * line_t[..., None])
    # POINT sombre pres du bord du dessus, meme azimut (toujours visible)
    r_dot = np.hypot(u - DOT_C * np.sin(a), v + DOT_C * np.cos(a))
    dot_t = smoothstep(DOT_R, DOT_R * 0.55, r_dot) * w_top
    rgb = rgb * (1.0 - dot_t[..., None]) + (IND_DARK * dot_t[..., None])

    out = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)])
    return out


def compose_atlas():
    frames = []
    print(f"Rendu de {N_FRAMES} frames {WORK}x{WORK}...")
    for i in range(N_FRAMES):
        angle = -135.0 + 270.0 * i / (N_FRAMES - 1)
        f = render_frame(angle)
        img = Image.fromarray((f * 255).astype(np.uint8))
        img = img.resize((OUT_FRAME, OUT_FRAME), Image.LANCZOS)
        # Le flou leve l'energie haute frequence que le bicubique de l'hote
        # (frame 256 px -> ~56 px a l'ecran) transforme en moire.
        img = img.filter(ImageFilter.GaussianBlur(0.5))
        frames.append(img)
        if i % 10 == 0:
            print(f"  frame {i}: ligne a {angle:+.1f} deg")

    rows = (N_FRAMES + COLS - 1) // COLS
    atlas = Image.new("RGBA", (COLS * OUT_FRAME, rows * OUT_FRAME), (0, 0, 0, 0))
    for i, img in enumerate(frames):
        col, row = i % COLS, i // COLS
        atlas.paste(img, (col * OUT_FRAME, row * OUT_FRAME))

    atlas.save("assets/knob_atlas.png", optimize=True)
    print(f"OK : assets/knob_atlas.png {atlas.size[0]}x{atlas.size[1]} "
          f"({rows} rangees x {COLS} colonnes, frames {OUT_FRAME}px @2x)")

    # Meta generee : le C++ lit ces constantes, pas de desaccord possible.
    meta = (
        "// Genere par tools/generate_knob_frames.py — NE PAS EDITER.\n"
        "#pragma once\n\n"
        "namespace odvox::knobatlas {\n"
        f"    constexpr int kFrames    = {N_FRAMES};\n"
        f"    constexpr int kColumns   = {COLS};\n"
        f"    constexpr int kFrameSize = {OUT_FRAME};   // pixels @2x\n"
        f"    constexpr int kDrawSize  = {SIZE_1X};     // taille de dessin @1x\n"
        "}\n"
    )
    with open("assets/knob_atlas_meta.h", "w", encoding="utf-8") as fh:
        fh.write(meta)
    print("OK : assets/knob_atlas_meta.h")

    # Planche de controle : 12 frames clefs sur fond metal, pour l'oeil
    sheet = Image.new("RGBA", (6 * 140, 2 * 140), (176, 174, 170, 255))
    d = ImageDraw.Draw(sheet)
    for j, i in enumerate(np.linspace(0, N_FRAMES - 1, 12).astype(int)):
        x, y = (j % 6) * 140 + 6, (j // 6) * 140 + 6
        small = frames[i].resize((SIZE_1X, SIZE_1X), Image.LANCZOS)
        sheet.paste(small, (x, y), small)
        d.text((x + 2, y + SIZE_1X), f"#{i}", fill=(20, 20, 20, 255))
    sheet.save("docs/figma/knob_atlas_preview.png")
    print("OK : docs/figma/knob_atlas_preview.png (planche de controle)")


if __name__ == "__main__":
    compose_atlas()
