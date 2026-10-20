#!/usr/bin/env python3
"""Builds Riddim Forge's factory presets: Presets/forge_factory.json.

Every preset is original. Each sound family below is a recipe written from riddim and
dubstep sound-design practice (wobbles chopped by LFO 1, PD/sync warps, comb, formant and
phaser filters, smash and clip at the end, eight named macros). A seeded random generator
varies each recipe, so the set is the same on every run.

    python3 Tools/make_forge_presets.py                 # write the JSON
    python3 Tools/make_forge_presets.py --levels lab.json   # also apply measured loudness

--levels takes the analysis ForgePresetLab writes and sets each preset's output so they
all play at about the same loudness.
"""

import argparse
import json
import math
import random
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Presets" / "forge_factory.json"


# --- Names of every choice, read from the C++ so indices can't drift ----------------------

def string_list(path, function):
    text = (ROOT / path).read_text()
    match = re.search(function + r"\(\)\s*\{\s*static const (?:juce::StringArray|auto) names\s*\{(.*?)\};", text, re.S)
    if not match:
        sys.exit(f"can't find {function} in {path}")
    return re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))


def table_names():
    text = (ROOT / "Source/dsp/Wavetables.cpp").read_text()
    match = re.search(r"getTableNames\(\)\s*\{\s*static const juce::StringArray names\s*\{(.*?)\};", text, re.S)
    return re.findall(r'"([^"]*)"', match.group(1))


TABLES = table_names()
WARPS = string_list("Source/forge/ForgeEngine.cpp", r"Forge::warpNames")
WARP_SOURCES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::warpSourceNames")
SOURCES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::modSourceNames")
DESTS = string_list("Source/forge/ForgeEngine.cpp", r"Forge::modDestNames")
ROUTES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::routeNames")
LFO_SHAPES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::lfoShapeNames")
LFO_MODES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::lfoModeNames")
RATES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::lfoRateNames")
NOISES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::noiseTypeNames")
SUB_SHAPES = string_list("Source/forge/ForgeEngine.cpp", r"Forge::subShapeNames")
FILTERS = string_list("Source/forge/ForgeFilter.cpp", r"Forge::filterTypeNames")
DIST_TYPES = ["Soft Clip", "Hard Clip", "Wavefold", "Bitcrush", "Diode", "Asym", "Sine Shaper", "X-Shaper", "Tube"]
DELAY_TIMES = ["1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/1"]
EQ_SHAPES = ["Low Shelf", "Peak", "High Shelf"]


def index(names, name):
    try:
        return names.index(name)
    except ValueError:
        sys.exit(f"unknown choice {name!r}; expected one of {names}")


# --- One preset -----------------------------------------------------------------------------

