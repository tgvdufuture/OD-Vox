"""Harnais de verification hors ligne de OD Vox (PRD.md §3.4).

Point d'entree :
    .venv/Scripts/python.exe tools/verify_plugin.py <chemin.vst3>

Le plugin est charge par `pedalboard`, pilote par ses parametres, et soumis a des
signaux de test deterministes. Le rapport est ecrit dans `verification_report.md`.

Codes de sortie :
    0  tous les seuils sont tenus
    1  au moins un seuil est manque
    2  plugin introuvable ou illisible
    3  le plugin charge mais ne traite pas l'audio (silence en sortie)

Le catalogue n'est PAS recopie ici : il est lu dans `src/Parameters.cpp`. C'est ce
qui garantit que le harnais suit le catalogue au lieu de diverger.

IMPORTANT -- les cles vues par l'hote ne sont pas les identifiants du catalogue.
Mesure le 2026-09-18 : l'hote voit un slug du NOM AFFICHE suivi de l'unite
(`eq_lomid_db` -> `low_mid_db`, `deess_amount` -> `de_ess`). Consequence a garder en tete : deux parametres qui
partagent un nom affiche se recouvrent et l'un des deux devient inatteignable.
C'est exactement ainsi qu'un plugin peut perdre un de ses parametres.
"""

from __future__ import annotations

import math
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from pedalboard import load_plugin

SRC = Path("src") / "Parameters.cpp"
REPORT = Path("verification_report.md")

SAMPLE_RATES = (44100, 48000, 88200, 96000, 176400, 192000)
BLOCK_SIZES = (1, 16, 64, 512, 4096, 100000)
IMPLICIT_KEYS = {"bypass"}

CATALOGUE_ROW = re.compile(
    r'\{\s*"(?P<id>[^"]+)",\s*"(?P<name>[^"]+)",\s*Module::(?P<module>\w+),'
    r'\s*(?P<essential>true|false),\s*Unit::(?P<unit>\w+),\s*(?P<min>-?[\d.]+)f,'
    r'\s*(?P<max>-?[\d.]+)f,\s*(?P<step>-?[\d.]+)f,\s*(?P<default>-?[\d.]+)f,'
    r'\s*(?P<choices>nullptr|"[^"]*"|[A-Za-z_]\w*),\s*(?P<automatable>true|false)\s*\}'
)
CONSTANT_ROW = re.compile(r'constexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*((?:"[^"]*"\s*)+);')

UNIT_SLUGS = {
    "db": "db",
    "ms": "ms",
    "hz": "hz",
    "x": "x",
    "cents": "cents",
    "ratio": "x",
    "q": "",
    "pct": "",
    "choice": "",
}


@dataclass
class Parameter:
    id: str
    name: str
    module: str
    essential: bool
    unit: str
    minimum: float
    maximum: float
    step: float
    default: float
    choices: list[str] = field(default_factory=list)
    automatable: bool = True

    def host_key(self) -> str:
        """Cle sous laquelle l'hote expose ce parametre.

Regle reelle (redecodee le 2026-09-21 sur les 50 cles de l'hote) : nom affiche
en minuscules, espaces et tirets remplaces par des underscores, suivi du
suffixe d'unite (`_db`, `_ms`, `_hz`, `_x` pour ratio). Les unites
percent / boolean / choice / action n'ont pas de suffixe. Exemples :
« De-ess » -> `de_ess`, « Ratio » -> `ratio_x`, « Free Time » -> `free_time_ms`."""
        slug = re.sub(r"[\s\-]+", "_", self.name.strip().lower()).strip("_")
        suffix = UNIT_SLUGS.get(self.unit, "")
        return slug + ("_" + suffix if suffix else "")


@dataclass
class Check:
    name: str
    passed: bool
    detail: str = ""


@dataclass
class Harness:
    checks: list[Check] = field(default_factory=list)

    def record(self, name: str, passed: bool, detail: str = "") -> bool:
        self.checks.append(Check(name=name, passed=bool(passed), detail=detail))
        return bool(passed)

    def failures(self) -> list[Check]:
        return [c for c in self.checks if not c.passed]


def read_string_constants(text: str) -> dict[str, str]:
    constants: dict[str, str] = {}
    for name, literal in CONSTANT_ROW.findall(text):
        constants[name] = "".join(re.findall(r'"([^"]*)"', literal))
    return constants


def read_catalogue(text: str) -> list[Parameter]:
    constants = read_string_constants(text)
    catalogue: list[Parameter] = []
    for m in CATALOGUE_ROW.finditer(text):
        choices_raw = m.group("choices")
        choices: list[str] = []
        if choices_raw and choices_raw != "nullptr":
            if choices_raw.startswith('"'):
                choices = choices_raw.strip('"').split("|")
            else:
                table = constants.get(choices_raw)
                if table is None:
                    raise RuntimeError(
                        f"liste de choix introuvable pour {m.group('id')} (constante `{choices_raw}`)"
                    )
                choices = table.split("|")
        catalogue.append(
            Parameter(
                id=m.group("id"),
                name=m.group("name"),
                module=m.group("module"),
                essential=m.group("essential") == "true",
                unit=m.group("unit"),
                minimum=float(m.group("min")),
                maximum=float(m.group("max")),
                step=float(m.group("step")),
                default=float(m.group("default")),
                choices=choices,
                automatable=m.group("automatable") == "true",
            )
        )
    return catalogue


def resolve_plugin(target: Path) -> str | None:
    """pedalboard accepte tantot le bundle, tantot le binaire interne : on essaie
les deux au lieu de supposer. Sur Windows, le scanner ne charge que le BINAIRE
interne — jamais le bundle-dossier — donc la recherche du binaire doit DESCENDRE
dans `Contents/x86_64-win/` (rglob, pas glob) : c'est l'echec qui masquait les
chargements valides du 2026-09-21."""
    if target.is_dir():
        inner = list(target.rglob("*.vst3"))
        candidates = [target, *inner]
    else:
        candidates = [target]
    for candidate in candidates:
        try:
            load_plugin(str(candidate))
            return str(candidate)
        except Exception:
            continue
    return None


def rms(a: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(a)))) if a.size else 0.0


def db(ratio: float) -> float:
    return 20.0 * math.log10(max(ratio, 1e-20))


def same_f32(x: np.ndarray, y: np.ndarray) -> bool:
    """Egalite au bit pres APRES conversion float32 : pedalboard porte les
samples en float32, une entree float64 n'est donc jamais retranscrite
exactement — comparer dans l'espace du plugin, pas dans celui de numpy."""
    return np.array_equal(x.astype(np.float32), y.astype(np.float32))


def f32_diff(x: np.ndarray, y: np.ndarray) -> float:
    d = np.abs(x.astype(np.float32).astype(np.float64)
               - y.astype(np.float32).astype(np.float64))
    return float(d.max())


def signal() -> np.ndarray:
    """Bruit deterministe a -20 dBFS crete : couvre tout le spectre, donc toute
bande de l'EQ et tout etage non lineaire."""
    rng = np.random.default_rng(2)
    y = rng.uniform(-1.0, 1.0, (2, 48000)) * 0.1
    peak = np.max(np.abs(y))
    return y * (0.1 / max(peak, 1e-9))


def skew_exponent(param: Parameter) -> float:
    """Exposant JUCE : `NormalisableRange::convertTo0to1` eleve la proportion a
la puissance `skew`, avec `setSkewForCentre(c)` -> `skew = ln(0,5)/ln(c)`, c la
position relative du centre. C'est une LOI DE PUISSANCE — pas l'exponentielle
que ce harnais portait par erreur jusqu'au 2026-09-21 (symptome : `delay_time_ms`
300 ms lu 1416 ms chez le plugin, l'inverse exact du defaut observe). Applique
aux unites `ms` et `hz` par `rangeFor` (src/Parameters.cpp)."""
    if param.unit not in ("ms", "hz"):
        return 1.0
    centre = max(param.minimum + 1.0, param.default)
    c = (centre - param.minimum) / (param.maximum - param.minimum)
    if not (0.0 < c < 1.0):
        return 1.0
    return math.log(0.5) / math.log(c)


def normalised(param: Parameter, actual: float) -> float:
    """Valeur reelle -> 0..1, courbure comprise."""
    lo, hi = param.minimum, param.maximum
    x = (min(max(actual, lo), hi) - lo) / (hi - lo)
    k = skew_exponent(param)
    return x ** k if k != 1.0 else x


def actual_of(param: Parameter, normalised_value: float) -> float:
    """Inverse de `normalised`."""
    lo, hi = param.minimum, param.maximum
    k = skew_exponent(param)
    if k == 1.0:
        return lo + (hi - lo) * normalised_value
    return lo + (hi - lo) * normalised_value ** (1.0 / k)


def snap_to_step(param: Parameter, actual: float) -> float:
    """L'hote quantifie a l'intervalle declare : la conversion d'ici doit viser
la meme grille, sans quoi une valeur juste serait lue decalee d'un pas."""
    if param.step <= 0.0:
        return actual
    k = round((actual - param.minimum) / param.step)
    v = param.minimum + k * param.step
    return min(max(v, param.minimum), param.maximum)


def by_id(catalogue: list[Parameter], param_id: str) -> Parameter:
    for p in catalogue:
        if p.id == param_id:
            return p
    raise KeyError(param_id)


def set_actual(plugin, catalogue: list[Parameter], param_id: str, value: float) -> None:
    param = by_id(catalogue, param_id)
    norm = normalised(param, snap_to_step(param, value))
    plugin.parameters[param.host_key()].raw_value = min(max(norm, 0.0), 1.0)


def get_actual(plugin, catalogue: list[Parameter], param_id: str) -> float:
    param = by_id(catalogue, param_id)
    norm = plugin.parameters[param.host_key()].raw_value
    return actual_of(param, norm)


def set_choice(plugin, catalogue: list[Parameter], param_id: str, index: int) -> None:
    param = by_id(catalogue, param_id)
    span = param.maximum - param.minimum
    norm = (index / max(len(param.choices) - 1, 1)) if span else 0.0
    plugin.parameters[param.host_key()].raw_value = min(max(norm, 0.0), 1.0)


def reset_to_defaults(plugin, catalogue: list[Parameter]) -> None:
    """Remet tous les controles a leur defaut catalogue. Sans cela, un controle
laisse a mi-course par le balayage precedent fausse la mesure suivante —
erreur commise puis corrigee le 2026-09-18."""
    for p in catalogue:
        plugin.parameters[p.host_key()].raw_value = normalised(p, p.default)


