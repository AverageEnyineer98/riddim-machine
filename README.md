# Riddim Machine

Two riddim/dubstep synth plugins and a matching FX plugin, built with JUCE 8 (C++17) and CMake.
Both build as VST3 and Standalone on Windows, macOS and Linux, plus AU on macOS.

![synth](docs/riddim-machine-v0.5.png)

## Riddim Machine (synth)

- **3D view**: an OpenGL keyboard (C1 to C7) whose keys dip and glow with the notes being
  played, in front of a scrolling spectrum terrain driven by the output. Drag to orbit, scroll
  to zoom, double-click to reset.
- **Two wavetable oscillators** with 8 built-in tables (16 morph frames each, band-limited so
  high notes don't alias): Riddim Sync Saw, Growl FM, Yoi Formant, Tearout Fold, Metal Screech,
  Pulse Width, Reese Stack, Basic Shapes. Position, level, octave, semitone, unison (1 to 5)
  and detune per oscillator, with a live preview of the current frame.
- **Phase warp** per oscillator, the Serum-style trick behind most riddim tones: Sync (hard
  sync sweep), Bend, Mirror and Quantize (bit-step), with a Warp amount the Wobble LFO can
  drive on oscillator A (`> Warp A`).
- **Sub sine** on the note's fundamental, **FM (B into A) and noise.**
- **Filter**: low pass, band pass, high pass or notch, with resonance and a decaying envelope.
- **Wobble LFO**, tempo-synced (1/1 to 1/32, with triplets), 8 shapes including Riddim Step,
  Half Wob and Stutter. Retriggers on each note, or locks to the host bar. Routes to cutoff,
  wavetable position A and B, FM depth, volume, vowel and warp A, and shows a live playhead.
- **Mono legato with glide** (default, as most riddim basses are) or 8-voice poly, with pitch
  bend up to 24 semitones.
- **Pitch envelope** (up to ±48 semitones, 1 to 500 ms decay) that fires on every hit, legato
  included, for the dive at the start of each riddim note.
- **The riddim FX chain** below on the output.
- **14 factory presets**, including three style starting points (Old School Riddim,
  Metallic Tearout, Dark Robot Riddim), plus your own, saved to `Documents/Riddim Machine/Presets`.

## Riddim Forge (synth)

A second synth built around the tricks riddim producers use in big wavetable synths, written
from scratch from publicly documented behaviour (no code or data from any other plugin).

![forge](docs/riddim-forge-v2-ui.png)

- **Three wavetable oscillators** plus sub and noise. Two warp slots per oscillator (sync,
  bend, PWM, asym, flip, mirror, quantize, phase distortion and FM / AM / RM from another
  oscillator). The wavetable display stacks the frames in 3D and draws the live, warped frame.
- **Wavetable import**: WAV / AIFF / FLAC of 2048-sample frames (a `clm ` chunk naming another
  frame size is read too). **+WAV** opens your own `Documents/Xfer/Serum Presets/Tables` folder
  when it exists, so tables you own can be loaded; none are bundled.
- **Two filters** (serial or parallel), with a live response curve.
- **Three envelopes, four drawable LFOs** (drag points, double-click to add or remove, drag a
  segment to bend it, shift to snap), **8 named macros** and a **16-slot mod matrix**.
- **Drag-and-drop modulation**: drag a source's handle onto any knob. Rings on the knob show
  the depth and a moving dot shows the live value; right-click a knob to change or remove it.
- **Hit sequencer**: each new note takes the next LFO 1 rate, so a plain MIDI clip gets per-hit
  wobble changes.
- **FX rack**: hyper/dimension, distortion, formant, comb, smash, filter, EQ, frequency shift,
  chorus, phaser, flanger, delay, reverb.
- **347 factory presets** across 14 families (wobble, riddim, growl, tearout, yoi, screech,
  metal, robot, reese, sub, lead, pad, pluck, FX), all original and level-matched. User presets
  go to `Documents/Riddim Machine/Forge Presets`.
- **Import Serum 2 presets** you own (preset menu > Import Serum 2 preset...). The `.SerumPreset`
  file is unpacked (zstd + CBOR, read with [zstd](https://github.com/facebook/zstd), BSD licence) and
  mapped to the nearest Forge settings: oscillators, warps, wavetable position, filters, envelopes,
  drawn LFOs, macros and their names, up to 16 mod routings, the FX rack and voicing. Wavetables
  come from your own Serum folders or from the preset itself, and the sound is level-matched.
  It is an approximation, not a copy of Serum's sound, and a note lists what couldn't come across.
  Nothing from Serum or any preset pack ships with the plugin.
- **3D view tailored to each preset**: a supershape whose form, spikes, twist, colours and
  orbit rings come from the preset's own settings (and its name), reacting to notes and spectrum.
- **MilkDrop scenes behind it (GPU)**: [projectM](https://github.com/projectM-visualizer/projectm),
  the open-source MilkDrop engine, runs one of four original scenes (Tunnel, Grid, Storm, Burst)
  on the graphics card, NVIDIA, AMD or Intel, through OpenGL 3.3. The scene is picked to suit the
  preset and its colours, fold count, twist and grit come from the same settings as the 3D form,
  so changing preset or turning a knob changes the scene (knob moves blend over once the sound
  settles). projectM's own analysis of the output (bass, mid, treble, beats) drives the motion,
  and the form's glowing rim is burnt into the scene's feedback every frame, so it leaves trails.
  Buttons under the view switch FORM / FORM + MILK / MILK, pick a scene (or AUTO), and BIG opens
  the visual in its own resizable window. Without libprojectM next to the plugin the view
  shows the form alone. Pictures: `docs/riddim-forge-milk.png`, `docs/riddim-forge-milk-scenes.png`; clip with sound:
  `docs/video/forge-visual-demo.mp4`.
- **Magic Sound**: a small neural network (conditional VAE trained only on our own presets,
  `Tools/train_magic.py`) makes a new patch every click, optionally in a chosen style. Each
  click plays five candidates offline, keeps the one closest to real presets of that family,
  and levels it. Recent results are remembered so it never repeats.

### Preset pipeline

    python3 Tools/make_forge_presets.py --out Presets/forge_factory.json
    ForgePresetLab Presets/forge_factory.json lab.json        # render + measure every preset
    python3 Tools/make_forge_presets.py --levels lab.json --out Presets/forge_factory.json
    ForgePresetLab --magic 40 magic.json                      # test Magic Sound
    ForgePresetLab --serum out_folder *.SerumPreset           # test Serum 2 import on your own presets

`ForgePresetLab` is built with `-DRIDDIM_BUILD_TESTS=ON`. Retraining Magic Sound is described
at the top of `Tools/train_magic.py`.

## Riddim Machine FX (effect)

The same processing chain as its own effect, to put on any bass, one-shot, basic square patch
or mixer channel. Inspired by the idea behind processing plugins like Avant Samples'
RiddimSmith (a riddim producer's processing chain in one plugin); the design and code here are
our own.

![fx](docs/riddim-machine-fx-v0.5.png)

The chain, in order:

1. **Low Keep**: everything below the set frequency (default 110 Hz) skips the chain and is
   summed to mono, so the sub stays clean however hard the rest is pushed.
2. **Distortion**: soft clip, hard clip, wavefold or bitcrush, at 2x oversampling.
3. **Formant**: three resonant band passes at vowel formants (A E I O U). Sweeping the vowel
   with an LFO gives the "yoi" talking sound. In the synth the Wobble LFO drives it
   (`> Vowel`); the FX plugin has its own tempo-synced Movement LFO.
4. **Comb**: a feedback comb for metallic growl resonance. In the synth, Key Track tunes it to
   the note being played so it stays in key.
5. **Smash**: three-band upward and downward compression (OTT style).
6. **Resample** (optional): a second distortion and smash pass, like bouncing a bass and
   processing it again.
7. **Tilt** EQ around 1 kHz.
8. **Clip**: a soft-knee clipper at 2x oversampling.
9. **Gate**: keyed from the input level, so the gaps between riddim hits stay silent instead
   of filling with comb ringing or smash tails.
10. **Heat**: one macro that pushes drive, smash and clip together, for quick results.
11. **FX Mix** and **Output**.

8 factory chains (Riddim Chain, Square To Riddim, Yoi Machine, Tearout Smash, Metal Comb, Grit Light,
Crushed Lo-Fi, Bypass) plus user presets in `Documents/Riddim Machine/FX Presets`.

## Audio examples

`docs/audio/` (24-bit WAV, with MP3 copies in `docs/audio/mp3/` for phones and browsers) has offline renders from the test below: a riddim pattern at 150 BPM on the
1/8-triplet grid, with gaps and the wobble rate changing per hit the way it would be automated.
`square_raw.wav` is the plain square; `square_fx_chain.wav` is that square through the FX
plugin's Square To Riddim chain; `riddim_square_yoi`, `riddim_square_wob`, `riddim_wobble` and
`riddim_growl` are synth presets, and `style_old_school`, `style_metallic_tearout` and
`style_dark_robot` are the three style presets. `forge_*.wav` are the Riddim Forge factory
presets over the same pattern with no rate automation (the hit sequencer does it), and
`forge_imported_table.wav` plays an imported test wavetable.

## Sound matching

`RiddimMatch` searches for synth settings that sound like a reference bass clip and saves
them as a preset you can load in the synth. It finds the hits and their pitch in the clip,
plays the same notes through the synth and FX chain, compares log-mel spectrograms at two
resolutions, and searches about 50 parameters with CMA-ES (an evolutionary optimiser).
It works best on an isolated bass loop or one-shot (a sample-pack loop, or a bass stem),
not a full mix with drums.

```
RiddimMatch reference.wav "My Match.rmpreset" --bpm 150 --evals 2400
```

Options: `--bpm` (for the tempo-synced LFO), `--note` (MIDI note, if pitch detection gets it
wrong), `--evals` (renders to try; about 2400 takes a few minutes on 4 cores), `--seconds`
(how much of the clip to use, default 6), `--seed`. It also writes the matched render as a
WAV next to the preset. Copy the preset into `Documents/Riddim Machine/Presets`.

## Layout

| Path | What it does |
| --- | --- |
| `Source/PluginProcessor.*`, `Source/PluginEditor.*` | The synth plugin |
| `Source/fx/` | The FX plugin |
| `Source/forge/` | Riddim Forge: engine, parameters, presets, UI, 3D view, Magic Sound, Serum 2 import |
| `Source/forge/MilkLayer.*`, `MilkScenes.*` | projectM loader and the four MilkDrop scenes |
| `cmake/ProjectM.cmake` | Builds libprojectM and copies it next to Riddim Forge |
| `Source/dsp/Wavetables.*` | Wavetable generation and band-limited playback |
| `Source/dsp/RiddimEngine.*` | Voices, unison, FM, filter, envelopes, LFO, mono/glide |
| `Source/dsp/RiddimFx.*` | The processing chain shared by both plugins |
| `Source/Parameters.*` | Every parameter, by ID |
| `Source/PresetManager.*`, `Source/FactoryPresets.cpp` | Factory and user presets |
| `Source/Visualiser3D.*` | OpenGL 3D view |
| `Source/VisualFeed.h` | Lock-free audio-to-UI bridge |
| `Source/ui/` | Look and feel, control panels, preset bar |
| `Tests/RenderTest.cpp` | Offline render and sanity check |
| `Tests/ForgeRenderTest.cpp` | The same for Riddim Forge, plus wavetable import |
| `Tools/RiddimMatch.cpp` | Sound matching: reference clip to preset |
| `Tools/make_forge_presets.py`, `Tools/ForgePresetLab.cpp` | Forge factory presets: generate, render, measure, level |
| `Tools/train_magic.py` | Trains the Magic Sound model |

## Building

Requirements: CMake 3.22+ and a C++17 compiler. CMake downloads JUCE automatically;
pass `-DJUCE_DIR=/path/to/JUCE` to use a local copy instead.

**Windows** (Visual Studio 2022):

```
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

**macOS** (Xcode):

```
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

**Linux**: install `libasound2-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev`, then

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs land in `build/RiddimMachine_artefacts/Release/`, `build/RiddimForge_artefacts/Release/`
and `build/RiddimMachineFX_artefacts/Release/`. Copy the `.vst3` bundles into your plugin folder:
Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`.

Riddim Forge's MilkDrop scenes need libprojectM. CMake builds it from a pinned commit as a
separate shared library (`projectM-4.dll`, `libprojectM-4.so.4`) and copies it next to the
Forge VST3 binary and standalone app; keep it there when you copy the plugin. Turn it off with
`-DRIDDIM_WITH_PROJECTM=OFF`. On Linux it also needs `libgl-dev`.

To run the offline render check, configure with `-DRIDDIM_BUILD_TESTS=ON`, build, and run
`RiddimRenderTest <output folder>`. It prints level, clipping and CPU use per pattern and
writes the WAVs. The same option builds `ForgeRenderTest`, `RiddimMatch` and `ForgeVisualDemo`
(plays a riddim bar through chosen presets in a window, for screenshots and clips:
`ForgeVisualDemo --presets 105,286,53,148 --seconds-each 6 --wav demo.wav`).

## Licence note

libprojectM is LGPL-2.1. It ships unmodified as its own shared library that the plugin loads at
run time, so anyone can swap in their own build, and its notice and source link are in
`packaging/THIRD_PARTY_NOTICES.txt`, which goes out with every build. The four MilkDrop scenes
are written for this project (`Source/forge/MilkScenes.cpp`); no third-party presets ship.

JUCE is dual-licensed (AGPLv3 or a commercial JUCE licence). Selling a closed-source
plugin needs a JUCE licence; the free Starter tier covers small revenue.
