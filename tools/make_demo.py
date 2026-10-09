"""Genere une demo audible du plugin : une voix de synthese avec bruit de fond et
grondement, avant et apres OD Vox (gate + low cut + gains).

Seuls les modules F1.3 a F1.5 existent : la demo ne peut pas montrer l'EQ, le
compresseur ou la reverb, qui arrivent avec les lots suivants.

Usage :
    .venv/Scripts/python.exe tools/make_demo.py
"""
import sys
from pathlib import Path

import numpy as np
from pedalboard import load_plugin
from pedalboard.io import AudioFile

sys.path.insert(0, str(Path(__file__).parent))

from verify_plugin import read_catalogue, set_actual, set_choice  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = ROOT / "build" / "ODVox_artefacts" / "Release" / "VST3" / "OD Vox.vst3" / "Contents" / "x86_64-win" / "OD Vox.vst3"
OUT = ROOT / "demo"

SR = 48000


def vocal_like(seconds: float, start_phase: float = 0.0):
    """Une « voix » : fondamentale 165 Hz avec vibrato, huit harmoniques, enveloppe
    de phrase. Plus le grondement et le souffle qu'on veut entendre disparaitre."""
    n = int(seconds * SR)
    t = np.arange(n, dtype=np.float64) / SR

    # Enveloppe de phrase : attaque 20 ms, tenue, relachement 150 ms.
    attack = np.minimum(t / 0.020, 1.0)
    release = np.minimum((seconds - t) / 0.150, 1.0)
    envelope = np.clip(np.minimum(attack, release), 0.0, 1.0) ** 0.7

    # Vibrato de 5 Hz, profondeur 0,3 % : ce qui fait « chanté » plutôt que « sinus ».
    f0 = 165.0 * (1.0 + 0.003 * np.sin(2.0 * np.pi * 5.0 * t))
    phase = 2.0 * np.pi * np.cumsum(f0) / SR + start_phase

    mono = np.zeros(n, dtype=np.float64)
    for k in range(1, 9):
        mono += np.sin(k * phase) / (k ** 1.5)

    mono *= 0.28 * envelope

    return np.clip(mono, -1.0, 1.0).astype(np.float32)


def build_song():
    """Trois phrases separees par des silences.

    Le grondement et le souffle sont ajoutes sur TOUTE la duree, et non phrase
    par phrase : c'est ce qu'on a sur un vrai enregistrement, et c'est ce qui
    donne au gate quelque chose a couper entre les phrases. Une premiere version
    de cette demo ajoutait le souffle a l'interieur des phrases seulement, si
    bien que les silences etaient des zeros exacts et que le gate n'y montrait
    rien du tout."""
    phrases = [vocal_like(1.4), vocal_like(1.4, 1.1), vocal_like(1.6, 0.4)]
    gap = np.zeros(int(0.8 * SR), dtype=np.float32)

    mono = np.concatenate([phrases[0], gap, phrases[1], gap, phrases[2]])

    n = mono.shape[0]
    t = np.arange(n, dtype=np.float64) / SR

    # Grondement a 70 Hz, constant : ce que le low cut doit enlever.
    mono += 0.003 * np.sin(2.0 * np.pi * 70.0 * t)

    # Souffle a -60 dBFS, constant : ce que le gate doit couper entre les phrases.
    rng = np.random.default_rng(42)
    mono += 0.001 * rng.standard_normal(n)

    mono = np.clip(mono, -1.0, 1.0).astype(np.float32)
    return np.stack([mono, mono])


def write(path: Path, data: np.ndarray) -> None:
    path.parent.mkdir(exist_ok=True)
    with AudioFile(str(path), "w", samplerate=SR, num_channels=2) as f:
        f.write(data)
    print(f"  {path.relative_to(ROOT)}  ({data.shape[1] / SR:.1f} s)")


def main() -> None:
    catalogue = read_catalogue()
    plugin = load_plugin(str(PLUGIN))

    dry = build_song()

    write(OUT / "avant.wav", dry)

    # Réglages du « mix » : gate profond, low cut a 120 Hz / 24 dB par octave.
    # Le seuil a -30 dB laisse une vraie marge au-dessus du souffle (-53 dBFS) :
    # a -45 dB, les cretes du souffle flottaient autour du seuil et le gate
    # clignotait au lieu de fermer.
    reglages = {
        "input_gain_db": 0.0,
        "gate_amount": 100.0,
        # rev. suppression du mode Avance : seuil/release/range du gate sont
        # figures dans le module (-45 dB / 150 ms / 80 dB). Le seuil -30 dB
        # d'antan n'est plus reglable ; le range 80 dB est maintenu.
        "lowcut_amount": 1.0,
    }

    for param_id, value in reglages.items():
        set_actual(plugin, catalogue, param_id, value)

    wet = plugin.process(dry, SR, reset=True)
    write(OUT / "apres.wav", wet)

    print()
    print("reglages :", ", ".join(f"{k}={v}" for k, v in reglages.items()))
    print("pente low cut : 24 dB/oct (choix 1)")


if __name__ == "__main__":
    main()