class Preset:
    def __init__(self, name, category, rng):
        self.name, self.category, self.rng = name, category, rng
        self.values, self.text = {}, {}
        self.next_slot = 0
        # Defaults that differ from the plugin's, for every factory sound.
        self.set(dist_drive=0.0, squash=0.0, clip=0.15, gate=1, master_gain=-6.0)

    def set(self, **values):
        for key, value in values.items():
            self.values[key] = float(value)
        return self

    def osc(self, o, table=None, warp1=None, warp2=None, src=None, route=None, **values):
        letter = "ABC"[o]
        ids = {"on": "on", "pos": "pos", "level": "level", "pan": "pan", "oct": "oct", "semi": "semi", "fine": "fine",
               "unison": "unison", "detune": "detune", "blend": "blend", "width": "width", "rphase": "rphase",
               "w1": "warp1", "w2": "warp2"}
        self.values[f"f{letter}_on"] = 1.0
        if table is not None:
            self.values[f"f{letter}_table"] = float(index(TABLES, table))
        if warp1 is not None:
            self.values[f"f{letter}_warp1_mode"] = float(index(WARPS, warp1))
        if warp2 is not None:
            self.values[f"f{letter}_warp2_mode"] = float(index(WARPS, warp2))
        if src is not None:
            self.values[f"f{letter}_warp_src"] = float(index(WARP_SOURCES, src))
        if route is not None:
            self.values[f"f{letter}_route"] = float(index(ROUTES, route))
        for key, value in values.items():
            self.values[f"f{letter}_{ids[key]}"] = float(value)
        return self

    def osc_off(self, o):
        self.values[f"f{'ABC'[o]}_on"] = 0.0

    def filter(self, f, type, cutoff, res=0.0, drive=0.0, var=0.5, mix=1.0, key=0.0, on=True):
        p = "flt_" if f == 0 else "flt2_"
        self.set(**{p + "on": 1 if on else 0, p + "type": index(FILTERS, type), p + "cutoff": cutoff, p + "res": res,
                    p + "drive": drive, p + "var": var, p + "mix": mix, p + "keytrack": key})

    def amp(self, a=1.0, d=200.0, s=1.0, r=30.0):
        self.set(amp_attack=a, amp_decay=d, amp_sustain=s, amp_release=r)

    def env(self, e, a=1.0, d=300.0, s=0.0, r=100.0):
        n = e + 2
        self.set(**{f"env{n}_attack": a, f"env{n}_decay": d, f"env{n}_sustain": s, f"env{n}_release": r})

    def lfo(self, l, shape="Sine", rate="1/8", sync=True, hz=2.0, mode="Trigger", smooth=0.0, curve=None):
        n = l + 1
        if curve is not None:
            shape = "Custom"
            self.text[f"lfoCurve{n}"] = ";".join(f"{x:.4f},{y:.4f},{c:.3f}" for x, y, c in curve)
        self.set(**{f"lfo{n}_shape": index(LFO_SHAPES, shape), f"lfo{n}_rate": index(RATES, rate), f"lfo{n}_sync": 1 if sync else 0,
                    f"lfo{n}_hz": hz, f"lfo{n}_mode": index(LFO_MODES, mode), f"lfo{n}_smooth": smooth})

    def mod(self, source, dest, amount, bipolar=False, aux=None):
        if self.next_slot >= 16:
            return
        n = self.next_slot + 1
        self.next_slot += 1
        self.set(**{f"mod{n}_src": index(SOURCES, source), f"mod{n}_dst": index(DESTS, dest), f"mod{n}_amt": max(-1.0, min(1.0, amount)),
                    f"mod{n}_bi": 1 if bipolar else 0, f"mod{n}_aux": index(SOURCES, aux) if aux else 0})

    def macro(self, m, name, value, routes):
        self.text[f"macroName{m + 1}"] = name
        self.values[f"macro{m + 1}"] = float(value)
        for dest, amount in routes:
            self.mod(f"Macro {m + 1}", dest, amount)

    def fx(self, dist=None, drive=0.0, dist_mix=1.0, heat=0.0, smash=0.0, clip=0.2, tilt=0.0, low_keep=110.0,
           vowel_mix=0.0, vowel=0.0, comb_mix=0.0, comb_freq=220.0, comb_fb=0.6, comb_track=True):
        if dist is not None:
            self.set(dist_type=index(DIST_TYPES, dist))
        self.set(dist_drive=drive, dist_mix=dist_mix, heat=heat, squash=smash, clip=clip, tilt=tilt, low_keep=low_keep,
                 vowel_mix=vowel_mix, vowel=vowel, comb_mix=comb_mix, comb_freq=comb_freq, comb_feedback=comb_fb,
                 comb_track=1 if comb_track else 0)

    def rack(self, name, **values):
        prefix = {"hyper": "rk_hyper_", "dim": "rk_dim_", "fxf": "rk_fxf_", "eq": "rk_eq", "shift": "rk_shift_",
                  "chorus": "rk_chorus_", "phaser": "rk_phaser_", "flanger": "rk_flanger_", "delay": "rk_delay_",
                  "reverb": "rk_reverb_"}[name]
        if name in ("hyper", "fxf", "shift", "chorus", "phaser", "flanger", "delay", "reverb"):
            self.values[prefix + "on"] = 1.0
        if name == "eq":
            self.values["rk_eq_on"] = 1.0
        for key, value in values.items():
            if key == "type":
                value = index(FILTERS, value)
            elif key == "time":
                value = index(DELAY_TIMES, value)
            elif key.endswith("shape"):
                value = index(EQ_SHAPES, value)
            self.values[prefix + key] = float(value)

    def voice(self, mono=True, legato=False, glide=0.0, poly=8, bend=12, pitch_env=0.0, pitch_decay=30.0):
        self.set(mono=1 if mono else 0, legato=1 if legato else 0, glide=glide, polyphony=poly, bend_range=bend,
                 pitch_env=pitch_env, pitch_decay=pitch_decay)

    def hits(self, rates, length=None):
        self.set(hitseq_on=1, hitseq_len=length or len(rates))
        for i, rate in enumerate(rates[:8]):
            self.values[f"hitseq_rate{i + 1}"] = float(index(RATES, rate))

    def to_json(self):
        return {"name": self.name, "category": self.category,
                "values": {k: round(v, 5) for k, v in sorted(self.values.items())}, "text": dict(sorted(self.text.items()))}


# --- Shared building blocks --------------------------------------------------------------------

def jitter(rng, value, amount, low=0.0, high=1.0):
    return max(low, min(high, value + rng.uniform(-amount, amount)))


def pick(rng, items):
    return items[rng.randrange(len(items))]


# Drawn LFO shapes (x, y, curve). y = 1 is the top; volume chops use them inverted.
def gate_curve(rng, steps):
    """Hard on/off pattern across one cycle, `steps` cells."""
    pattern = [rng.random() < 0.7 for _ in range(steps)]
    pattern[0] = True
    points = []
    for i, on in enumerate(pattern):
        x0, x1 = i / steps, (i + 1) / steps
        y = 1.0 if on else 0.0
        points += [(x0 + (0.002 if i else 0.0), y, 0.0), (x1 - 0.004, y, 0.0)]
    points[0] = (0.0, points[0][1], 0.0)
    points.append((1.0, points[0][1], 0.0))
    return points


def wobble_curve(rng):
    """A riddim wobble: fast rise, bent fall, sometimes a notch."""
    peak = rng.uniform(0.15, 0.45)
    bend = rng.uniform(-0.8, 0.6)
    points = [(0.0, 0.0, rng.uniform(-0.5, 0.5)), (peak, 1.0, bend)]
    if rng.random() < 0.5:
        notch = rng.uniform(peak + 0.1, 0.85)
        points += [(notch, rng.uniform(0.1, 0.5), 0.0), (min(0.97, notch + 0.08), rng.uniform(0.5, 0.9), rng.uniform(-0.6, 0.6))]
    points.append((1.0, 0.0, 0.0))
    return points


