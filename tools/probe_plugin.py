"""Probe a VST3 plugin without a GUI: list its parameters and check whether it
actually passes audio.

Used to interrogate any VST3 offline (pedalboard hosts it headlessly, so
we can sweep controls and measure the result instead of guessing).

Usage:
    python tools/probe_plugin.py "C:/path/to/Plugin.vst3"
"""
import sys
from pathlib import Path

import numpy as np
from pedalboard import load_plugin


def main() -> None:
    if len (sys.argv) < 2:
        print ("usage: python tools/probe_plugin.py <chemin/du/Plugin.vst3>")
        raise SystemExit (2)

    target = Path (sys.argv[1])
    print(f"loading: {target}")
    print(f"exists : {target.exists()}")

    # pedalboard wants the bundle directory for a .vst3 bundle, but accepts the
    # inner binary too -- try the bundle first, then fall back.
    candidates = [str(target)]
    if target.is_dir():
        candidates += [str(p) for p in target.rglob("*.vst3") if p.is_file()]

    plugin = None
    for c in candidates:
        try:
            plugin = load_plugin(c)
            print(f"OK  loaded via: {c}")
            break
        except Exception as exc:  # noqa: BLE001
            print(f"ERR {c}: {type(exc).__name__}: {exc}")

    if plugin is None:
        print("could not load plugin at all")
        return

    params = plugin.parameters
    print(f"\n--- {len(params)} parameters ---")
    for i, (name, value) in enumerate(params.items()):
        print(f"{i:>3}  {name:<28} = {value}")

    # --- does it pass audio? -------------------------------------------------
    sr = 48000
    rng = np.random.default_rng(0)
    noise = (rng.standard_normal((2, sr)) * 0.05).astype(np.float32)
    try:
        out = plugin.process(noise, sr)
        print(f"\n--- audio test ---")
        print(f"in  rms L/R: {np.sqrt((noise**2).mean(axis=1))}")
        print(f"out rms L/R: {np.sqrt((out**2).mean(axis=1))}")
        print(f"out peak   : {np.abs(out).max():.6f}")
        print(f"out shape  : {out.shape}")
    except Exception as exc:  # noqa: BLE001
        print(f"\naudio test failed: {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