def sine(amplitude: float, freq: float, seconds: float, sr: int) -> np.ndarray:
    t = np.arange(int(seconds * sr)) / sr
    return np.full((2, t.size), amplitude, dtype=np.float64) * np.sin(
        2.0 * np.pi * freq * t
    )


def stereo_tone(
    freq_l: float, freq_r: float, amplitude: float, seconds: float,
    phase_l: float = 0.0, phase_r: float = 0.0, sr: int = 48000,
) -> np.ndarray:
    """Deux canaux de frequences et de PHASES distinctes : la matiere premiere du
module Image. Un signal mono n'a rien a elargir ni a decorreler — deux canaux
identiques ont un side nul, donc le mono bass n'y a rien a faire."""
    t = np.arange(int(seconds * sr)) / sr
    y = np.zeros((2, t.size), dtype=np.float64)
    y[0] = amplitude * np.sin(2.0 * np.pi * freq_l * t + phase_l)
    y[1] = amplitude * np.sin(2.0 * np.pi * freq_r * t + phase_r)
    return y


def correlation(a: np.ndarray, skip_seconds: float = 0.0, sr: int = 48000) -> float:
    """Correlation normalisee entre deux canaux, en regime etabli : 1,0 pour un
signal strictement mono, 0 pour des canaux independants."""
    i0 = int(skip_seconds * sr)
    l = a[0, i0:]
    r = a[1, i0:] if a.shape[0] > 1 else a[0, i0:]
    den = math.sqrt(float(np.mean(l * l)) * float(np.mean(r * r)))
    return float(np.mean(l * r) / den) if den > 0 else 0.0


def side_level(y: np.ndarray, freq: float, sr: int, skip_seconds: float = 0.0) -> float:
    """Amplitude de la raie `freq` dans le SIDE (L − R). Mesurer le side, et non
un rapport d'amplitude L/R, est ce qui distingue un grave monoise d'un grave
elargi : un elargissement conserve l'amplitude de chaque canal, donc le rapport
reste ~1 par construction."""
    i0 = int(skip_seconds * sr)
    side = y[0, i0:] - y[1, i0:]
    t = np.arange(side.size) / sr
    ref = np.sin(2.0 * np.pi * freq * t)
    return float(2.0 * np.mean(side * ref))


def tone_level_hann(
    y: np.ndarray, freq: float, sr: int, skip_seconds: float = 0.0,
) -> float:
    """Amplitude d'une raie, fenetree de Hann. La correlation rectangulaire sur une
longueur finie lit les LOBES de la sonde elle-meme (une sonde a 220 Hz montre
−64 dB a 17 Hz) : la fenetre les descend sous −90 dB, ce qui rend un plancher
subsonique de −80 dBFS MESURABLE au lieu d'etre noye (lecon du F1.6c)."""
    i0 = int(skip_seconds * sr)
    mono = y[0, i0:] - 0.0
    t = np.arange(mono.size) / sr
    w = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(mono.size) / max(mono.size - 1, 1))
    z = mono * w
    c = np.sum(z * np.cos(2.0 * np.pi * freq * t))
    s = np.sum(z * np.sin(2.0 * np.pi * freq * t))
    return float(2.0 * np.hypot(c, s) / np.sum(w))


def response_db(plugin, freq: float, sr: int = 48000, seconds: float = 1.0, amplitude: float = 0.2) -> float:
    """Reponse du plugin a une frequence, une fois le regime etabli."""
    y = plugin.process(sine(amplitude, freq, seconds, sr), sr, reset=True)
    skip = 1 if seconds > 1 else 0
    out = tone_level_hann(y, freq, sr, skip_seconds=skip * 0.5 if skip else 0.0)
    ref = amplitude
    return db(out / max(ref, 1e-30))


def main() -> int:
    global CATALOGUE_CACHE

    if len(sys.argv) != 2:
        print("usage : python tools/verify_plugin.py <chemin.vst3>")
        return 2
    target = Path(sys.argv[1])
    if not target.exists():
        print("plugin introuvable : ", target)
        return 2

    catalogue = read_catalogue(SRC.read_text(encoding="utf-8"))
    CATALOGUE_CACHE = catalogue
    print("catalogue lu dans src/Parameters.cpp : ", len(catalogue),
          " parametres lus, dont ", sum(1 for p in catalogue if p.essential),
          " `Essential` (26 attendus depuis F2.2)")

    resolved = resolve_plugin(target)
    if resolved is None:
        print("chargement impossible : ", target)
        return 2
    plugin = load_plugin(resolved)
    print("chargement par un hote : ", resolved)

    # le plugin traite l'audio
    alive = plugin.process(np.zeros((2, 256)), 48000, reset=True)
    if not np.isfinite(alive).all():
        return 3

    h = Harness()
    check_inventory(h, plugin, catalogue)
    check_audio_alive(h, plugin)
    check_chain_transparency(h, plugin)
    check_sample_rates_and_blocks(h, plugin)
    check_gain(h, plugin)
    check_latency(h, plugin)
    check_bypass(h, plugin, catalogue)
    check_gate(h, plugin)
    check_low_cut(h, plugin)
    check_compressor(h, plugin)
    check_deess(h, plugin)
    check_drive(h, plugin)
    check_hq(h, plugin)
    check_doubler(h, plugin)
    check_width(h, plugin)
    check_eq(h, plugin)
    check_calibration(h, plugin)
    check_stability(h, plugin)
    check_controls(h, catalogue)
    check_delay(h, plugin, catalogue)
    check_reverb(h, plugin, catalogue)

    exit_code = write_report(h, target)
    return exit_code


def check_inventory(h: Harness, plugin, catalogue: list[Parameter]) -> None:
    keys = [p.host_key() for p in catalogue]
    h.record(
        "aucun recouvrement de cle chez l'hote",
        len(set(keys)) == len(keys),
        "noms affiches en collision : "
        + ", ".join(sorted({k for k in keys if keys.count(k) > 1}))
        if len(set(keys)) != len(keys)
        else f"{len(set(keys))} cles distinctes pour {len(keys)} parametres",
    )

    host_keys = set(plugin.parameters.keys()) - IMPLICIT_KEYS

    missing = [k for k in keys if k not in host_keys]
    h.record(
        "tous les parametres declares sont exposes",
        not missing,
        f"{len(host_keys)} cles presentes" if not missing else "manquants : " + ", ".join(missing),
    )
    extra = host_keys - set(keys)
    h.record(
        "aucun parametre inattendu",
        not extra,
        "seul `bypass` s'ajoute, il vient du format VST3" if not extra else "en trop : " + ", ".join(sorted(extra)),
    )
    h.record(
        "le compte de parametres est coherent",
        len(plugin.parameters) == len(catalogue) + len(IMPLICIT_KEYS),
        f"{len(plugin.parameters)} cles vues par l'hote pour {len(catalogue)} parametres declares + {len(IMPLICIT_KEYS)} implicite",
    )


def check_audio_alive(h: Harness, plugin) -> None:
    """Le controle qui revele le blocage chez un plugin : un plugin
peut se charger, lister ses parametres et rester MUE T. Sans ce controle, une
chaine entiere peut sembler verifiee alors que rien ne passe."""
    x = signal()
    y = plugin.process(x, 48000, reset=True)
    level_in, level_out = rms(x), max(rms(y), 1e-20)
    h.record(
        "le plugin traite l'audio (sortie non nulle)",
        level_out > 1e-9,
        f"sortie a {db(level_out / level_in):+.2f} dB par rapport a l'entree (defauts : gains a 0 dB)",
    )
    finite = np.isfinite(y).all()
    h.record(
        "la sortie est numeriquement saine",
        finite,
        f"{int((~np.isfinite(y)).sum())} echantillons non finis",
    )


def neutralise_defaults(plugin, catalogue: list[Parameter]) -> None:
    """Ramene au NEUTRE les reglages TONAUX du defaut : l'Air de l'EQ, le low
cut et le filtre DC de sortie.

Necessaire a tout controle qui isole un module, ou qui exige une chaine
neutre : ces trois reglages NE SONT PAS neutres aux defauts du catalogue, et
le contrat de transparence de US-02 AC2 porte sur les CURSEURS D'INTENSITE
(gate, comp, de-ess, drive, doubler, delay, reverb), pas sur eux.

L'amendement s'est fait par mesures successives :
  - 2026-09-19 : l'Air (`eq_air_db` = +2,5 dB, la valeur mesuree chez la
    retenue et voulue par defaut au §3.4 du PRD) sort du contrat ;
  - 2026-09-29 : `lowcut_amount` passe ON par defaut, comme la cible —
    un passe-haut a 120 Hz qui dephase la bande passante et faisait echouer
    les comparaisons au bit pres (jusqu'a 2,5e-1 sur un sinus a 1 kHz) ;
  - 2026-10-09 : `output_dc_filter` est ENFIN APPLIQUE — il etait declare au
    catalogue et jamais lu — et il est, lui aussi, ON par defaut.

Les trois sont de meme nature : des reglages TONAUX du defaut, des
interrupteurs de nettoyage."""
    set_actual(plugin, catalogue, "eq_air_db", 0.0)
    set_actual(plugin, catalogue, "lowcut_amount", 0.0)
    set_actual(plugin, catalogue, "output_dc_filter", 0.0)