def stepped_curve(rng, steps):
    """Stepped levels, like a pitch or position sequence."""
    points = []
    for i in range(steps):
        y = round(rng.uniform(0.0, 1.0) * 4) / 4
        points += [(i / steps + (0.001 if i else 0.0), y, 0.0), ((i + 1) / steps - 0.003, y, 0.0)]
    points.append((1.0, points[0][1], 0.0))
    return points


def ramp_curve(rng, up=True):
    bend = rng.uniform(-0.7, 0.7)
    return [(0.0, 0.0 if up else 1.0, bend), (0.97, 1.0 if up else 0.0, 0.0), (1.0, 0.0 if up else 1.0, 0.0)]


# Every recipe takes (rng, preset) and fills it in. Names come from word lists per family.
WOBBLE_TABLES = ["Riddim Sync Saw", "Growl FM", "Wet Growl", "Dirty Saw", "Analog Saw", "Harmonic Saws", "Saw To Square",
                 "Phase Dist", "Resonant Sweep", "Chirp Growl", "Pulse Sync"]
GROWL_TABLES = ["Growl FM", "Wet Growl", "Chirp Growl", "FM 2:1", "FM 3:2", "FM 7:1", "Robot Buzz", "Tearout Fold", "Vowel A-O-U"]
TEAR_TABLES = ["Tearout Fold", "Tearout Screech", "Metal Screech", "Hard Fold", "Digital Crush", "FM 7:1", "Dirty Saw", "Square Sync"]
YOI_TABLES = ["Yoi Formant", "Vowel A-O-U", "Vowel I-E-A", "Wet Growl", "Sine Harmonics", "Drawbars"]
SCREECH_TABLES = ["Metal Screech", "Tearout Screech", "Pulse Sync", "Tri Sync", "Square Sync", "Spectral Comb", "FM 7:1"]
METAL_TABLES = ["Spectral Comb", "Metal Screech", "Robot Buzz", "Digital Crush", "FM 7:1", "Noise Cycle", "Hard Fold"]
REESE_TABLES = ["Reese Stack", "Analog Saw", "Harmonic Saws", "Dirty Saw", "Saw To Square", "Analog Square"]
SUB_TABLES = ["Warm Sub", "Basic Shapes", "Sine Harmonics", "Analog Square"]
LEAD_TABLES = ["Analog Saw", "Pulse Width", "Saw To Square", "Harmonic Saws", "Drawbars", "Tri Sync", "Basic Shapes"]
PAD_TABLES = ["Drawbars", "Sine Harmonics", "Harmonic Saws", "Vowel I-E-A", "Analog Saw", "Pulse Width", "Resonant Sweep"]

RIDDIM_RATES = ["1/8", "1/8T", "1/16", "1/16T", "1/4T", "1/8.", "1/32"]
GRIT_WARPS = ["PD (src)", "Sync", "Sync Window", "Bend +", "Bend +/-", "Asym +/-", "Mirror", "FM (src)", "FM (sub)", "Quantize", "RM (src)"]


def riddim_core(rng, p, tables, rate=None, chop=True):
    """The shared riddim skeleton: osc A low and gritty, LFO 1 moving it, a chop on volume."""
    rate = rate or pick(rng, RIDDIM_RATES)
    table = pick(rng, tables)
    w1 = pick(rng, GRIT_WARPS)
    w2 = pick(rng, ["Off", "Off", "Mirror", "Bend +", "Asym +", "Quantize", "Flip", "PWM"])
    p.osc(0, table=table, warp1=w1, warp2=w2, src=pick(rng, ["Osc B", "Osc B", "Sub", "Osc C"]), route="Filter 1",
          pos=rng.uniform(0.0, 0.5), level=0.8, oct=pick(rng, [0, 0, 0, -1]), unison=pick(rng, [1, 1, 1, 2, 3]), detune=rng.uniform(0.05, 0.2),
          w1=rng.uniform(0.1, 0.5), w2=rng.uniform(0.0, 0.4))
    p.voice(mono=True, glide=pick(rng, [0, 0, 5, 12, 25]))
    p.amp(a=1.0, d=200, s=1.0, r=rng.uniform(10, 40))
    p.set(sub_on=1, sub_level=rng.uniform(0.2, 0.4), sub_oct=0, sub_route=index(ROUTES, "Direct"))

    if rng.random() < 0.45:
        p.lfo(0, rate=rate, curve=wobble_curve(rng), smooth=rng.uniform(0.0, 0.15))
    else:
        p.lfo(0, shape=pick(rng, ["Sine", "Triangle", "Saw Down", "Riddim Step", "Half Wob", "Dip", "Exp Down"]), rate=rate,
              smooth=rng.uniform(0.0, 0.1))
    p.mod("LFO 1", "A Warp 1", rng.uniform(0.3, 0.6))
    if rng.random() < 0.6:
        p.mod("LFO 1", "A Position", rng.uniform(0.2, 0.6))

    if chop:
        steps = pick(rng, [4, 6, 8])
        p.lfo(1, rate=pick(rng, ["1/4", "1/2", "1/4T"]), curve=gate_curve(rng, steps))
        # Volume = 1 + amount * (1 - gate): fully off between hits.
        p.mod("LFO 2", "Volume", -1.0, aux=None)
        p.text["lfoCurve2"] = ";".join(
            f"{x:.4f},{1.0 - y:.4f},{c:.3f}" for x, y, c in (tuple(map(float, part.split(","))) for part in p.text["lfoCurve2"].split(";")))
    return table


