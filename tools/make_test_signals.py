"""Generate deterministic test signals used to characterise a vocal-chain plugin.

Each signal is designed to expose one thing:

  sweep.wav       log sweep 20 Hz -> 20 kHz   -> frequency response of every filter
  noise.wav       white noise                 -> transfer function (Welch), level
  impulse.wav     unit impulse                -> latency + impulse response
  levels.wav      1 kHz tone stepping in 3 dB -> compressor curve / threshold
  sines.wav       1 kHz then 8 kHz            -> harmonic distortion + aliasing
  sibilance.wav   vowel tone alternating with 7 kHz bursts -> de-esser behaviour
  stereo_id.wav   identical L and R noise     -> doubler / stereo width

Outputs are 48 kHz 32-bit float WAV, peak-normalised to -12 dBFS unless stated.
"""
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48000
PEAK_DBFS = -12.0
OUT = Path(__file__).resolve().parent.parent / "research" / "test_signals"
OUT.mkdir(parents=True, exist_ok=True)


def to_peak(x: np.ndarray, dbfs: float = PEAK_DBFS) -> np.ndarray:
    target = 10 ** (dbfs / 20)
    peak = np.max(np.abs(x))
    return (x * (target / peak)).astype(np.float32) if peak > 0 else x.astype(np.float32)


def write(name: str, data: np.ndarray, channels: int = 2) -> None:
    """data is mono 1-D; duplicate to `channels` identical channels."""
    if data.ndim == 1:
        data = np.tile(data, (channels, 1))
    sf.write(OUT / name, data.T, SR, subtype="FLOAT")
    print(f"  {name:<16} {data.shape[1] / SR:5.1f} s  {data.shape[0]} ch")


def log_sweep(seconds: float = 10.0, f1: float = 20.0, f2: float = 20000.0) -> np.ndarray:
    t = np.arange(int(SR * seconds)) / SR
    k = np.log(f2 / f1)
    phase = 2 * np.pi * f1 * seconds / k * (np.exp(t / seconds * k) - 1)
    x = np.sin(phase)
    # short fades so the sweep does not click at the edges
    fade = int(SR * 0.01)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return x


def main() -> None:
    rng = np.random.default_rng(20260918)
    print(f"writing test signals to {OUT}")

    write("sweep.wav", to_peak(log_sweep()))

    write("noise.wav", to_peak(rng.standard_normal(int(SR * 10.0))))

    imp = np.zeros(int(SR * 1.0))
    imp[int(SR * 0.1)] = 1.0
    write("impulse.wav", imp.astype(np.float32), channels=2)

    # stepped level tone: reveals compressor threshold and ratio
    steps = np.arange(-48, 0, 3, dtype=float)
    seg = int(SR * 1.0)
    levels = np.concatenate(
        [np.sin(2 * np.pi * 1000 * np.arange(seg) / SR) * (10 ** (db / 20)) for db in steps]
    )
    write("levels.wav", levels.astype(np.float32))
    (OUT / "levels_steps_db.txt").write_text(
        "\n".join(f"{i * seg}\t{db:.1f}" for i, db in enumerate(steps)), encoding="utf-8"
    )

    sine_seg = int(SR * 3.0)
    t = np.arange(sine_seg) / SR
    sines = np.concatenate(
        [np.sin(2 * np.pi * 1000 * t), np.sin(2 * np.pi * 8000 * t)]
    )
    write("sines.wav", to_peak(sines))

    # sibilance test: 0.4 s of vowel-ish tone, then 0.4 s of 7 kHz noise burst
    vowel_len, sib_len = int(SR * 0.4), int(SR * 0.4)
    tv = np.arange(vowel_len) / SR
    vowel = (
        np.sin(2 * np.pi * 220 * tv)
        + 0.5 * np.sin(2 * np.pi * 440 * tv)
        + 0.3 * np.sin(2 * np.pi * 880 * tv)
    )
    sib = rng.standard_normal(sib_len)
    sib = signal.sosfilt(signal.butter(4, [6000, 9000], "bandpass", fs=SR, output="sos"), sib)
    burst = np.concatenate([vowel, sib])
    write("sibilance.wav", to_peak(np.tile(burst, 8)))

    # identical L/R: any stereo widening must show up as a correlation change
    ident = to_peak(rng.standard_normal(int(SR * 5.0)))
    write("stereo_id.wav", np.tile(ident, (2, 1)), channels=2)

    print("\nall signals written")


if __name__ == "__main__":
    main()