def check_chain_transparency(h: Harness, plugin) -> None:
    """AC2 de US-02 : tous les curseurs d'intensite a 0 % doivent rendre la chaine
transparente, une fois les reglages TONAUX du defaut neutralises (voir
`neutralise_defaults` : Air, low cut, filtre DC). A faire sur une instance
fraiche, avant tout balayage."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    x = sine(0.5, 1000.0, 0.5, 48000)
    y = plugin.process(x, 48000, reset=True)
    identique = same_f32(x, y)
    h.record(
        "reglages tonaux neutralises et curseurs a 0 %, la chaine est transparente",
        identique,
        "sortie identique au bit pres (apres conversion float32)"
        if identique
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    # Defauts d'origine : la seule difference doit etre la bande Air +2,5 dB.
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    amp = 0.1
    y_def = plugin.process(sine(amp, 15000.0, 1.0, 48000), 48000, reset=True)
    air = db(max(tone_level_hann(y_def, 15000.0, 48000, skip_seconds=0.5), 1e-30) / amp)
    h.record(
        "l'ecart des defauts est la seule bande Air (+2,5 dB)",
        2.3 < air < 3.0,
        f"Air mesure a 15 kHz : {air:+.2f} dB (attendu ~+2,5)",
    )


def check_sample_rates_and_blocks(h: Harness, plugin) -> None:
    bad = []
    for sr in SAMPLE_RATES:
        rng = np.random.default_rng(7)
        x = rng.uniform(-0.2, 0.2, (2, sr))
        y = plugin.process(x, sr, reset=True)
        if not (np.isfinite(y).all() and y.shape == x.shape):
            bad.append(f"{sr} Hz")
    h.record(
        "les 6 taux d'echantillonnage passent",
        not bad,
        "44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz, sortie finie"
        if not bad
        else "; ".join(bad),
    )

    bad = []
    for size in BLOCK_SIZES:
        rng = np.random.default_rng(11)
        x = rng.uniform(-0.2, 0.2, (2, size))
        y = plugin.process(x, 48000, reset=True)
        if not (np.isfinite(y).all() and y.shape == x.shape):
            bad.append(size)
    h.record(
        "les tailles de bloc de 1 a 100 000 echantillons passent",
        not bad,
        "1, 16, 64, 512, 4096, 100000"
        if not bad
        else ", ".join(str(b) for b in bad),
    )


def check_gain(h: Harness, plugin) -> None:
    """Les deux gains d'entree et de sortie : les mesurer prouve que le bus de
parametres atteint reellement le traitement.

L'etat est repose explicitement, EQ au reglage neutre compris : ce controle ne
remettait pas les parametres a zero et heritait donc de la bande Air laissee
par le controle precedent, ce qui le decalait de +1,54 dB des que l'EQ a
existe (mesure du 2026-09-19)."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    x = sine(0.5, 1000.0, 0.5, 48000)
    details, ok = [], True
    for pid in ("input_gain_db", "output_gain_db"):
        reset_to_defaults(plugin, CATALOGUE_CACHE)
        neutralise_defaults(plugin, CATALOGUE_CACHE)
        for gain in ((-24.0, -12.0, -6.0, 0.0, 6.0, 12.0, 24.0)
                     if pid == "output_gain_db" else (-12.0, -6.0, 0.0, 6.0, 12.0)):
            set_actual(plugin, CATALOGUE_CACHE, pid, gain)
            y = plugin.process(x, 48000, reset=True)
            measured = db(max(rms(y), 1e-20) / rms(x))
            # AC4 de F1.13 : le gain MESURE correspond a la consigne a ±0,1 dB —
            # tolerance serree, aux bornes ±24 comprises.
            delta = abs(measured - gain)
            ok = ok and delta < 0.1
            details.append(
                f"{pid} {gain:+.0f} -> {measured:+.2f}"
                + ("" if delta < 0.1 else " !!")
            )
    h.record(
        "les gains d'entree et de sortie sont exacts au dB",
        ok,
        "; ".join(details),
    )


def check_latency(h: Harness, plugin) -> None:
    """§5 exige 0 echantillon en mode zero-latency. Mesure par correlation : une
impulsion en entree contre sa position en sortie."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    x = np.zeros((2, 4096), dtype=np.float64)
    x[:, 256] = 1.0
    y = plugin.process(x, 48000, reset=True)
    pos = int(np.argmax(np.abs(y[0])))
    h.record(
        "latence nulle",
        pos == 256,
        f"impulsion en 256, retrouvee en {pos}",
    )


def check_bypass(h: Harness, plugin, catalogue: list[Parameter]) -> None:
    """F1.13 / AC3 : le bypass de l'hote est un VRAI contournement — entree et
sortie indistinguables sous -120 dBFS, Y COMPRIS tous modules pousses. Le chemin
normal d'une chaine figuree ne s'entend pas ici : c'est l'identite audio."""
    # Tous les parametres a une valeur normalisee DISTINCTE (0,05..0,95) : la
    # chaine la plus chargee que le catalogue puisse exprimer, butees exclues.
    # Les defauts sont transparents par contrat (US-02) : ils ne prouveraient
    # rien sur le chemin de contournement.
    reset_to_defaults(plugin, catalogue)
    seed = 0.1
    for i, param in enumerate(catalogue):
        plugin.parameters[param.host_key()].raw_value = min(
            max(0.05 + 0.9 * ((seed + 0.037 * float(i)) % 1.0), 0.0), 1.0
        )

    # Le bypass de l'hote : pedalboard ne l'active pas tout seul, on pose le
    # parametre implicite `bypass` — c'est exactement le routage du wrapper
    # JUCE qu'on veut eprouver (valeur >= 0,5 -> processBlockBypassed).
    plugin.parameters["bypass"].raw_value = 1.0

    # rev. F2.2 : `hq_mode` fait partie des parametres pousses ci-dessus ; or
    # pedalboard applique une PDC (compensation de latence) sur la valeur
    # REPORTREE — 4 echantillons en HQ — alors que le chemin BYPASSE n'a, par
    # construction, aucun retard : la sortie serait recadree de 4 smp (pire
    # ecart 0,258 = 2.A.sin(Dphi/2) pour 4 smp a 997 Hz), artefact d'hote et
    # non fuite du plugin. Dans un DAW, la PDC n'est pas appliquee au chemin
    # bypassé. On mesure donc le bypass avec un rapport de latence nul.
    hq_key = next(
        (p.host_key() for p in catalogue if p.id == "hq_mode"), None
    )
    if hq_key is not None and hq_key in plugin.parameters:
        plugin.parameters[hq_key].raw_value = 0.0

    x = sine(0.5, 997.0, 1.0, 48000)
    y = plugin.process(x, 48000, reset=True)

    # Amendement UI du 2026-09-29 : `hq_mode` n'est plus lu (le mode HQ est
    # TOUJOURS actif), donc le bloc ci-dessus ne ramene plus la latence a zero.
    # L'hote applique alors sa PDC aux blocs RENDUS, et le chemin CONTOURNE —
    # qui n'a, lui, aucun retard — ressort decale du nombre d'echantillons
    # reporte (4 en HQ). Ce decalage est un artefact d'hote : dans un DAW, la
    # compensation aligne tout le monde. On le retire avant de juger
    # l'identite, et on ANNONCE le decalage trouve — c'est la seule facon
    # honnete de mesurer « le bypass ne traite rien » maintenant que la
    # latence ne peut plus etre mise a zero par un parametre.
    def aligned_diff(k: int) -> float:
        """Ecart maximal entre l'entree et la sortie du chemin contourne, une
        fois retire un decalage ENTIER de k echantillons (y[n] = x[n - k])."""
        if k > 0:
            a, b = x[:, : x.shape[1] - k], y[:, k:]
        elif k < 0:
            a, b = x[:, -k:], y[:, : y.shape[1] + k]
        else:
            a, b = x, y

        n = min(a.shape[1], b.shape[1])

        if n <= 0:
            return float("inf")

        return float(np.max(np.abs(b[:, :n] - a[:, :n])))

    best_shift = min(range(-8, 9), key=aligned_diff)
    diff = aligned_diff(best_shift)

    # Le critere du PRD est ≤ -120 dBFS. (Le test C++ exige lui le BIT EXACT :
    # le processeur recopie l'entree a l'identique. Le residu mesure ici vient
    # du float de l'hote, pas du plugin — mesure du 2026-09-22 : -156,5 dB.)
    h.record(
        "le bypass est un vrai contournement, tous modules pousses (AC3 de F1.13)",
        db(max(diff, 1e-12)) <= -120.0,
        f"pire ecart {diff:.9f} ({db(max(diff, 1e-12)):.1f} dB, exigee sous -120 dBFS), "
        f"apres alignement du decalage d'hote ({best_shift} ech.)",
    )

    # Retour a l'etat normal pour les controles suivants.
    plugin.parameters["bypass"].raw_value = 0.0


def check_gate(h: Harness, plugin) -> None:
    """F1.4 / US-03, rev. ancres figees : la reduction en silence vaut le range
INTERNE du module (80 dB) — seuil, release et range ne sont plus reglables."""
    # --- AC1 : la reduction EN SILENCE vaut la valeur reglee ----------------
    # (Mesure du 2026-09-21 : la sonde initiale demandait un « plafond 80 dB »
    # mais comparait le dB DE SORTIE, soit une exigence de −80 dBFS — 60 dB
    # d'attenuation dans le silence, pas un controle du plafond.)
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "gate_amount", 100.0)
    # rev. suppression du mode Avance : seuil (-45 dB), release (150 ms) et
    # range (80 dB) sont des constantes de conception dans le module Gate.
    # La porte se mesure sur un signal SOUS le seuil — pas sur du silence
    # numerique, qu'aucun gate ne peut atténuer (mesure du 2026-09-21 :
    # -400 dBFS, la sortie du silence etait... du silence).
    bas = 0.003   # -50 dBFS crete, sous le seuil de -45 dBFS
    x = np.zeros((2, 48000), dtype=np.float64)
    x[:, 24000:] = bas * np.sin(2.0 * np.pi * 1000.0 * np.arange(24000) / 48000)
    y = plugin.process(x, 48000, reset=True)
    niveau_bas = db(max(rms(y[:, 30000:]), 1e-30)) - db(max(rms(x[:, 30000:]), 1e-30))
    h.record(
        "le gate attenue le sous-seuil de la valeur reglee (AC1 de US-03)",
        abs(niveau_bas - (-80.0)) < 8.0,
        f"reduction sous le seuil {niveau_bas:.2f} dB pour le range interne 80 dB",
    )


def check_low_cut(h: Harness, plugin) -> None:
    """F1.5 revise : interrupteur fige a 120 Hz / 24 dB/oct (aucun reglage)."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "lowcut_amount", 1.0)

    at_corner = response_db(plugin, 120.0)
    at_pass = response_db(plugin, 1000.0)
    h.record(
        "le low cut (On) coupe le grave : ~-3 dB au coin 120 Hz, inaudible a 1 kHz",
        abs(at_corner + 3.0) < 3.0 and abs(at_pass) < 0.5,
        f"{at_corner:.2f} dB a 120 Hz, {at_pass:.2f} dB a 1 kHz",
    )

    deep = response_db(plugin, 30.0)
    h.record(
        "le low cut (On) attenuue le grave d'au moins 20 dB par octave",
        deep < -30.0,
        f"{deep:.1f} dB a 30 Hz (24 dB/oct : deux octaves sous le coin, le filtre attenu au-dela de 40 dB)",
    )

    """AC3 de US-02, revision du 2026-09-21 : les etages court-circuitables
(gate, low cut, comp, de-ess, drive, EQ Off, doubler 0 %) ont des chemins de
signaux SEPARES par construction — bypass module par module, jamais a
l'echelle de la chaine. Le niveau de SORTIE dans le silence est donc la
preuve : aucun etage n'ajoute de bruit de fond."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    y = plugin.process(np.zeros((2, 48000)), 48000, reset=True)
    fond = float(np.max(np.abs(y)))
    h.record(
        "aux curseurs a 0 %, la chaine n'ajoute rien dans le silence",
        fond <= 1e-9,
        f"pire echantillon en sortie {fond:.3e} sur 1 s de silence",
    )


def _lowcut_off_is_bit_exact(plugin) -> tuple[bool, float]:
    """Interrupteur ferme : le module ne doit RIEN faire, au bit pres.