def standard_macros(rng, p, extra=None):
    """Eight named macros, the way riddim packs lay them out."""
    p.macro(0, "Grit", rng.uniform(0.0, 0.4), [("Dist Drive", 0.5), ("A Warp 2", 0.35)])
    p.macro(1, "Movement", rng.uniform(0.2, 0.6), [("A Position", 0.45)])
    p.macro(2, "Vowel", rng.uniform(0.0, 0.5), [("FX Vowel", 0.8)])
    p.macro(3, "Comb", 0.0, [("Comb Mix", 0.7)])
    p.macro(4, "Smash", rng.uniform(0.2, 0.5), [("Smash", 0.6)])
    p.macro(5, "Width", rng.uniform(0.0, 0.3), [("Hyper Mix", 0.6)])
    p.macro(6, "Sub", 0.5, [("Sub Level", 0.6)])
    p.macro(7, extra or "Space", 0.0, [("Reverb Mix", 0.5)] if extra in (None, "Space") else [])


def finishing_fx(rng, p, heavy=True):
    if heavy:
        p.fx(dist=pick(rng, DIST_TYPES), drive=rng.uniform(0.35, 0.8), dist_mix=rng.uniform(0.7, 1.0), heat=rng.uniform(0.0, 0.4),
             smash=rng.uniform(0.35, 0.75), clip=rng.uniform(0.2, 0.5), tilt=rng.uniform(-0.2, 0.3), low_keep=rng.uniform(90, 140))
    else:
        p.fx(dist=pick(rng, ["Soft Clip", "Tube", "Sine Shaper"]), drive=rng.uniform(0.1, 0.35), smash=rng.uniform(0.1, 0.3), clip=0.15)
    p.rack("hyper", mix=rng.uniform(0.0, 0.35), detune=rng.uniform(0.15, 0.4), unison=pick(rng, [3, 4, 5]))
    p.rack("reverb", size=rng.uniform(0.2, 0.5), mix=rng.uniform(0.0, 0.08))
    p.set(rk_reverb_on=1.0 if rng.random() < 0.5 else 0.0)
    p.rack("delay", time=pick(rng, ["1/8", "1/8.", "1/16"]), mix=0.0, feedback=0.3)
    p.set(rk_delay_on=0.0)


# --- Sound families ----------------------------------------------------------------------------

def wobble(rng, p):
    riddim_core(rng, p, WOBBLE_TABLES, chop=rng.random() < 0.5)
    p.filter(0, pick(rng, ["Low Pass 24", "Low Pass 12", "Ladder (Acid)", "Band Pass 12"]), cutoff=rng.uniform(300, 1500),
             res=rng.uniform(0.2, 0.6), drive=rng.uniform(0.1, 0.5))
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.35, 0.65))
    if rng.random() < 0.5:
        p.filter(1, pick(rng, ["Phaser +", "Comb +", "Formant"]), cutoff=rng.uniform(200, 1200), res=rng.uniform(0.2, 0.6), mix=rng.uniform(0.3, 0.7))
        p.osc(0, route="Both")
    finishing_fx(rng, p)
    standard_macros(rng, p, "Wob Speed")
    p.mod("Macro 8", "LFO 1 Rate", 0.5)


def riddim_chop(rng, p):
    rate = pick(rng, ["1/8T", "1/16", "1/16T"])
    riddim_core(rng, p, WOBBLE_TABLES + GROWL_TABLES, rate=rate, chop=True)
    p.hits([pick(rng, RIDDIM_RATES) for _ in range(8)], length=pick(rng, [2, 3, 4, 6, 8]))
    p.filter(0, pick(rng, ["Comb +", "Comb -", "Phaser +", "Formant", "Low Pass 24"]), cutoff=rng.uniform(150, 900),
             res=rng.uniform(0.3, 0.7), var=rng.uniform(0.2, 0.8), mix=rng.uniform(0.5, 1.0))
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.2, 0.5))
    finishing_fx(rng, p)
    p.fx(dist=pick(rng, ["Hard Clip", "Wavefold", "X-Shaper", "Diode"]), drive=rng.uniform(0.5, 0.85), smash=rng.uniform(0.5, 0.8),
         clip=rng.uniform(0.3, 0.6), comb_mix=rng.uniform(0.0, 0.3))
    standard_macros(rng, p, "Chop")


def growl(rng, p):
    riddim_core(rng, p, GROWL_TABLES, chop=rng.random() < 0.3)
    p.osc(1, table=pick(rng, GROWL_TABLES + ["Basic Shapes", "Sine Harmonics"]), warp1=pick(rng, ["FM (src)", "Sync", "Off"]),
          src="Osc A", route="Filter 1", oct=pick(rng, [0, 1]), semi=pick(rng, [0, 0, 7, 12]), level=rng.uniform(0.4, 0.7),
          pos=rng.uniform(0, 1), w1=rng.uniform(0.1, 0.4))
    p.osc(0, warp1="FM (src)", src="Osc B")
    p.filter(0, "Formant", cutoff=rng.uniform(400, 1400), res=rng.uniform(0.3, 0.6), var=rng.uniform(0.0, 1.0), mix=rng.uniform(0.5, 0.8))
    p.mod("LFO 1", "F1 Var", rng.uniform(0.4, 0.8))
    p.fx(vowel_mix=rng.uniform(0.2, 0.6))
    finishing_fx(rng, p)
    p.fx(vowel_mix=rng.uniform(0.2, 0.6))
    p.mod("LFO 1", "FX Vowel", rng.uniform(0.4, 0.9))
    standard_macros(rng, p, "Mouth")


