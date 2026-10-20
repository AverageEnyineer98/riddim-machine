#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ForgeEngine.h"
#include "ForgeRack.h"

// Riddim Forge's parameters, by ID. The riddim FX chain reuses the IDs in ../Parameters.h.
namespace ForgeID
{
    // Oscillators A, B and C share a layout: "fA_pos", "fC_warp1", ...
    inline juce::String osc (char which, const char* name) { return juce::String ("f") + which + "_" + name; }
    inline char oscLetter (int index)                      { return (char) ('A' + index); }
    inline juce::String filter (int index, const char* name) { return (index == 0 ? "flt_" : "flt2_") + juce::String (name); }
    inline juce::String env (int index, const char* name)    { return "env" + juce::String (index + 2) + "_" + name; } // env2, env3
    inline juce::String lfo (int index, const char* name)    { return "lfo" + juce::String (index + 1) + "_" + name; }
    inline juce::String slot (int index, const char* name)   { return "mod" + juce::String (index + 1) + "_" + name; }
    inline juce::String macro (int index)                    { return "macro" + juce::String (index + 1); }
    inline juce::String hitRate (int step)                   { return "hitseq_rate" + juce::String (step + 1); }

    // Oscillator.
    inline constexpr const char* on        = "on";
    inline constexpr const char* table     = "table";
    inline constexpr const char* pos       = "pos";
    inline constexpr const char* level     = "level";
    inline constexpr const char* pan       = "pan";
    inline constexpr const char* octave    = "oct";
    inline constexpr const char* semi      = "semi";
    inline constexpr const char* fine      = "fine";
    inline constexpr const char* unison    = "unison";
    inline constexpr const char* detune    = "detune";
    inline constexpr const char* blend     = "blend";
    inline constexpr const char* width     = "width";
    inline constexpr const char* randPhase = "rphase";
    inline constexpr const char* warp1Mode = "warp1_mode";
    inline constexpr const char* warp1     = "warp1";
    inline constexpr const char* warp2Mode = "warp2_mode";
    inline constexpr const char* warp2     = "warp2";
    inline constexpr const char* warpSrc   = "warp_src";
    inline constexpr const char* route     = "route";

    inline constexpr const char* subOn     = "sub_on";
    inline constexpr const char* subLevel  = "sub_level";
    inline constexpr const char* subOctave = "sub_oct";
    inline constexpr const char* subShape  = "sub_shape";
    inline constexpr const char* subRoute  = "sub_route";
    inline constexpr const char* noise       = "noise_level";
    inline constexpr const char* noiseColour = "noise_colour";
    inline constexpr const char* noiseType   = "noise_type";
    inline constexpr const char* noiseRoute  = "noise_route";

    // Filter (with ForgeID::filter).
    inline constexpr const char* type      = "type";
    inline constexpr const char* cutoffName = "cutoff";
    inline constexpr const char* res       = "res";
    inline constexpr const char* driveName = "drive";
    inline constexpr const char* var       = "var";
    inline constexpr const char* keytrack  = "keytrack";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* filtersParallel = "flt_parallel";

    // Filter 1 by full ID, as older presets and the sound matcher name them.
    inline constexpr const char* filterType = "flt_type";
    inline constexpr const char* cutoff     = "flt_cutoff";
    inline constexpr const char* resonance  = "flt_res";
    inline constexpr const char* drive      = "flt_drive";
    inline constexpr const char* keyTrack   = "flt_keytrack";

    inline constexpr const char* attack  = "amp_attack";
    inline constexpr const char* decay   = "amp_decay";
    inline constexpr const char* sustain = "amp_sustain";
    inline constexpr const char* release = "amp_release";
    // Envelopes 2 and 3 (with ForgeID::env).
    inline constexpr const char* envAttack  = "attack";
    inline constexpr const char* envDecay   = "decay";
    inline constexpr const char* envSustain = "sustain";
    inline constexpr const char* envRelease = "release";

    // LFO (with ForgeID::lfo).
    inline constexpr const char* shape  = "shape";
    inline constexpr const char* sync   = "sync";
    inline constexpr const char* rate   = "rate";
    inline constexpr const char* hz     = "hz";
    inline constexpr const char* mode   = "mode";
    inline constexpr const char* smooth = "smooth";

    // Mod slot (with ForgeID::slot).
    inline constexpr const char* source  = "src";
    inline constexpr const char* aux     = "aux";
    inline constexpr const char* dest    = "dst";
    inline constexpr const char* amount  = "amt";
    inline constexpr const char* bipolar = "bi";

    inline constexpr const char* hitSeqOn  = "hitseq_on";
    inline constexpr const char* hitSeqLen = "hitseq_len";