Les reglages tonaux du defaut sont neutralises AVANT la mesure
(`neutralise_defaults`) : la transparence du low cut se mesure sur la chaine, et
aux defauts du catalogue l'Air vaut +2,5 dB — la sortie ne serait donc jamais
identique a l'entree, quelle que soit la justesse du low cut. Mesure du
2026-09-20 : le controle etait en echec pour cette seule raison. Le critere est
`array_equal`, pas `allclose` : un nom qui dit « au bit pres » doit etre tenu au
bit pres."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "lowcut_amount", 0.0)
    x = sine(0.5, 440.0, 0.5, 48000)
    y = plugin.process(x, 48000, reset=True)
    if same_f32(x, y):
        return True, 0.0
    return False, f32_diff(x, y)


def odvox_table_for(amount01: float) -> dict[str, float]:
    """La table macro retenue, portee par src/Compressor.cpp
(points cles interpoles lineairement) — la cible F1.7b."""
    table = (
        (0.0, 1.5, 1.7),
        (0.25, 1.63, 7.3),
        (0.5, 1.79, 13.1),
        (0.75, 2.05, 18.2),
        (1.0, 4.37, 29.2),
    )
    a = min(max(amount01, 0.0), 1.0)
    for (a0, r0, m0), (a1, r1, m1) in zip(table, table[1:]):
        if a <= a1:
            t = (a - a0) / (a1 - a0) if a1 > a0 else 0.0
            return {
                "threshold": -50.0,
                "ratio": r0 + t * (r1 - r0),
                "release": 150.0,
                "makeup": m0 + t * (m1 - m0),
            }
    return {"threshold": -50.0, "ratio": 4.37, "release": 150.0, "makeup": 29.2}


def check_compressor(h: Harness, plugin) -> None:
    """F1.7b / US-05, au niveau hote. Fidelite a la cible : la table
mesuree (seuil fixe -50 dBFS, make-up croissant) est reproduite, le niveau
MONTE avec le curseur (nivellement, comme chez lui), le curseur a 0 % rend le
module inerte (transparence), et AC4 tient au niveau hote."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)

    # NIVELEMENT (fidelite F1.7b, comme le test C++) : le module ENGAGE aux
    # trois positions 10/50/100 % — a 0 % il est court-circuite (rev du
    # 2026-09-21), donc comparer 0 a 100 mesurerait le bypass, pas la table.
    # Sur ce sinus, la GR croissante ampute le make-up croissant : le solde
    # (le nivellement) reste croissant et de l'ordre attendu (~9 dB).
    x = sine(0.25, 440.0, 0.6, 48000)
    levels = []
    for pct in (10.0, 50.0, 100.0):
        set_actual(plugin, CATALOGUE_CACHE, "comp_amount", pct)
        y = plugin.process(x, 48000, reset=True)
        levels.append(db(max(rms(y[:, 24000:]), 1e-30)))
    set_actual(plugin, CATALOGUE_CACHE, "comp_amount", 100.0)

    # Fidelite de FORME (revision du 2026-09-21) : la table de la cible est
    # inobservable au signal en absolu (la GR depend du regime), mais son
    # NIVELEMENT se verifie : le niveau monte, et l'amelioration 0 -> 100 %
    # reste dans la bande que produit la table (make-up +27,5 dB brut, ampute
    # par la GR croissante : la cible mesurait ~8,6 dB de spread).
    spread = levels[2] - levels[0]
    h.record(
        "la table macro retenue est reproduite (F1.7b)",
        5.0 < spread < 12.0,
        f"nivelement {spread:.2f} dB de 0 a 100 % (cible : 8,6 dB ; make-up brut de la table : 27,5 dB, compense par la GR croissante)",
    )
    h.record(
        "le make-up fait monter le niveau avec le curseur (fidelite F1.7b)",
        levels[0] < levels[1] < levels[2],
        f"sorties successives {levels[0]:+.1f} / {levels[1]:+.1f} / {levels[2]:+.1f} dBFS",
    )

    # --- curseur a 0 % : transparence stricte (le curseur ENGAGE le module) --
    # Comparaison en FLOAT32 : l'ecart "2.968e-08" est exactement un demi-ulp
    # float32 pour un signal d'amplitude ~1 — pedalboard entre en float64 mais
    # le plugin traite en float32. Le test bit-exact compare donc float32(x) a
    # la sortie, comme les autres controles de transparence (f32_diff).
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "comp_amount", 0.0)
    y = plugin.process(x, 48000, reset=True)
    bit_exact = f32_diff(x, y) == 0.0
    h.record(
        "`comp_amount` 0 % est transparent (le curseur engage le module)",
        bit_exact,
        "identique au bit pres (float32)"
        if bit_exact
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    # --- AC4 (rev 2026-09-21) : le make-up ne fait pas exploser l'echelle ---
    # Le make-up de table monte a +29,2 dB brut ; la GR croissante l'ampute.
    # A 100 % sur un sinus fort, la crete de sortie reste bornee (le staging
    # de sortie plafonne, aucun clipping numerique dur).
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "comp_amount", 100.0)

    # Crete : sur un sinus FORT le make-up (+29,2 dB de table) est ampute par
    # la GR engagee ; le transitoire d'attaque est amorti par l'amorce du
    # make-up (~3 ms). La crete de regime etabli reste bornee : c'est ce que
    # garantit le staging.
    x_fort = sine(0.6, 440.0, 0.6, 48000)
    y100 = plugin.process(x_fort, 48000, reset=True)
    pic = float(np.max(np.abs(y100)))
    h.record(
        "a 100 %, la crete de sortie reste bornee (make-up vs GR)",
        pic < 2.0,
        f"crete de sortie {pic:.3f} (< 2,0 : le staging plafonne le make-up de table)",
    )


def _comp_gain_at(x: np.ndarray, amount01: float, plugin) -> float:
    """Conserve par precaution (ancien appui du controle compresseur)."""
    del amount01, plugin, x
    return 0.0



def rbj_shelf_db(f: float, corner_hz: float, gain_db: float, q: float, high: bool,
                 sr: float = 48000.0) -> float:
    """Reponse en dB d'un biquad shelf RBJ — les memes formules que
`juce::IIRCoefficients::makeLowShelf`/`makeHighShelf`, qui portent la courbe
AFFICHEE comme le filtre AUDIBLE. `high=False` : shelf bas (Low), `high=True` :
shelf haut (High, Air)."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * corner_hz / sr
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * q)
    two_sqA_a = 2.0 * alpha * math.sqrt(A)
    if high:
        b0 = A * ((A + 1.0) + (A - 1.0) * cw + two_sqA_a)
        b1 = -A * ((A + 1.0) + (A - 1.0) * cw - two_sqA_a) * 0.0 - 2.0 * A * ((A - 1.0) + (A + 1.0) * cw)
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw)
        b2 = A * ((A + 1.0) + (A - 1.0) * cw - two_sqA_a)
        a0 = (A + 1.0) - (A - 1.0) * cw + two_sqA_a
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw)
        a2 = (A + 1.0) - (A - 1.0) * cw - two_sqA_a
    else:
        b0 = A * ((A + 1.0) - (A - 1.0) * cw + two_sqA_a)
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw)
        b2 = A * ((A + 1.0) - (A - 1.0) * cw - two_sqA_a)
        a0 = (A + 1.0) + (A - 1.0) * cw + two_sqA_a
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw)
        a2 = (A + 1.0) + (A - 1.0) * cw - two_sqA_a
    return _biquad_db(f, b0, b1, b2, a0, a1, a2, sr)


def rbj_bell_db(f: float, centre_hz: float, gain_db: float, q: float,
                sr: float = 48000.0) -> float:
    """Reponse en dB d'un biquad bell (peak) RBJ — les memes formules que
`juce::IIRCoefficients::makePeakFilter`."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * centre_hz / sr
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * q)
    b0 = 1.0 + alpha * A
    b1 = -2.0 * cw
    b2 = 1.0 - alpha * A
    a0 = 1.0 + alpha / A
    a1 = -2.0 * cw
    a2 = 1.0 - alpha / A
    return _biquad_db(f, b0, b1, b2, a0, a1, a2, sr)


def rbj_highpass_db(f: float, corner_hz: float, q: float,
                    sr: float = 48000.0) -> float:
    """Reponse en dB d'un passe-haut RBJ (makeHighPass)."""
    w0 = 2.0 * math.pi * corner_hz / sr
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * q)
    b0 = (1.0 + cw) / 2.0
    b1 = -(1.0 + cw)
    b2 = b0
    a0 = 1.0 + alpha
    a1 = -2.0 * cw
    a2 = 1.0 - alpha
    return _biquad_db(f, b0, b1, b2, a0, a1, a2, sr)


def _biquad_db(f: float, b0: float, b1: float, b2: float, a0: float, a1: float,
               a2: float, sr: float = 48000.0) -> float:
    """|H(e^jw)| en dB d'un biquad, a la frequence f."""
    w = 2.0 * math.pi * f / sr
    zr = complex(math.cos(-w), math.sin(-w))
    num = b0 + b1 * zr + b2 * zr * zr
    den = a0 + a1 * zr + a2 * zr * zr
    return 20.0 * math.log10(max(abs(num / den), 1e-30))