def tearout(rng, p):
    riddim_core(rng, p, TEAR_TABLES, rate=pick(rng, ["1/16", "1/16T", "1/32", "1/8T"]), chop=rng.random() < 0.6)
    p.osc(1, table=pick(rng, TEAR_TABLES), warp1=pick(rng, GRIT_WARPS), oct=pick(rng, [-1, 0]), semi=pick(rng, [0, 12, 19]),
          level=rng.uniform(0.4, 0.7), unison=pick(rng, [1, 2]), w1=rng.uniform(0.2, 0.7), route="Filter 1")
    p.filter(0, pick(rng, ["Comb -", "Flanger +", "Phaser -", "Ring Mod", "Ladder (Acid)"]), cutoff=rng.uniform(150, 2500),
             res=rng.uniform(0.4, 0.8), drive=rng.uniform(0.3, 0.8), var=rng.uniform(0.0, 1.0), mix=rng.uniform(0.6, 1.0))
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.3, 0.7), bipolar=True)
    finishing_fx(rng, p)
    p.fx(dist=pick(rng, ["Hard Clip", "Wavefold", "Bitcrush", "X-Shaper", "Diode"]), drive=rng.uniform(0.6, 0.95),
         smash=rng.uniform(0.6, 0.9), clip=rng.uniform(0.4, 0.7), heat=rng.uniform(0.2, 0.6))
    p.rack("shift", amount=rng.uniform(-0.3, 0.3), feedback=rng.uniform(0.1, 0.4), mix=rng.uniform(0.0, 0.3))
    standard_macros(rng, p, "Tear")
    p.mod("Macro 8", "Shift Amount", 0.4)


def yoi(rng, p):
    riddim_core(rng, p, YOI_TABLES, rate=pick(rng, ["1/8", "1/8T", "1/4T", "1/16"]), chop=rng.random() < 0.4)
    p.filter(0, "Formant", cutoff=rng.uniform(500, 1500), res=rng.uniform(0.4, 0.8), var=0.0, mix=rng.uniform(0.6, 0.85))
    p.mod("LFO 1", "F1 Var", rng.uniform(0.7, 1.0))
    p.lfo(2, rate=pick(rng, ["1/4", "1/2"]), curve=ramp_curve(rng))
    p.mod("LFO 3", "A Position", rng.uniform(0.3, 0.6))
    finishing_fx(rng, p)
    p.fx(vowel_mix=rng.uniform(0.4, 0.8), vowel=rng.uniform(0.0, 0.3))
    p.mod("LFO 1", "FX Vowel", rng.uniform(0.5, 1.0))
    p.set(sub_level=rng.uniform(0.08, 0.2), flt_drive=rng.uniform(0.3, 0.6))
    standard_macros(rng, p, "Yoi")


def screech(rng, p):
    riddim_core(rng, p, SCREECH_TABLES, rate=pick(rng, ["1/16", "1/8T", "1/8"]), chop=rng.random() < 0.4)
    p.osc(0, oct=pick(rng, [1, 1, 0]), warp1=pick(rng, ["Sync", "Sync Window", "PD (src)"]), w1=rng.uniform(0.4, 0.8))
    p.filter(0, pick(rng, ["High Pass 12", "Band Pass 24", "Peak", "Comb +"]), cutoff=rng.uniform(600, 3500), res=rng.uniform(0.4, 0.8))
    p.lfo(2, rate=pick(rng, ["1/2", "1/1"]), curve=stepped_curve(rng, pick(rng, [4, 8])))
    p.mod("LFO 3", "A Pitch", pick(rng, [0.5, 0.25, 0.125]) * 0.5)
    finishing_fx(rng, p)
    p.rack("shift", amount=rng.uniform(0.05, 0.5), feedback=rng.uniform(0.2, 0.6), mix=rng.uniform(0.2, 0.6))
    p.set(sub_level=rng.uniform(0.2, 0.4))
    standard_macros(rng, p, "Scream")
    p.mod("Macro 8", "Shift Amount", 0.5)


def metallic(rng, p):
    riddim_core(rng, p, METAL_TABLES, chop=rng.random() < 0.5)
    p.filter(0, pick(rng, ["Comb +", "Comb -", "Flanger -", "Ring Mod"]), cutoff=rng.uniform(80, 900), res=rng.uniform(0.6, 0.9),
             var=rng.uniform(0.0, 0.6), mix=rng.uniform(0.45, 0.8))
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.2, 0.5))
    finishing_fx(rng, p)
    p.fx(comb_mix=rng.uniform(0.2, 0.6), comb_fb=rng.uniform(0.5, 0.85))
    p.rack("flanger", rate=rng.uniform(0.05, 0.3), depth=rng.uniform(0.3, 0.8), feedback=rng.uniform(0.3, 0.7), mix=rng.uniform(0.2, 0.5))
    p.set(sub_level=rng.uniform(0.08, 0.2), flt_drive=rng.uniform(0.3, 0.6))
    standard_macros(rng, p, "Metal")