    inline constexpr const char* pitchEnv   = "pitch_env";
    inline constexpr const char* pitchDecay = "pitch_decay";
    inline constexpr const char* mono       = "mono";
    inline constexpr const char* legato     = "legato";
    inline constexpr const char* glide      = "glide";
    inline constexpr const char* bendRange  = "bend_range";
    inline constexpr const char* polyphony  = "polyphony";
    inline constexpr const char* masterTune = "master_tune";
    inline constexpr const char* tempoMode  = "tempo_mode";

    // FX rack.
    inline constexpr const char* hyperOn = "rk_hyper_on", *hyperMix = "rk_hyper_mix", *hyperDetune = "rk_hyper_detune",
                                *hyperRate = "rk_hyper_rate", *hyperUnison = "rk_hyper_unison",
                                *dimSize = "rk_dim_size", *dimMix = "rk_dim_mix";
    inline constexpr const char* fxfOn = "rk_fxf_on", *fxfType = "rk_fxf_type", *fxfCutoff = "rk_fxf_cutoff",
                                *fxfRes = "rk_fxf_res", *fxfDrive = "rk_fxf_drive", *fxfVar = "rk_fxf_var", *fxfMix = "rk_fxf_mix";
    inline constexpr const char* eqOn = "rk_eq_on", *eq1Freq = "rk_eq1_freq", *eq1Gain = "rk_eq1_gain", *eq1Q = "rk_eq1_q",
                                *eq1Shape = "rk_eq1_shape", *eq2Freq = "rk_eq2_freq", *eq2Gain = "rk_eq2_gain",
                                *eq2Q = "rk_eq2_q", *eq2Shape = "rk_eq2_shape";
    inline constexpr const char* bodeOn = "rk_shift_on", *bodeShift = "rk_shift_amount", *bodeFeedback = "rk_shift_fb",
                                *bodeMix = "rk_shift_mix";
    inline constexpr const char* chorusOn = "rk_chorus_on", *chorusRate = "rk_chorus_rate", *chorusDepth = "rk_chorus_depth",
                                *chorusDelay = "rk_chorus_delay", *chorusFeedback = "rk_chorus_fb", *chorusMix = "rk_chorus_mix";
    inline constexpr const char* phaserOn = "rk_phaser_on", *phaserRate = "rk_phaser_rate", *phaserDepth = "rk_phaser_depth",
                                *phaserFreq = "rk_phaser_freq", *phaserFeedback = "rk_phaser_fb", *phaserMix = "rk_phaser_mix";
    inline constexpr const char* flangerOn = "rk_flanger_on", *flangerRate = "rk_flanger_rate", *flangerDepth = "rk_flanger_depth",
                                *flangerFeedback = "rk_flanger_fb", *flangerMix = "rk_flanger_mix";
    inline constexpr const char* delayOn = "rk_delay_on", *delayTime = "rk_delay_time", *delayFeedback = "rk_delay_fb",
                                *delayMix = "rk_delay_mix", *delayTone = "rk_delay_tone", *delayPingPong = "rk_delay_pp";
    inline constexpr const char* reverbOn = "rk_reverb_on", *reverbSize = "rk_reverb_size", *reverbDamping = "rk_reverb_damp",
                                *reverbWidth = "rk_reverb_width", *reverbMix = "rk_reverb_mix";

    // Text kept in the state tree next to the parameters (saved with presets).
    inline juce::Identifier macroNameProperty (int index) { return juce::Identifier ("macroName" + juce::String (index + 1)); }
    inline juce::Identifier lfoCurveProperty (int index)  { return juce::Identifier ("lfoCurve" + juce::String (index + 1)); }
}

// Built-in table names plus the imported one.
const juce::StringArray& forgeTableNames();
const juce::StringArray& forgeDelayTimeNames();
const double* forgeDelayTimeBeats();
const juce::StringArray& forgeEqShapeNames();

// How synced LFOs, the hit sequencer and the delay read the song tempo. Auto plays a
// half-time project (under 100 BPM, e.g. 75) at double speed, so riddim at 75 or 150 BPM
// moves the same.
const juce::StringArray& forgeTempoModeNames();
double forgeTempoFactor (int tempoMode, double hostBpm);

juce::AudioProcessorValueTreeState::ParameterLayout createForgeParameterLayout();

// Reads the synth and rack settings for one block. Looks every parameter up once, so
// reading on the audio thread doesn't build ID strings.
class ForgeSettingsReader
{
public:
    explicit ForgeSettingsReader (const juce::AudioProcessorValueTreeState&);
    Forge::Settings read() const;
    Forge::RackSettings readRack() const;

private:
    using Value = std::atomic<float>*;
    const juce::AudioProcessorValueTreeState& state;
    std::vector<Value> values; // in the order read() and readRack() consume them
    size_t rackStart = 0;
};