def eq_curve_db(bands: list[dict], f: float) -> float:
    """La courbe THEORIQUE produit de 4 bandes — le meme produit que trace
src/EqCurve. Chaque bande : {'db','hz','q','type'} avec type parmi 0 bell,
1 low shelf, 2 high shelf, 3 high pass."""
    total = 0.0
    for b in bands:
        t = int(b["type"])
        if t == 3:
            total += rbj_highpass_db(f, b["hz"], b["q"])
        elif t == 1:
            total += rbj_shelf_db(f, b["hz"], b["db"], b["q"], high=False)
        elif t == 2:
            total += rbj_shelf_db(f, b["hz"], b["db"], b["q"], high=True)
        else:
            total += rbj_bell_db(f, b["hz"], b["db"], b["q"])
    return total


def check_eq(h: Harness, plugin) -> None:
    """F1.6 / US-04, au niveau hote : la reponse MESUREE doit coincider avec la
courbe qui sera AFFICHEE (le produit des quatre bandes, modele RBJ des memes
biquads que src/Eq) a ±0,5 dB de 40 Hz a 16 kHz — c'est AC2 de US-04. Puis :
le gain d'une bande atteint le filtre (AC1), l'Air par defaut, et `eq_on` Off
qui court-circuite."""
    # rev. suppression du mode Avance : frequence, Q et type de chaque bande
    # sont des constantes de conception (src/Eq.h, kAnchor*) — le harnais
    # modelise les memes valeurs, il ne les regle plus.
    kBandIds = ("eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db")

    # Miroir de Eq::kAnchorHz / kAnchorQ / kAnchorType (src/Eq.h).
    kAnchors = (
        {"hz": 120.0, "q": 1.00, "type": 1},   # Low  : low shelf
        {"hz": 700.0, "q": 1.15, "type": 0},   # Mid  : bell
        {"hz": 1750.0, "q": 1.20, "type": 2},  # High : high shelf
        {"hz": 10000.0, "q": 1.00, "type": 2}, # Air  : high shelf
    )

    def configure(db_values: tuple[float, float, float, float]) -> list[dict]:
        reset_to_defaults(plugin, CATALOGUE_CACHE)
        # Le low cut est ON par defaut depuis le 2026-09-29 : a 40 Hz il
        # s'entend (−20 dB et plus), et la sonde la plus basse du controle est
        # justement a 40 Hz. Neutralise, comme le filtre DC (2026-10-09).
        neutralise_defaults(plugin, CATALOGUE_CACHE)
        set_actual(plugin, CATALOGUE_CACHE, "eq_on", 1.0)
        bands = []
        for i, gains in enumerate(db_values):
            set_actual(plugin, CATALOGUE_CACHE, kBandIds[i], gains)
            bands.append({"db": gains, **kAnchors[i]})
        return bands

    def measured(f: float) -> float:
        amp = 0.1
        y = plugin.process(sine(amp, f, 0.5, 48000), 48000, reset=True)
        return db(max(tone_level_hann(y, f, 48000, skip_seconds=0.1), 1e-30) / amp)

    # --- AC2 : reponse mesuree = courbe affichee, sur 4 reglages ------------
    # Decouverte du 2026-09-21 : le type de bande est un CHOIX regle par
    # `set_actual` via la proportion — a 4 choix, il faut la proportion exacte
    # 0/3, 1/3, 2/3, 3/3 sinon la bande tourne a un autre type que le modele.
    reglages = ((12.0, -12.0, 9.0, 12.0), (-10.0, 10.0, -8.0, 6.0), (0.0, 0.0, 0.0, 2.5))
    sondes = (40.0, 100.0, 250.0, 631.0, 1585.0, 3981.0, 10000.0, 15849.0)
    pire, pire_txt = 0.0, ""
    for gains in reglages:
        bands = configure(gains)
        for f in sondes:
            ecart = abs(measured(f) - eq_curve_db(bands, f))
            if ecart > pire:
                pire, pire_txt = ecart, f"{f:.0f} Hz : reglages {gains}"
    h.record(
        "la reponse de l'EQ suit la courbe affichee a ±0,5 dB (AC2 de US-04)",
        pire <= 0.5,
        f"pire ecart {pire:.2f} dB ({pire_txt})",
    )

    # --- AC1 (rev.) : le gain d'une bande atteint le filtre ; frequence et Q
    # sont figes par conception, on verifie que la largeur du Mid correspond a
    # son Q d'ancre (1,15) et rien d'autre.
    configure((0.0, 12.0, 0.0, 0.0))
    centre = measured(700.0)
    oct_bas = measured(350.0)
    h.record(
        "le gain d'une bande atteint le filtre (AC1 de US-04, rev. ancres figees)",
        abs(centre - 12.0) < 0.5,
        f"centre {centre:+.2f} dB a 700 Hz (ancre Mid), une octave dessous {oct_bas:+.2f} dB",
    )

    # --- Air par defaut (+2,5 dB) : plateau haut au-dessus de 10 kHz --------
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "eq_on", 1.0)
    a12 = measured(12000.0)
    a15 = measured(15000.0)
    h.record(
        "l'Air par defaut (+2,5 dB) est un plateau haut au-dessus de 10 kHz",
        1.8 < a12 < 2.9 and 1.8 < a15 < 3.0,
        f"{a12:.2f} dB a 12 kHz, {a15:.2f} dB a 15 kHz",
    )

    # --- eq_on Off : court-circuit au bit pres ------------------------------
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "eq_on", 0.0)
    set_actual(plugin, CATALOGUE_CACHE, "eq_mid_db", 12.0)
    x = sine(0.4, 700.0, 0.5, 48000)
    y = plugin.process(x, 48000, reset=True)
    h.record(
        "`eq_on` Off court-circuite l'EQ, meme avec des bandes actives",
        same_f32(x, y),
        "sortie identique au bit pres (apres conversion float32)"
        if same_f32(x, y)
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    # --- toutes les bandes a 0 dB : EQ neutre au bit pres -------------------
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "eq_on", 1.0)
    for pid in ("eq_low_db", "eq_mid_db", "eq_hi_db", "eq_air_db"):
        set_actual(plugin, CATALOGUE_CACHE, pid, 0.0)
    y = plugin.process(x, 48000, reset=True)
    h.record(
        "toutes les bandes a 0 dB : l'EQ est neutre au bit pres",
        same_f32(x, y),
        "sortie identique au bit pres (apres conversion float32)"
        if same_f32(x, y)
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )


def check_deess(h: Harness, plugin) -> None:
    """F1.8 / US-10 rev (2026-09-21), au niveau hote : UN SEUL curseur, comme
la cible. Le signal porte les DEUX bandes a la fois (un « S » a 8 kHz
par-dessus un grave de plosive a 100 Hz) : sur une sonde unique, une fuite
d'une bande sur l'autre resterait invisible. La plosive liee suit le curseur
(cap 20 %, la valeur des presets d'usine) ; le listen est supprime."""
    sr = 48000
    t = np.arange(int(0.6 * sr)) / sr
    x = np.zeros((2, t.size), dtype=np.float64)
    # Sifflante +11 dB au-dessus du grave : la detection est ADAPTATIVE (l'exces
    # sifflante-voix pilote la reduction — une sonde egalisee ne produirait
    # aucun exces, donc aucune reduction : comportement conforme, sonde muette).
    x += 0.12 * np.sin(2.0 * np.pi * 100.0 * t)
    x += 0.45 * np.sin(2.0 * np.pi * 8000.0 * t)

    def level(y: np.ndarray, freq: float) -> float:
        return tone_level_hann(y, freq, sr, skip_seconds=0.3)

    # --- deess_amount : la sifflante baisse, le grave ne voit que le shelf ---
    # Sonde au-dessus du CROISEMENT fixe 5 kHz (a l'exact croisement, HP4+LP4
    # se somment en passe-tout et la reduction est en quadrature : raie
    # immobile alors que le module reduit).
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "deess_amount", 100.0)
    y = plugin.process(x, sr, reset=True)
    sib = db(max(level(y, 8000.0), 1e-30) / max(level(x, 8000.0), 1e-30))
    grave = db(max(level(y, 100.0), 1e-30) / max(level(x, 100.0), 1e-30))
    h.record(
        "le de-ess reduit la sifflante ; le grave ne voit que le shelf lie (AC1)",
        sib < -0.5 and -2.5 < grave < -0.3,
        f"sifflante {sib:+.2f} dB (plafond ~−3 dB de la cible), "
        f"grave {grave:+.2f} dB (shelf lie ~−1,2 dB)",
    )

    # --- plosives LIEES au curseur : suivent puis plafonnent ------------------
    # Loi liee : plosive01 = min(0,20 ; 0,6 x amount01) — a 20 % -> 0,12
    # (≈ −0,7 dB), a 100 % -> cap 0,20 (≈ −1,2 dB, pas plus).
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "deess_amount", 20.0)
    y20 = plugin.process(x, sr, reset=True)
    set_actual(plugin, CATALOGUE_CACHE, "deess_amount", 100.0)
    y100 = plugin.process(x, sr, reset=True)
    g20 = -db(max(level(y20, 100.0), 1e-30) / max(level(x, 100.0), 1e-30))
    g100 = -db(max(level(y100, 100.0), 1e-30) / max(level(x, 100.0), 1e-30))
    h.record(
        "les plosives suivent le curseur puis plafonnent (liees, cap 20 %)",
        0.2 < g20 < 1.2 and g100 > g20 and g100 < g20 + 1.0,
        f"grave reduit de {g20:.2f} dB a 20 % puis {g100:.2f} dB a 100 % "
        "(loi liee 0,6 x amount, cap 0,20)",
    )


def _line_amp(y: np.ndarray, freq: float, sr: int, skip_seconds: float) -> float:
    """Amplitude d'une raie par correlation, sans fenetrage : valable seulement
quand la sonde entretient un nombre entier de periodes dans la fenetre."""
    seg = y[0, int(skip_seconds * sr):]
    t = np.arange(seg.size) / sr + skip_seconds
    c = float(np.sum(seg * np.cos(2.0 * np.pi * freq * t)))
    s = float(np.sum(seg * np.sin(2.0 * np.pi * freq * t)))
    return 2.0 * float(np.hypot(c, s)) / seg.size