def robot(rng, p):
    riddim_core(rng, p, ["Robot Buzz", "Digital Crush", "Square Sync", "Pulse Sync", "FM 3:2"], chop=True)
    p.osc(0, warp2="Quantize", w2=rng.uniform(0.3, 0.8))
    p.filter(0, pick(rng, ["Sample & Hold", "Band Pass 12", "Formant", "Ring Mod"]), cutoff=rng.uniform(200, 4000), res=rng.uniform(0.2, 0.6), mix=rng.uniform(0.5, 0.85))
    p.lfo(2, rate=pick(rng, ["1/16", "1/8"]), shape="Square")
    p.mod("LFO 3", "F1 Cutoff", rng.uniform(0.2, 0.5))
    finishing_fx(rng, p)
    p.fx(dist="Bitcrush", drive=rng.uniform(0.3, 0.7))
    p.set(sub_level=rng.uniform(0.08, 0.2), flt_drive=rng.uniform(0.3, 0.6))
    standard_macros(rng, p, "Bits")


def reese(rng, p):
    table = pick(rng, REESE_TABLES)
    p.osc(0, table=table, oct=0, unison=pick(rng, [4, 5, 7, 8]), detune=rng.uniform(0.15, 0.35), blend=0.65, width=0.9, level=0.75,
          pos=rng.uniform(0, 0.6), warp1=pick(rng, ["Off", "Bend +", "Asym +/-"]), w1=rng.uniform(0, 0.3))
    p.osc(1, table=pick(rng, REESE_TABLES), oct=0, fine=rng.uniform(-14, 14), unison=pick(rng, [2, 3, 4]), detune=rng.uniform(0.1, 0.3),
          level=0.55, route="Filter 1")
    p.filter(0, pick(rng, ["Low Pass 24", "Ladder (Acid)", "Low Pass 12"]), cutoff=rng.uniform(250, 1400), res=rng.uniform(0.1, 0.45),
             drive=rng.uniform(0.2, 0.5))
    p.lfo(0, shape="Sine", rate=pick(rng, ["1/1", "2/1", "1/2"]), mode="Free")
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.1, 0.3), bipolar=True)
    p.voice(mono=True, glide=rng.uniform(20, 80), legato=True)
    p.amp(a=3, d=300, s=1.0, r=rng.uniform(60, 200))
    p.set(sub_on=1, sub_level=rng.uniform(0.3, 0.5), sub_oct=0)
    finishing_fx(rng, p, heavy=rng.random() < 0.5)
    p.rack("phaser", rate=rng.uniform(0.05, 0.2), depth=0.6, freq=rng.uniform(300, 1200), feedback=0.4, mix=rng.uniform(0.0, 0.4))
    p.set(rk_phaser_on=1.0 if rng.random() < 0.5 else 0.0)
    standard_macros(rng, p, "Space")


def sub_bass(rng, p):
    p.osc(0, table=pick(rng, SUB_TABLES), oct=0, pos=rng.uniform(0.0, 0.4), level=0.8)
    p.set(sub_on=1, sub_level=rng.uniform(0.5, 0.8), sub_shape=index(SUB_SHAPES, pick(rng, ["Sine", "Sine", "Triangle"])), sub_oct=0)
    p.filter(0, "Low Pass 24", cutoff=rng.uniform(200, 900), res=rng.uniform(0.0, 0.3))
    p.voice(mono=True, glide=rng.uniform(0, 60), pitch_env=rng.choice([0, 0, 12, 24]), pitch_decay=rng.uniform(15, 60))
    p.amp(a=1, d=rng.uniform(150, 900), s=rng.choice([1.0, 0.0, 0.6]), r=rng.uniform(40, 200))
    p.fx(dist=pick(rng, ["Soft Clip", "Tube", "Sine Shaper"]), drive=rng.uniform(0.1, 0.5), smash=rng.uniform(0.0, 0.3), clip=0.2)
    p.macro(0, "Drive", 0.2, [("Dist Drive", 0.6)])
    p.macro(1, "Tone", 0.3, [("F1 Cutoff", 0.4)])
    p.macro(2, "Punch", 0.0, [("Pitch Env", 0.3)])
    p.macro(3, "Body", 0.5, [("Sub Level", 0.4), ("A Position", 0.4)])
    p.macro(4, "Smash", 0.0, [("Smash", 0.6)])
    p.macro(5, "Glide", 0.0, [])
    p.macro(6, "Sub", 0.5, [("Sub Level", 0.5)])
    p.macro(7, "Space", 0.0, [("Reverb Mix", 0.4)])


