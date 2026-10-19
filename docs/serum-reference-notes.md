# Serum dubstep / riddim preset study

Study notes from William's own Serum preset packs, used only to learn how these sounds are
built. No preset files, wavetables or exact parameter sets were copied into this project, and
none will ship as factory presets. Riddim Forge's presets are written from scratch from the
general patterns below. Written 2026-10-19.

## What was read

| Pack | Format | Presets | Readable |
| --- | --- | --- | --- |
| Aweminus (Avant Samples) | Serum 1 `.fxp` | 43 (basses, reeses, drums, FX) | names, wavetable names, macro names only |
| Calcium | Serum 1 `.fxp` | 75 (drop basses, leads, subs) | same |
| MOONBOY Nuclear Dubstep | Serum 1 `.fxp` | 30 basses | same |
| Samplifire Riddim (Avant) | Serum 1 `.fxp` | 36 (basses, FX, synths) | same, plus the designers' notes |
| Test Press Serum 2 Tearout Dubstep | Serum 2 `.SerumPreset` | 50 (38 bass/wub, 8 lead, 2 FX, 2 sub) | **full parameter set, named** |

- **Serum 2** presets are a JSON header followed by a zstd-compressed CBOR document, both
  standard, self-describing formats where every value carries its own name
  (`VoiceFilter0.kParamType = "HP12"`, `ModSlot3.destModuleParamName = "kParamTablePos"`).
  So all 50 could be read directly, with no reverse engineering.
- **Serum 1** `.fxp` presets are zlib-compressed blocks of unnamed numbers. Matching those numbers
  to knobs would mean reverse-engineering Xfer's layout, so I didn't do it. Only the plain text
  inside was read: wavetable file names, macro names and the designers' notes.
  67 of the 184 are large files, which means they embed custom wavetables or samples.
- Some numbers in the patterns below are **inferred**. Mod-source numbers in the Serum 2 files are
  plain indices. Source 6 is almost certainly LFO 1 (it drives the rhythmic volume, filter and
  warp moves), and sources 25 to 32 match the 8 macros, because each one's targets fit the
  macro's name. LFO rates are stored in an internal unit, so only relative speeds are reported.

## The patterns (Serum 2 tearout pack, 50 presets)

**Oscillators**
- 4 or 5 sources in every preset: usually 3 wavetable oscillators, plus sub and noise.
  Wavetable oscillators sit at **octave -2** in 138 of 197 uses. Only 7 of 50 presets go up to
  ±1 octave or more.
- The tables are mostly plain ones: Serum 2's "Default Shapes" (39), "Analog BD Sin" (10) and
  "Basic Shapes". The character comes from **warp, filters and FX**, not from exotic tables.
- **Warp modes:** phase distortion from another oscillator (`PD OSC`) leads with 47 uses. Then come
  **Sync** (33), **Bend +/-** (20), **PWM** (7), and a little FM-from-osc, Asym, Mirror and DLM.
  Second warp slots are rare (4).
- Unison is mostly 1 or 2. Wide stacks (16 voices) are for reeses and leads. Detune is small
  (median 0.04).
- The noise oscillator often plays a recorded texture ("AC hum" is in 128 of the Serum 1 presets
  and 5 of the Serum 2 ones) mixed in quietly for grit.

**Filters**
- Both voice filters are used (91 active across the 50). They are rarely a plain low pass. The
  types are **phaser** (Phase24, 16), **comb** (CombP/N, 8), **HN12** (10), **HP12** (6), Wsp (6),
  **formant** (4), flanger, notch, reverb and diffuser filters.
- Filter drive median 8 and resonance median 21, both moderate. The filter is used for colour,
  not screaming resonance.

**Modulation**
- Mono in 46 of 50, legato mostly off (each hit retriggers), short portamento.
- Amp envelope: near-instant attack (median 1 ms) and a short release (median 15 ms).
- **LFO 1 is the riddim engine.** It carries 422 mod routings. Its top targets: **oscillator
  volume** (119, the volume chop that turns a held note into rhythmic hits), filter cutoff (58),
  **warp amount** (44), wavetable position (43), FX filter cutoff (28) and EQ band frequencies (32).
- LFO shapes are **custom drawn** (75 of 107 used LFOs), with 2 to 5 points. In 44 of 50 the
  LFO 1 curve starts and ends at the top: a hit that dips and comes back. Trigger mode is the
  default, then Envelope (one-shot, 15) and Free (11). 11 use triplet timing.
- **Macro use is heavy:** 8 named macros in nearly every preset. The most common names are
  RATE (33, into LFO rate), SUB (29), BODE (26, frequency shifter wet), OSC C, DRIVE, FILTER,
  TIGHTEN, PHASERS, HYPER, LPF, DETUNE, COMB and PUNCH.
  A common trick: an FX unit sits at **wet 0** and a macro brings it in.