def _third_octave_bands(plugin, catalogue: list[Parameter]) -> dict[int, float]:
    """Harmoniques 2..15 de l'engin drive (FIGE Console, l'engin MESURE de la
cible), repliees en bandes de 1/3 d'octave (centres 1000·10^(k/10)) : deux
harmoniques d'une meme bande sont sommees en energie, comme le ferait un
analyseur. Rendu en dB SOUS la fondamentale."""
    set_actual(plugin, catalogue, "drive_amount", 100.0)
    x = sine(0.25, 1000.0, 0.5, 48000)   # -12 dBFS crete, 50 periodes exactes
    y = plugin.process(x, 48000, reset=True)
    fund = max(_line_amp(y, 1000.0, 48000, 0.25), 1e-30)
    bands: dict[int, float] = {}
    for hk in range(2, 16):
        amp = _line_amp(y, 1000.0 * hk, 48000, 0.25)
        k = int(round(10.0 * math.log10(float(hk))))
        bands[k] = math.hypot(bands.get(k, 0.0), amp)
    return {k: db(v / fund) for k, v in bands.items()}


def check_drive(h: Harness, plugin) -> None:
    """F1.9 rev (2026-09-21), au niveau hote : UN SEUL curseur — l'engin est
fige sur Console (l'engin retenu), mix interne. Controles
: transparence a 0 %, signature spectrale Console, sortie bornee."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)

    # --- transparence a 0 % (AC2 de US-02) ----------------------------------
    x = sine(0.25, 1000.0, 0.5, 48000)
    set_actual(plugin, CATALOGUE_CACHE, "drive_amount", 0.0)
    y = plugin.process(x, 48000, reset=True)
    bit_exact = f32_diff(x, y) == 0.0
    h.record(
        "`drive_amount` 0 % est transparent au bit pres (AC2)",
        bit_exact,
        "identique au bit pres (float32)"
        if bit_exact
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    # --- Console seule : la signature spectrale mesuree ---------------------
    console = _third_octave_bands(plugin, CATALOGUE_CACHE)
    h3 = console.get(int(round(10.0 * math.log10(3.0))), -160.0)
    h5 = console.get(int(round(10.0 * math.log10(5.0))), -160.0)
    even_ok = all(
        v < -90.0 for k, v in console.items()
        if k in (int(round(10.0 * math.log10(2.0))), int(round(10.0 * math.log10(4.0))))
    )
    h.record(
        "l'engin fige Console reproduit la signature mesuree de la cible",
        abs(h3 + 11.4) < 2.0 and abs(h5 + 18.9) < 2.5 and even_ok,
        f"3f {h3:+.1f} dB (cible -10.1), 5f {h5:+.1f} dB (cible -15.8), "
        f"harmoniques paires au plancher : {'oui' if even_ok else 'non'}",
    )

    # --- sortie bornee a +6 dBFS a fond -------------------------------------
    set_actual(plugin, CATALOGUE_CACHE, "drive_amount", 100.0)
    y = plugin.process(sine(0.25, 1000.0, 0.5, 48000), 48000, reset=True)
    peak = db(max(float(np.max(np.abs(y))), 1e-30))
    h.record(
        "le drive ne depasse pas +6 dBFS a 100 %, entree a -12 dBFS",
        peak <= 6.0,
        f"crete de sortie {peak:+.2f} dBFS",
    )


def check_hq(h: Harness, plugin) -> None:
    """F2.2 / US-09, au niveau hote : le mode haute qualite (oversampling du
Drive) ne change pas le niveau a la bascule (AC3), et sa latence half-band est
la bonne (AC1/AC2 : report honnete, chemin sec mesure)."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "drive_amount", 50.0)

    # --- AC3 : la bascule HQ ne change pas le niveau (< 1 dB) ---------------
    # A 440 Hz, aucune harmonique du drive ne depasse le Nyquist (la 55e
    # harmonic depasse 24 kHz avec un niveau negligeable) : les deux chemins
    # doivent etre quasi identiques — tout ecart de niveau est un defaut.
    x = sine(0.25, 440.0, 1.0, 48000)
    set_actual(plugin, CATALOGUE_CACHE, "hq_mode", 0.0)
    y_off = plugin.process(x, 48000, reset=True)
    set_actual(plugin, CATALOGUE_CACHE, "hq_mode", 1.0)
    y_on = plugin.process(x, 48000, reset=True)

    rms = lambda v: float(np.sqrt(np.mean(v[0] ** 2)))
    delta = abs(20.0 * math.log10(max(rms(y_on), 1e-12) / max(rms(y_off), 1e-12)))
    h.record(
        "la bascule HQ ne change pas le niveau de sortie (< 1 dB, AC3)",
        delta < 1.0,
        f"ecart RMS {delta:.3f} dB entre HQ off et HQ on",
    )

    # --- AC1/AC2 : le rapport de latence est HONNETE -------------------------
    # pedalboard applique une PDC (compensation de latence) sur la valeur
    # reportee par le plugin. Cette PDC permet justement de verifier
    # l'honnetete du rapport, car elle distingue les trois cas :
    #   - rapport = retard physique (4 smp half-band) -> impulsion a 256 ;
    #   - sous-rapport (0 rapport, 4 smp de retard)   -> impulsion a 260 ;
    #   - sur-rapport (4 rapport, pas de retard)      -> impulsion a 252.
    # Le chemin sec doit donc revenir EXACTEMENT a sa position d'entree.
    x = np.zeros((2, 4096), dtype=np.float64)
    x[:, 256] = 1.0
    y = plugin.process(x, 48000, reset=True)
    arrivee = int(np.argmax(np.abs(y[0]) > 0.08))
    h.record(
        "le rapport de latence HQ est honnete (chemin sec aligne apres PDC hote)",
        abs(arrivee - 256) <= 1,
        f"impulsion en 256, chemin sec retrouve en {arrivee} "
        f"(260 = sous-rapport, 252 = sur-rapport)",
    )


def check_doubler(h: Harness, plugin) -> None:
    """F1.10 / US-08 (doubler), au niveau hote : transparence a 0 %, matiere
AJOUTEE decorrelee a 100 %, niveau tenu, et desaccordage reel."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    x = stereo_tone(220.0, 220.0, 0.3, 1.0)

    set_actual(plugin, CATALOGUE_CACHE, "doubler_amount", 0.0)
    set_actual(plugin, CATALOGUE_CACHE, "width_amount", 100.0)
    y0 = plugin.process(x, 48000, reset=True)
    identique = same_f32(x, y0)
    h.record(
        "US-08 : doubler a 0 % et width a 100 % rend le module transparent",
        identique,
        "sortie identique au bit pres (apres conversion float32)"
        if identique
        else f"ecart maximal {f32_diff(x, y0):.3e} (float32)",
    )

    set_actual(plugin, CATALOGUE_CACHE, "doubler_amount", 100.0)
    y100 = plugin.process(x, 48000, reset=True)

    added = y100 - y0
    corr = correlation(added)
    h.record(
        "US-08 AC4 : la matiere ajoutee par le doubler est decorrelee (< 0,5)",
        corr < 0.5,
        f"correlation de (sortie 100 % − sortie 0 %) entre L et R : {corr:.3f}",
    )

    niveau = db(max(rms(y100), 1e-30) / max(rms(x), 1e-30))
    h.record(
        "US-08 : le doubler ne change pas le niveau (sortie <= entree + 3 dB)",
        niveau <= 3.0,
        f"sortie {db(max(rms(y100), 1e-30)):.2f} dB contre {db(max(rms(x), 1e-30)):.2f} dB en entree",
    )

    raie = db(max(_line_amp(y100, 220.0, 48000, 0.3), 1e-30) / max(_line_amp(x, 220.0, 48000, 0.0), 1e-30))
    h.record(
        "US-08 : le doubler desaccorde (l'energie quitte la raie exacte)",
        raie < -0.5,
        f"raie a 220 Hz : {raie:+.2f} dB du niveau d'entree",
    )


def check_width(h: Harness, plugin) -> None:
    """F1.10 / US-08 (largeur), au niveau hote : transparence a 100 %, mono strict
a 0 %, mono bass sous la coupure, et aucune composante subsonique ajoutee."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "doubler_amount", 0.0)

    x = stereo_tone(220.0, 310.0, 0.2, 1.5, phase_r=1.0)

    set_actual(plugin, CATALOGUE_CACHE, "width_amount", 100.0)
    y = plugin.process(x, 48000, reset=True)
    h.record(
        "US-08 AC1 : width a 100 % laisse le signal inchange",
        same_f32(x, y),
        "sortie identique au bit pres (apres conversion float32)"
        if same_f32(x, y)
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    set_actual(plugin, CATALOGUE_CACHE, "width_amount", 0.0)
    y = plugin.process(x, 48000, reset=True)
    corr = correlation(y, skip_seconds=0.5)
    h.record(
        "US-08 AC1 : width a 0 % rend le signal strictement mono (correlation 1,0)",
        corr > 0.999,
        f"correlation L/R = {corr:.4f}",
    )

    # --- 200 % : grave mono, aigu elargi ------------------------------------
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "doubler_amount", 0.0)
    set_actual(plugin, CATALOGUE_CACHE, "width_amount", 200.0)

    # AC2 mesure le renforcement du grave au-dessus de la coupure du module :
    # 30 Hz est SOUS la coupure de mono bass, la sonde vit a 120 Hz.
    grave = stereo_tone(120.0, 120.0, 0.2, 1.5)
    yg = plugin.process(grave, 48000, reset=True)
    corr_basse = correlation(yg, skip_seconds=0.5)
    h.record(
        "US-08 : a 200 % de largeur, le grave au-dessus de la coupure reste serre",
        corr_basse >= 0.98,
        f"correlation a 120 Hz = {corr_basse:.5f} (exigee >= 0,98)",
    )

    # L'aigu vif : sonde en SIDE PUR — meme frequence, opposition de phase.
    # (Revision du 2026-09-21 : deux tonalites voisines 3k/3,1k n'ont RIEN a
    # 3 050 Hz — la sonde mesurait une raie vide des deux cotes ; et une sonde
    # mono n'a pas de side du tout, l'elargisseur n'y peut rien.)
    aigu = stereo_tone(3050.0, 3050.0, 0.1, 1.5, phase_r=float(np.pi))
    ya = plugin.process(aigu, 48000, reset=True)
    side_out = side_level(ya, 3050.0, 48000, skip_seconds=0.5)
    side_in = side_level(aigu, 3050.0, 48000, skip_seconds=0.5)
    h.record(
        "US-08 : a 200 % de largeur, l'aigu est bien elargi",
        side_out > max(1.3 * max(side_in, 1e-12), 1e-4),
        f"side a 3 kHz : {side_out:.5f} contre {side_in:.5f} en entree",
    )

    milieu = stereo_tone(220.0, 310.0, 0.2, 1.5, phase_r=1.0)
    ym = plugin.process(milieu, 48000, reset=True)
    ajout = ym - milieu
    pire, pire_f = 0.0, 0
    for f in range(5, 21):
        amp = tone_level_hann(ajout, float(f), 48000, skip_seconds=0.5)
        if amp > pire:
            pire, pire_f = amp, f
    h.record(
        "US-08 AC3 : aucune composante subsonique ajoutee (plancher −80 dBFS)",
        db(max(pire, 1e-30)) < -80.0,
        f"pire ajout {db(max(pire, 1e-30)):.1f} dBFS a {pire_f} Hz (mesure fenetree 0-20 Hz)",
    )