def lead(rng, p):
    p.osc(0, table=pick(rng, LEAD_TABLES), oct=0, unison=pick(rng, [3, 5, 7]), detune=rng.uniform(0.1, 0.3), blend=0.6, level=0.7,
          pos=rng.uniform(0, 0.8), warp1=pick(rng, ["Off", "Sync", "Bend +", "PWM"]), w1=rng.uniform(0, 0.4))
    p.osc(1, table=pick(rng, LEAD_TABLES), oct=pick(rng, [0, 1]), semi=pick(rng, [0, 7, 12]), level=0.45, route="Filter 1")
    p.filter(0, pick(rng, ["Low Pass 12", "Low Pass 24", "Band Pass 12"]), cutoff=rng.uniform(1500, 8000), res=rng.uniform(0.1, 0.4))
    p.env(0, a=1, d=rng.uniform(150, 600), s=0.2, r=200)
    p.mod("Env 2", "F1 Cutoff", rng.uniform(0.2, 0.5))
    p.lfo(0, shape="Sine", rate="1/8", sync=False, hz=rng.uniform(4.5, 6.0), mode="Free")
    p.mod("LFO 1", "A Pitch", 0.01, bipolar=True, aux="Mod Wheel")
    p.voice(mono=rng.random() < 0.5, glide=rng.uniform(0, 60), poly=8)
    p.amp(a=rng.uniform(1, 20), d=400, s=0.8, r=rng.uniform(100, 400))
    finishing_fx(rng, p, heavy=False)
    p.rack("delay", time=pick(rng, ["1/8", "1/8.", "1/4"]), mix=rng.uniform(0.1, 0.25), feedback=rng.uniform(0.25, 0.5))
    p.rack("reverb", size=rng.uniform(0.4, 0.7), mix=rng.uniform(0.1, 0.25))
    p.set(sub_on=0)
    standard_macros(rng, p, "Space")


def pad(rng, p):
    for o in range(2):
        p.osc(o, table=pick(rng, PAD_TABLES), oct=pick(rng, [-1, 0, 0, 1]), unison=pick(rng, [4, 6, 8]), detune=rng.uniform(0.15, 0.35),
              blend=0.7, width=1.0, level=0.55, pos=rng.uniform(0, 1), route="Filter 1")
    p.filter(0, pick(rng, ["Low Pass 12", "Low Pass 24", "Phaser +"]), cutoff=rng.uniform(800, 4000), res=rng.uniform(0.0, 0.3))
    p.lfo(0, shape=pick(rng, ["Sine", "Triangle"]), rate=pick(rng, ["2/1", "4/1", "1/1"]), mode="Free")
    p.mod("LFO 1", "A Position", rng.uniform(0.2, 0.5))
    p.mod("LFO 1", "F1 Cutoff", rng.uniform(0.05, 0.2), bipolar=True)
    p.voice(mono=False, poly=8)
    p.amp(a=rng.uniform(300, 1500), d=1000, s=0.8, r=rng.uniform(800, 2500))
    p.set(sub_on=0)
    p.fx(dist="Soft Clip", drive=0.05, smash=0.1, clip=0.05)
    p.set(gate=0)
    p.rack("chorus", rate=0.2, depth=0.4, mix=rng.uniform(0.2, 0.5))
    p.rack("reverb", size=rng.uniform(0.6, 0.95), mix=rng.uniform(0.3, 0.5), damp=0.4)
    p.rack("delay", time=pick(rng, ["1/4", "1/4.", "1/8."]), mix=rng.uniform(0.1, 0.3), feedback=0.4)
    standard_macros(rng, p, "Space")


def pluck(rng, p):
    p.osc(0, table=pick(rng, LEAD_TABLES + ["FM 2:1", "FM 3:2"]), oct=0, level=0.7, pos=rng.uniform(0, 1), unison=pick(rng, [1, 3]),
          detune=0.12)
    p.filter(0, pick(rng, ["Low Pass 24", "Low Pass 12"]), cutoff=rng.uniform(400, 1200), res=rng.uniform(0.1, 0.5))
    p.env(0, a=0.5, d=rng.uniform(80, 300), s=0.0, r=100)
    p.mod("Env 2", "F1 Cutoff", rng.uniform(0.4, 0.7))
    p.voice(mono=False, poly=8)
    p.amp(a=0.5, d=rng.uniform(150, 500), s=0.0, r=rng.uniform(100, 300))
    p.set(sub_on=0)
    finishing_fx(rng, p, heavy=False)
    p.rack("delay", time=pick(rng, ["1/8", "1/8.", "1/16"]), mix=rng.uniform(0.15, 0.35), feedback=rng.uniform(0.3, 0.55))
    p.rack("reverb", size=rng.uniform(0.4, 0.8), mix=rng.uniform(0.1, 0.3))
    standard_macros(rng, p, "Space")


def fx_sound(rng, p):
    kind = pick(rng, ["riser", "downer", "noise", "impact"])
    p.osc(0, table=pick(rng, TABLES[:20]), oct=0, level=0.6, unison=pick(rng, [1, 4]), detune=0.3)
    p.set(noise_level=rng.uniform(0.2, 0.6), noise_type=index(NOISES, pick(rng, NOISES)))
    p.filter(0, pick(rng, ["Low Pass 12", "Band Pass 12", "Phaser +"]), cutoff=rng.uniform(300, 1500), res=rng.uniform(0.2, 0.6))
    if kind in ("riser", "downer"):
        p.lfo(0, rate=pick(rng, ["4/1", "8/1"]), mode="Envelope", curve=ramp_curve(rng, up=kind == "riser"))
        p.mod("LFO 1", "Master Pitch", 0.5)
        p.mod("LFO 1", "F1 Cutoff", 0.35)
        p.amp(a=10, d=1000, s=1.0, r=1500)
    else:
        p.voice(pitch_env=rng.uniform(-36, 36), pitch_decay=rng.uniform(100, 400))
        p.amp(a=1, d=rng.uniform(300, 1200), s=0.0, r=800)
    p.voice(mono=True, pitch_env=p.values.get("pitch_env", 0.0), pitch_decay=p.values.get("pitch_decay", 30.0))
    p.set(sub_on=0)
    finishing_fx(rng, p)
    p.rack("reverb", size=rng.uniform(0.6, 0.95), mix=rng.uniform(0.2, 0.45))
    p.set(gate=0)
    standard_macros(rng, p, "Space")