- Up to 24 mod slots in one preset, with typically 12 to 20 in use.

**FX chain** (one main rack, 5 to 13 units)
- The typical order is **Hyper/Dimension > Distortion > Filter / EQ > Multiband comp > Bode >
  Filter > Reverb**.
- Usage by unit: multiband compressor 65 (OTT style; ratio at maximum in half), FX filter 63,
  distortion 56 (drive median 73; diode, hard clip, soft clip, zero-square, asym, sine fold),
  Hyper/Dimension 45 (unison 4), **Bode frequency shifter 36** (feedback ~22, shift
  modulated), EQ 36, reverb 21 (small, wet ~21%), chorus 15, delay 10, phaser 9, flanger 7.
- The bass stays mono below about 120 Hz (the `LFMono` utility).

**Serum 1 packs (names and notes only)**
- The tables are mostly Basic Shapes / Basic Mini / Analog plus "AC hum" noise. Spectral
  "Monster", "Reesey Mess" and "FFT Squeal" tables add growl. A third embed custom tables.
- The designers' notes describe a **layering workflow**: "pair with sub, group, distort + OTT +
  EQ" (Samplifire Bass Saw), and "add a vocoder + compression + reverb" (Samplifire Resonant
  Riddim). Riddim basses are made as layers and resampled.
- Macros are named for what they do to the sound: TONE / WIDTH / THICKEN / TOP LAYER (MOONBOY),
  TIMBRE / WT POS / DRIVE / PITCH ENV (Calcium), INDEX / CRAZY / DEEPER (Samplifire).

## Riddim Forge today vs. what these presets need

| Needed | Forge now | Gap |
| --- | --- | --- |
| 8 named macros, each driving several targets | none | **missing** |
| 16-24 mod slots | 8 | too few |
| LFO into osc volume, FX wet, LFO rate, EQ freq, pitch | volume and levels yes; FX wet, LFO rate and pitch no | partial |
| Custom drawn LFO shapes; Envelope / Free modes | 8 fixed shapes, retrig on/off | **missing** |
| Phase distortion from the other osc (`PD OSC`) | FM / AM / RM from the other osc | **missing** (most-used warp) |
| 3 wavetable oscillators + sub + noise | 2 + sub + white noise | osc C missing; noise has no samples |
| 2 voice filters, comb / phaser / formant / flanger types | 1 filter, 5 basic types | **missing** |
| Frequency shifter (Bode) | none | **missing** |
| Hyper / Dimension (unison chorus) | none | missing |
| FX filter with LFO, 2-band EQ, chorus, phaser, flanger, reverb, delay | formant, comb, tilt only | missing |
| Multiband OTT, distortion modes, low mono | yes (Smash, Distortion, Low Keep) | covered; add diode, asym, sine-fold modes |
| Mono, short attack/release, porta, pitch env | yes | covered |

## Suggested changes, in order

1. **Macros**: 8 named macro knobs as mod sources, each name saved with the preset.
2. **Mod matrix to 16 slots**, with new destinations: FX unit wets, LFO 1/2 rate, master pitch,
   filter 2, EQ frequencies and the Bode shift.
3. **Drawable LFO**: up to 16 points with curve per segment; Trigger / Envelope / Free modes.
4. **PD-from-other-osc warp mode**, written from the general phase-distortion idea.
5. **Filter 2** (serial or parallel) and new types: comb +/-, phaser, flanger, formant.
6. **FX rack**: frequency shifter, hyper/dimension, FX filter, 2-band EQ, chorus, phaser,
   flanger, small reverb and delay, all with wet as a mod destination.
7. **Osc C** and sampled noise (our own recorded or generated textures, not Serum's).
8. **300+ original factory presets** in categories (Riddim, Tearout/Wub, Growl, Robot, Yoi,
   Reese, Sub, Lead, FX, Drums), built from the patterns above with macros on every one.

## On "exactly the same sound as Serum"

Bit-for-bit identical output isn't possible without copying Serum's internals, which we won't do.
These are realistic ways to get close:
- **Feature parity** (the list above) means any of these presets can be rebuilt by hand in Forge
  and land close.
- **RiddimMatch**: bounce a Serum preset to a short WAV, and the matcher searches Forge settings to
  match it. With the new features it has much more to work with.
- **Serum's own wavetables**: Forge already imports 2048-sample WAV tables, so you can load
  tables from your own Serum install (`Documents/Xfer/Serum Presets/Tables`) for personal use.
  We just can't ship them.