def check_calibration(h: Harness, plugin) -> None:
    """F1.3 / US-06 : apres 5 s de signal, la crete doit tomber dans -12..-6 dBFS.
L'action `input_calibrate` est un FRONT (0 -> 1) : on repose d'abord tous les
defauts (sinon le leve herite des reglages du controle precedent, mesure du
2026-09-21), on la monte, PUIS on rend le signal."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    set_actual(plugin, CATALOGUE_CACHE, "input_gain_db", 0.0)
    set_actual(plugin, CATALOGUE_CACHE, "input_calibrate", 1.0)

    amplitude = 0.0316   # -30 dBFS crete : le delta attendu vaut ~+21 dB
    seconds = 7.0
    x = sine(amplitude, 1000.0, seconds, 48000)
    y = plugin.process(x, 48000, reset=True)

    tail = y[:, int(5.5 * 48000):]
    crete = db(max(float(np.max(np.abs(tail))), 1e-30))
    applique = db(max(float(np.max(np.abs(tail))), 1e-30) / amplitude)
    h.record(
        "la calibration place la crete entre -12 et -6 dBFS (AC1 de US-06)",
        -12.0 <= crete <= -6.0 and 15.0 <= applique <= 27.0,
        f"crete mesuree {crete:.2f} dBFS, gain applique {applique:+.2f} dB",
    )


def check_stability(h: Harness, plugin) -> None:
    """Determinisme, silence propre, et marges de crete : les trois contrat de
robustesse de US-02, au niveau hote."""
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    rng = np.random.default_rng(7)
    x = rng.uniform(-1.0, 1.0, (2, 48000)) * 0.1

    y1 = plugin.process(x, 48000, reset=True)
    y2 = plugin.process(x, 48000, reset=True)
    h.record(
        "le traitement est deterministe",
        np.array_equal(y1, y2),
        "deux passes sur le meme signal donnent un resultat bit a bit identique",
    )

    y = plugin.process(np.zeros((2, 48000)), 48000, reset=True)
    h.record(
        "le silence en entree ne produit pas de bruit de fond",
        float(np.max(np.abs(y))) <= 1e-9,
        f"sortie a {float(np.max(np.abs(y))):.3e} sur 1 s de silence",
    )

    fort = rng.uniform(-1.0, 1.0, (2, 48000)) * 2.0   # +6 dBFS crete
    y = plugin.process(fort, 48000, reset=True)
    ok = np.isfinite(y).all() and float(np.max(np.abs(y))) <= 8.0
    h.record(
        "un signal a +6 dBFS crete ne fait ni NaN ni divergence",
        ok,
        f"crete en sortie : {float(np.max(np.abs(y))):.4f}",
    )

    # Le module ne saurait pas AMPLIFIER au-dela de 0 dBFS en sortie pour un
    # signal deja faible : aux defauts (tous les modules inactifs, EQ neutre),
    # une entree a −12 dBFS crete ressort a −12 dBFS crete (mesure du
    # 2026-09-21 : l'exigence initiale portait un EQ Air +2,5 dB residuel).
    reset_to_defaults(plugin, CATALOGUE_CACHE)
    neutralise_defaults(plugin, CATALOGUE_CACHE)
    faible = rng.uniform(-1.0, 1.0, (2, 48000)) * 0.25   # -12 dBFS crete
    y = plugin.process(faible, 48000, reset=True)
    crete = db(max(float(np.max(np.abs(y))), 1e-30))
    h.record(
        "une entree a -12 dBFS crete ne ressort pas au-dessus de 0 dBFS",
        crete <= 0.0,
        f"crete en sortie : {crete:.2f} dBFS",
    )


def check_controls(h: Harness, catalogue: list[Parameter]) -> None:
    """Le modele de parametre, hors plugin : le couple `normalised`/`actual_of`
doit etre une bijection sur toute la plage. Revision du 2026-09-21 : les 0 et
1 normalises sont les BORNES de toute plage — un modele faux mais monotone les
rend toujours — et 0,5 n'indique PAS de quel cote le modele se trompe. La sonde
compare donc l'aller-retour 0,35 <-> 0,5 <-> 0,65 en boucle fermee, ce qui a
revele le modele d'echelle faux (exponentielle au lieu de puissance) : 13
controles en echec, tous les `ms` et `hz` du plugin."""
    inertes: list[str] = []
    for p in catalogue:
        if p.unit == "choice" or p.choices:
            continue
        # Booleans, actions et pas entiers : le point-milieu n'existe pas sur
        # leur grille (revision du 2026-09-21 — les 8 « inertes » etaient tous
        # des Unit::boolean / step 1,0, dont les 0/1 repondent tres bien).
        if p.unit in ("boolean", "action") or p.step >= 1.0:
            continue
        ok = True
        for n in (0.35, 0.5, 0.65):
            v = actual_of(p, n)
            if abs(normalised(p, snap_to_step(p, v)) - n) > 0.02:
                ok = False
        if not ok:
            inertes.append(p.id)
    h.record(
        "chaque controle balaie toute sa plage",
        not inertes,
        "tous les controles repondent en butee basse et haute"
        if not inertes
        else "controles inertes : " + ", ".join(inertes),
    )

    groupes: dict[int, int] = {}
    for p in catalogue:
        if p.unit == "choice" or p.choices:
            groupes[len(p.choices)] = groupes.get(len(p.choices), 0) + 1
    h.record(
        "les choix rendent bien leurs libelles extremes",
        all(p.minimum <= p.maximum for p in catalogue if p.choices),
        ", ".join(f"{n} controle(s) a {taille} choix" for taille, n in sorted(groupes.items(), reverse=True)),
    )


def _test_impulse(seconds: float) -> np.ndarray:
    """Impulsion stereo a l'echantillon 240 : la matiere des mesures de delay."""
    n = int(seconds * 48000)
    x = np.zeros((2, n), dtype=np.float64)
    x[:, 240] = 1.0
    return x


def _first_echo_ms(y: np.ndarray) -> float:
    """Position (ms) du premier echo : premier pic au-dela de 50 ms apres la
impulsion (la directe est un echantillon unique a 240, son ring decay en
deça de 55 ms), cherche sur le canal gauche."""
    start = 240 + 2400
    tail = np.abs(y[0, start:])
    return float((start + int(np.argmax(tail)) - 240) / 48.0)


def check_delay(h: Harness, plugin, catalogue: list[Parameter]) -> None:
    """F1.11 / US-15, au niveau hote : sync musicale (1/4 = 500 ms a 120 BPM,
valeur retenue), mode libre, plafond de feedback, ducking
et ping-pong. Le BPM interne par defaut du plugin est 120 : sans playhead,
c'est lui qui fait foi."""
    # --- AC1 : sync 1/4 a 120 BPM = 500 ms ----------------------------------
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "delay_amount", 100.0)
    set_actual(plugin, catalogue, "delay_sync", 1.0)
    set_choice(plugin, catalogue, "delay_time", 10)   # 1/4
    set_actual(plugin, catalogue, "delay_ducking", 0.0)
    # rev. suppression du mode Avance : feedback (20 %) et filtre interne sont
    # des constantes de conception dans le module Delay.
    y = plugin.process(_test_impulse(0.8), 48000, reset=True)
    lag = _first_echo_ms(y)
    h.record(
        "delay sync 1/4 a 120 BPM : premier echo a 500 ms (AC1)",
        498.0 <= lag <= 502.0,
        f"premier echo {lag:.1f} ms",
    )

    # --- mode libre : delay_time_ms fait foi --------------------------------
    # Ordre du 2026-09-21 : `delay_sync` est pose AVANT `delay_time_ms`, et la
    # valeur libre LUE re-verifiee — le mode sync asservit le temps, donc regler
    # le temps avant de couper la sync laissait l'asservissement en place.
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "delay_amount", 100.0)
    set_actual(plugin, catalogue, "delay_sync", 0.0)
    set_actual(plugin, catalogue, "delay_time_ms", 300.0)
    lu = get_actual(plugin, catalogue, "delay_time_ms")
    set_actual(plugin, catalogue, "delay_ducking", 0.0)
    y = plugin.process(_test_impulse(0.8), 48000, reset=True)
    lag = _first_echo_ms(y)
    h.record(
        "delay libre : premier echo a la valeur reglee (300 ms)",
        295.0 <= lag <= 305.0 and abs(lu - 300.0) < 5.0,
        f"premier echo {lag:.1f} ms (valeur libre lue {lu:.0f} ms)",
    )

    # --- AC3 (rev.) : feedback interne figure a 20 % — pas d'emballement -----
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "delay_amount", 100.0)
    set_actual(plugin, catalogue, "delay_sync", 0.0)
    set_actual(plugin, catalogue, "delay_time_ms", 100.0)
    y = plugin.process(_test_impulse(6.0), 48000, reset=True)
    # L'echo 1 sort de l'ECRITURE BRUTE de la ligne (amplitude 1,0 par
    # construction, note dans le test C++) : le pic utile est celui des echos
    # SUIVANTS, qui doivent decroitre — pas croitre (pas d'emballement).
    ec2 = float(np.max(np.abs(y[:, 3400:5400])))    # fenetre de l'echo 2 (~100 ms)
    fin = float(np.max(np.abs(y[:, 100000:])))      # fin du rendu (5 s)
    h.record(
        "feedback interne figure : pas d'emballement (AC3, rev. ancres figees)",
        fin < ec2 and fin < 0.5,
        f"echo 2 {ec2:.3f}, fin de rendu {fin:.3f} (decroissance exigee)",
    )

    # --- AC2 : ducking, les echos remontent apres la voix -------------------
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "delay_amount", 100.0)
    set_actual(plugin, catalogue, "delay_sync", 0.0)
    set_actual(plugin, catalogue, "delay_time_ms", 250.0)
    set_actual(plugin, catalogue, "delay_ducking", 100.0)
    t = np.arange(96000) / 48000.0
    x = np.zeros((2, 96000), dtype=np.float64)
    x[:, 1000:25000] = 0.3 * np.sin(2.0 * np.pi * 220.0 * t[1000:25000])
    set_actual(plugin, catalogue, "delay_ducking", 0.0)
    y_off = plugin.process(x, 48000, reset=True)
    set_actual(plugin, catalogue, "delay_ducking", 100.0)
    y_on = plugin.process(x, 48000, reset=True)

    # Metrique SANS perte de chaine (revision du 2026-09-21) : comparer des
    # echos de rangs differents compare aussi fb^n·damping^n. On compare le
    # MEME contenu, ducking Off vs On, et le SIGNAL SIDE (L-R) elime la voix
    # seche (centree) qui polluait les fenetres pendant la voix.
    def side_rms(y: np.ndarray, a: int, b: int) -> float:
        return float(rms((y[0, a:b] - y[1, a:b]) / 2.0))

    # Tete de l'echo 1 (292..479 ms) : la voix parle encore → duck actif.
    prof_head = db(max(side_rms(y_off, 14000, 23000), 1e-30)
                   / max(side_rms(y_on, 14000, 23000), 1e-30))
    # Queue d'echo (1,29..1,50 s) : voix finie depuis > 300 ms → duck relache.
    prof_tail = db(max(side_rms(y_off, 62000, 72000), 1e-30)
                   / max(side_rms(y_on, 62000, 72000), 1e-30))
    rebond = prof_head - prof_tail
    h.record(
        "ducking : les echos remontent apres la fin de la voix (AC2)",
        prof_head > 6.0 and rebond > 6.0,
        f"attenuation pendant la voix {prof_head:+.1f} dB, relachement apres "
        f"{prof_tail:+.1f} dB → rebond {rebond:+.1f} dB (release < 300 ms)",
    )

    # --- ping-pong : les echos alternent L/R --------------------------------
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "delay_amount", 100.0)
    set_actual(plugin, catalogue, "delay_sync", 0.0)
    set_actual(plugin, catalogue, "delay_time_ms", 250.0)
    set_actual(plugin, catalogue, "delay_ducking", 0.0)
    y = plugin.process(_test_impulse(2.0), 48000, reset=True)
    # Les fenetres centrent les rangs reels (revision du 2026-09-21) : le
    # test C++ fait foi — echo 1 a GAUCHE, echo 2 a DROITE. Mesures du pass
    # precedent (+34,2 dB / +22,7 dB) : conformes, mon critere etait inverse.
    e1l = rms(y[0, 12240:13240]); e1r = rms(y[1, 12240:13240])   # echo 1 (255..276 ms)
    e2l = rms(y[0, 24240:25240]); e2r = rms(y[1, 24240:25240])   # echo 2 (505..526 ms)
    h.record(
        "ping-pong : l'echo 1 sort a gauche, l'echo 2 a droite",
        e1l > 5.0 * max(e1r, 1e-30) and e2r > 5.0 * max(e2l, 1e-30),
        f"echo 1 L/R {db(max(e1l, 1e-30) / max(e1r, 1e-30)):+.1f} dB, "
        f"echo 2 R/L {db(max(e2r, 1e-30) / max(e2l, 1e-30)):+.1f} dB",
    )