FAMILIES = [
    # (category, recipe, count, name words)
    ("Wobble", wobble, 48, ["Wob", "Wub", "Wobble", "Swing", "Lurch", "Sway", "Bounce"]),
    ("Riddim", riddim_chop, 48, ["Riddim", "Chop", "Stomp", "Hit", "Stutter", "Step", "Grid"]),
    ("Growl", growl, 40, ["Growl", "Snarl", "Throat", "Gurgle", "Grunt", "Roar", "Beast"]),
    ("Tearout", tearout, 36, ["Tear", "Rip", "Shred", "Saw", "Grind", "Mangle", "Wreck"]),
    ("Yoi", yoi, 28, ["Yoi", "Talk", "Mouth", "Vowel", "Speak", "Yap", "Chatter"]),
    ("Screech", screech, 24, ["Screech", "Scream", "Siren", "Squeal", "Laser", "Shriek", "Whine"]),
    ("Metal", metallic, 22, ["Metal", "Clang", "Pipe", "Gear", "Foundry", "Anvil", "Rattle"]),
    ("Robot", robot, 16, ["Robot", "Droid", "Circuit", "Glitch", "Byte", "Servo", "Modem"]),
    ("Reese", reese, 20, ["Reese", "Drift", "Smear", "Rumble", "Haze", "Undertow", "Fog"]),
    ("Sub", sub_bass, 18, ["Sub", "Low", "Deep", "Weight", "Floor", "Thump", "Belly"]),
    ("Lead", lead, 14, ["Lead", "Line", "Hook", "Blade", "Signal", "Beam", "Flare"]),
    ("Pad", pad, 10, ["Pad", "Mist", "Cloud", "Drift", "Aura", "Veil", "Glow"]),
    ("Pluck", pluck, 10, ["Pluck", "Pick", "Ping", "Tick", "Drop", "Flick", "Snap"]),
    ("FX", fx_sound, 12, ["Rise", "Fall", "Sweep", "Blast", "Impact", "Whoosh", "Crash"]),
]

ADJECTIVES = ["Rusty", "Toxic", "Heavy", "Wonky", "Broken", "Filthy", "Hollow", "Grim", "Molten", "Feral", "Crooked", "Static",
              "Gnarly", "Vile", "Sunken", "Rabid", "Murky", "Twisted", "Sour", "Brutal", "Cold", "Acid", "Bent", "Dusty", "Electric",
              "Fat", "Greasy", "Hungry", "Jagged", "Lazy", "Mutant", "Nasty", "Oily", "Pressure", "Rotten", "Swamp", "Thick", "Ugly",
              "Wet", "Zombie", "Night", "Neon", "Iron", "Chrome", "Plasma", "Gutter", "Vapour", "Lunar", "Quake", "Venom"]


def build(levels=None):
    rng = random.Random(20261019)
    presets = []

    init = Preset("Init", "Init", rng)
    init.set(dist_drive=0.0, squash=0.0, clip=0.0)
    presets.append(init)

    used = {"Init"}
    for category, recipe, count, words in FAMILIES:
        for i in range(count):
            for _ in range(100):
                name = f"{pick(rng, ADJECTIVES)} {pick(rng, words)}"
                if name not in used:
                    break
            used.add(name)
            preset = Preset(name, category, random.Random(f"{category}-{i}"))
            recipe(preset.rng, preset)
            presets.append(preset)

    if levels:
        apply_levels(presets, levels)
    return presets


def build_training(count, seed):
    """Extra presets from the same recipes with other seeds, as Magic Sound training data."""
    rng = random.Random(seed)
    weights = [family[2] for family in FAMILIES]
    presets = []
    for i in range(count):
        category, recipe, _, words = rng.choices(FAMILIES, weights)[0]
        preset = Preset(f"train {i}", category, random.Random(f"train-{seed}-{i}"))
        recipe(preset.rng, preset)
        presets.append(preset)
    return presets


def apply_levels(presets, path):
    """Sets each preset's output so it plays near the target loudness."""
    analysis = {item["name"]: item for item in json.loads(Path(path).read_text())}
    for preset in presets:
        item = analysis.get(preset.name)
        if not item or preset.name == "Init" or item.get("rmsDb", -100) < -60:
            continue
        target = -15.0 if preset.category not in ("Pad", "Pluck", "FX", "Lead") else -18.0
        gain = preset.values.get("master_gain", -6.0) + (target - item["rmsDb"])
        preset.values["master_gain"] = max(-24.0, min(6.0, gain))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--levels", help="ForgePresetLab analysis JSON to level-match the presets")
    parser.add_argument("--out", default=str(OUT))
    parser.add_argument("--training", type=int, help="write this many training presets instead of the factory set")
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()

    presets = build_training(args.training, args.seed) if args.training else build(args.levels)
    Path(args.out).write_text(json.dumps([p.to_json() for p in presets], indent=1) + "\n")
    counts = {}
    for p in presets:
        counts[p.category] = counts.get(p.category, 0) + 1
    print(f"wrote {len(presets)} presets to {args.out}: " + ", ".join(f"{k} {v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
