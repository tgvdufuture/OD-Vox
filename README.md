# OD Vox

All-in-one vocal chain — **VST3** plugin (Windows), **C++20 / JUCE 8**.

Load a factory preset, dial one knob per module, listen. The product exposes
**29 parameters, all visible**: no hidden setting, no advanced mode, one knob per
idea.

## The chain

In processing order:

| Stage | Parameter | Range |
|---|---|---|
| Input Gain | `input_gain_db` | −24 → +24 dB |
| Input calibration | `input_calibrate` | action (~5 s of listening) |
| Gate | `gate_amount` | 0 → 100 % |
| Low Cut | `lowcut_amount` | On / Off — 120 Hz, 24 dB/oct fixed |
| 4-band EQ | `eq_low_db`, `eq_mid_db`, `eq_hi_db`, `eq_air_db`, `eq_on` | ±15 dB |
| Comp | `comp_amount` | 0 → 100 % |
| De-ess | `deess_amount` | 0 → 100 % |
| Drive | `drive_amount`, `hq_mode` | 0 → 100 % — HQ 4× always active |
| Doubler / Width | `doubler_amount`, `width_amount` | 0 → 100 % / 0 → 200 % |
| Delay | `delay_amount`, `delay_time`, `delay_sync`, `delay_time_ms`, `delay_ducking` | 21 rhythmic divisions or 1–2000 ms |
| Reverb | `reverb_short_pct`, `reverb_small_pct`, `reverb_big_pct`, `reverb_lush_pct` | 0 → 100 % |
| Group bypass | `fx_on`, `delay_on`, `reverb_on` | band LED |
| Output | `output_gain_db`, `output_dc_filter` | ±24 dB — 10 Hz DC filter |

**20 factory presets** ship with the plugin, including 6 indexed by microphone
type (SM7b, SM58, condenser, NT1).

## Building

Requirements:

- **CMake ≥ 3.22**;
- **Visual Studio (MSVC)** — built here with toolset 14.29 (VS 2019 16.11);
- **JUCE 8** in `external/JUCE`. The folder is ignored by git: JUCE is not
  bundled with this repository.

```bash
git clone --branch 8.0.15 --depth 1 https://github.com/juce-framework/JUCE external/JUCE
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The bundle is produced at `build/ODVox_artefacts/Release/VST3/OD Vox.vst3`. The
build enables `COPY_PLUGIN_AFTER_BUILD`: it is also copied to the system VST3
folder (`C:\Program Files\Common Files\VST3\`).

## Verifying

```bash
cmake -B build -DODVOX_BUILD_TESTS=ON      # test targets: OFF by default
cmake --build build --config Release
ctest --test-dir build -C Release          # ODVoxTests + ODVoxEditorTests
```

The suites cover the logic (catalog, presets, DSP, bus contract) and the
interface (EQ curve drawing and gestures, offscreen, without a window).

The offline verification harness runs against the **installed** VST3 and covers
59 checks — transparency at default settings, reported latency, curves,
thresholds:

```bash
python tools/verify_plugin.py     # requires .venv: pedalboard, numpy, scipy
```

It writes `verification_report.md`.

Editor snapshots and stress (manual targets):

```bash
cmake --build build --config Release --target ODVoxSnapshot   # PNG capture
cmake --build build --config Release --target ODVoxStress     # editor create/destroy
```

## Documentation

- `PRD.md` — product specification: parameter catalog, interface contracts,
  acceptance criteria and the measurements that establish them.
- `docs/FIGMA_KIT.md` — redraw the interface assets in Figma.
- `tools/` — verification harness and asset generators.

## License

**AGPL-3.0** — see `LICENSE`.

The plugin embeds JUCE 8 and Steinberg's VST3 SDK, whose licenses are detailed in
`THIRD_PARTY.md`. In short: publishing OD Vox under AGPLv3 is possible;
distributing a **closed-source** version is not, without a commercial JUCE
license.

Released binaries (GitHub Releases) come with their corresponding source code,
which is this repository — that is what the license requires.

## Provenance

No line of code, no graphical asset, no preset and no impulse response in this
repository comes from another product. The design landmarks — EQ anchors at
120/700/1,750/10,000 Hz, the compressor curve, the delay's 21 rhythmic
divisions, the RT60 of the four reverb engines — are **values chosen for this
product**, documented as such in `PRD.md`.