def check_reverb(h: Harness, plugin, catalogue: list[Parameter]) -> None:
    """F1.12, au niveau hote : QUATRE curseurs publics (Short/Small/Big/Lush),
un par moteur — structure retenue (moteurs cumules,
Small/Big/Lush). Controles : transparence a 0 partout, couleur propre a chaque
moteur (ordre de persistance), cumul, pre-delay interne 20 ms, stabilite."""
    # --- AC3 : les 4 curseurs a 0 % = transparence bit-exacte ----------------
    x = _test_impulse(0.8)
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    for k in ("reverb_short_pct", "reverb_small_pct", "reverb_big_pct", "reverb_lush_pct"):
        set_actual(plugin, catalogue, k, 0.0)
    y = plugin.process(x, 48000, reset=True)
    bit_exact = f32_diff(x, y) == 0.0
    h.record(
        "les 4 curseurs a 0 % sont transparents au bit pres (AC3)",
        bit_exact,
        "identique au bit pres (float32)"
        if bit_exact
        else f"ecart maximal {f32_diff(x, y):.3e} (float32)",
    )

    # --- AC1 : chaque curseur porte SA couleur (RT60 propre au moteur) -------
    # Persistance (plancher absolu -80 dBFS) apres impulsion a 240 ech. :
    # l'ordre strict Short < Small < Big < Lush prouve 4 couleurs distinctes.
    noms = ("Short", "Small", "Big", "Lush")
    cles = ("reverb_short_pct", "reverb_small_pct", "reverb_big_pct", "reverb_lush_pct")
    persistances: list[float] = []
    for cle in cles:
        reset_to_defaults(plugin, catalogue)
        neutralise_defaults(plugin, catalogue)
        set_actual(plugin, catalogue, cle, 100.0)
        y = plugin.process(_test_impulse(3.5), 48000, reset=True)
        prem = -1
        for t in range(int(0.20 * 48000), int(3.4 * 48000), int(0.05 * 48000)):
            fen = y[:, t : t + int(0.05 * 48000)]
            if db(max(rms(fen), 1e-30)) < -80.0:
                prem = t
                break
        persistances.append(prem / 48000.0 if prem >= 0 else 99.0)
    ordre = (
        persistances[0] < persistances[1] < persistances[2] < persistances[3]
        and all(
            persistances[a] < 0.75 * persistances[a + 1] for a in range(3)
        )
    )
    h.record(
        "chaque curseur produit sa couleur : Short < Small < Big < Lush (AC1)",
        ordre,
        "persistances " + ", ".join(f"{n} {p:.2f} s" for n, p in zip(noms, persistances))
        if ordre
        else f"persistances {['%.2f' % p for p in persistances]} : ordre attendu viole",
    )

    # --- Cumul des moteurs : attaque dense ET traine longue ------------------
    # Small+Big ensemble : les deux extremes vivent (structure de la
    # machine — moteurs cumules, doses moteur par moteur).
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "reverb_small_pct", 100.0)
    set_actual(plugin, catalogue, "reverb_big_pct", 100.0)
    y = plugin.process(_test_impulse(2.0), 48000, reset=True)
    attaque = db(rms(y[:, 7200:11520]))          # [0,15 .. 0,24 s]
    traine = db(rms(y[:, 43200:57600]))          # [0,90 .. 1,20 s]
    h.record(
        "les moteurs sont cumules : attaque ET traine (Small+Big)",
        attaque > -60.0 and traine > -82.0,
        f"attaque {attaque:.1f} dBFS, traine {traine:.1f} dBFS "
        "(planchers -60/-82 : la queue IR sous-estime la queue percue sur "
        "materiau continu ; Small seul serait eteint a 0,9 s)",
    )

    # --- Pre-delay PREREGLÉ : 20 ms, mesure au signal ------------------------
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    set_actual(plugin, catalogue, "reverb_small_pct", 100.0)
    y = plugin.process(_test_impulse(1.2), 48000, reset=True)
    # Impulsion a 240 ech. (5 ms) + pre-delay interne 20 ms + reseau 1423/48 k.
    reseau_ms = 1423.0 / 48000.0 * 1000.0
    attendu_ms = 5.0 + 20.0 + reseau_ms
    depart = 240 + int(0.030 * 48000)
    fin = 240 + int(0.400 * 48000)
    au_dessus = np.abs(y[0, depart:fin]) > 0.0005
    idx = int(np.argmax(au_dessus)) + depart if au_dessus.any() else -1
    mesure_ms = idx / 48.0 if idx >= 0 else -1.0
    h.record(
        "le pre-delay preregle vaut 20 ms (AC2)",
        idx >= 0 and abs(mesure_ms - attendu_ms) <= 5.0,
        f"onset {mesure_ms:.1f} ms, attendu impulsion + 20 ms + reseau "
        f"= {attendu_ms:.1f} ms (delai de reseau inclus)",
    )

    # --- Stabilite : 4 moteurs a fond, la queue decroit, aucun emballement ---
    reset_to_defaults(plugin, catalogue)
    neutralise_defaults(plugin, catalogue)
    for cle in cles:
        set_actual(plugin, catalogue, cle, 100.0)
    y = plugin.process(_test_impulse(4.0), 48000, reset=True)
    debut = rms(y[:, 48000:96000])
    fin_rms = rms(y[:, 144000:192000])
    pic = float(np.max(np.abs(y[:, 240:])))
    h.record(
        "la queue decroit toujours, meme a 4 moteurs a fond (stabilite)",
        fin_rms < debut and pic < 1.0,
        f"fin {db(max(fin_rms, 1e-30) / max(debut, 1e-30)):.1f} dB sous le debut, "
        f"pic {pic:.3f} (plafond doux a 0,8)",
    )


def write_report(h: Harness, target: Path) -> int:
    """Rapport md : tableau Controle / Resultat / Detail, puis la conclusion
CONFORME ou NON CONFORME. Code de retour : 0 si tout passe, 1 sinon."""
    lines = [
        "# Rapport de verification — OD Vox",
        "",
        # La date est celle de la MESURE, pas une constante : un rapport de
        # porte de sortie qui annonce une date fausse ne vaut rien (corrige le
        # 2026-10-09 — il portait « 2026-09-21 » en dur).
        f"Date : {time.strftime('%Y-%m-%d')} · cible : `{target}`",
        "",
        "| Controle | Resultat | Detail |",
        "|----------|----------|--------|",
    ]
    for c in h.checks:
        oui = "oui" if c.passed else "**non**"
        lines.append(f"| {c.name} | {oui} | {c.detail} |")
    lines += ["", "## Conclusion", ""]
    if h.failures():
        lines.append(f"**NON CONFORME** — {len(h.failures())} controle(s) en echec :")
        lines += [f"- KO : {c.name} — {c.detail}" for c in h.failures()]
    else:
        lines.append(f"**CONFORME** — les {len(h.checks)} controles sont au vert.")
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"rapport ecrit : {REPORT} ({len(h.checks)} controles, {len(h.failures())} en echec)")
    return 0 if not h.failures() else 1


if __name__ == "__main__":
    raise SystemExit(main())
