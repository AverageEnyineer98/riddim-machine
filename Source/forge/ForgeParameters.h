#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ForgeEngine.h"

// Riddim Forge's parameters, by ID. The FX chain reuses the IDs in ../Parameters.h.
namespace ForgeID
{
    // Oscillators A and B share a layout: "fA_pos", "fB_warp1", ...
    inline juce::String osc (char which, const char* name) { return juce::String ("f") + which + "_" + name; }
    inline juce::String lfo (int index, const char* name)  { return "lfo" + juce::String (index + 1) + "_" + name; }
    inline juce::String slot (int index, const char* name) { return "mod" + juce::String (index + 1) + "_" + name; }
    inline juce::String hitRate (int step)                  { return "hitseq_rate" + juce::String (step + 1); }

    inline constexpr const char* on        = "on";
    inline constexpr const char* table     = "table";
    inline constexpr const char* pos       = "pos";
    inline constexpr const char* level     = "level";
    inline constexpr const char* octave    = "oct";
    inline constexpr const char* semi      = "semi";
    inline constexpr const char* fine      = "fine";
    inline constexpr const char* unison    = "unison";
    inline constexpr const char* detune    = "detune";
    inline constexpr const char* blend     = "blend";
    inline constexpr const char* width     = "width";
    inline constexpr const char* warp1Mode = "warp1_mode";
    inline constexpr const char* warp1     = "warp1";
    inline constexpr const char* warp2Mode = "warp2_mode";
    inline constexpr const char* warp2     = "warp2";

    inline constexpr const char* subLevel  = "sub_level";
    inline constexpr const char* subOctave = "sub_oct";
    inline constexpr const char* noise     = "noise_level";

    inline constexpr const char* filterType = "flt_type";
    inline constexpr const char* cutoff     = "flt_cutoff";
    inline constexpr const char* resonance  = "flt_res";
    inline constexpr const char* drive      = "flt_drive";
    inline constexpr const char* keyTrack   = "flt_keytrack";

    inline constexpr const char* attack  = "amp_attack";
    inline constexpr const char* decay   = "amp_decay";
    inline constexpr const char* sustain = "amp_sustain";
    inline constexpr const char* release = "amp_release";
    inline constexpr const char* env2Attack  = "env2_attack";
    inline constexpr const char* env2Decay   = "env2_decay";
    inline constexpr const char* env2Sustain = "env2_sustain";
    inline constexpr const char* env2Release = "env2_release";

    inline constexpr const char* shape   = "shape";
    inline constexpr const char* rate    = "rate";
    inline constexpr const char* retrig  = "retrig";

    inline constexpr const char* source = "src";
    inline constexpr const char* dest   = "dst";
    inline constexpr const char* amount = "amt";

    inline constexpr const char* hitSeqOn  = "hitseq_on";
    inline constexpr const char* hitSeqLen = "hitseq_len";

    inline constexpr const char* pitchEnv   = "pitch_env";
    inline constexpr const char* pitchDecay = "pitch_decay";
    inline constexpr const char* mono       = "mono";
    inline constexpr const char* glide      = "glide";
    inline constexpr const char* bendRange  = "bend_range";
}

// Built-in table names plus the imported one.
const juce::StringArray& forgeTableNames();

juce::AudioProcessorValueTreeState::ParameterLayout createForgeParameterLayout();

// Reads the synth settings for one block. Looks every parameter up once, so reading on the
// audio thread doesn't build ID strings.
class ForgeSettingsReader
{
public:
    explicit ForgeSettingsReader (const juce::AudioProcessorValueTreeState&);
    Forge::Settings read() const;

private:
    using Value = std::atomic<float>*;

    struct Osc { Value on, table, pos, level, octave, semi, fine, unison, detune, blend, width, warpMode[2], warp[2]; };
    struct Lfo { Value shape, rate, retrig; };
    struct Slot { Value source, dest, amount; };

    Osc osc[2];
    Lfo lfo[2];
    Slot slots[Forge::numModSlots];
    Value hitRates[Forge::numHitSteps];
    Value subLevel, subOctave, noise, filterType, cutoff, resonance, drive, keyTrack,
          attack, decay, sustain, release, env2Attack, env2Decay, env2Sustain, env2Release,
          hitSeqOn, hitSeqLen, pitchEnv, pitchDecay, mono, glide, bendRange;
};
