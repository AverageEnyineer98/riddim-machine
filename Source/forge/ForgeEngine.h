#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/Wavetables.h"
#include <optional>

// Riddim Forge's synth voice: two wavetable oscillators with two warp slots each, sub and
// noise, a drive filter, two envelopes, two LFOs, an 8-slot mod matrix, and the hit
// sequencer that gives every new note its own LFO 1 rate (the riddim rate-switching trick,
// without automation).
namespace Forge
{
    enum class Warp
    {
        off, sync, syncWindow, bendPlus, bendMinus, bendPlusMinus, pwm, asymPlus, asymMinus, asymPlusMinus,
        flip, mirror, quantize, fmOther, amOther, rmOther, fmSub, count
    };

    enum class ModSource { none, lfo1, lfo2, env2, velocity, note, modWheel, count };

    enum class ModDest
    {
        none, posA, warpA1, warpA2, levelA, pitchA, posB, warpB1, warpB2, levelB, pitchB,
        subLevel, noiseLevel, cutoff, resonance, drive, volume, fxVowel, count
    };

    enum class FilterType { lp12, lp24, bp, hp, notch, count };

    constexpr int numModSlots = 8;
    constexpr int numHitSteps = 8;
    constexpr int maxUnison = 7;
    constexpr int userTable = 8; // table index that plays the imported wavetable

    struct Settings
    {
        struct Osc
        {
            bool on = true;
            int table = 0;
            float position = 0.0f, level = 0.8f;
            int octave = 0, semitone = 0;
            float fine = 0.0f;              // cents
            int unison = 1;
            float detune = 0.2f, blend = 0.5f, width = 0.8f;
            Warp warpMode[2] { Warp::off, Warp::off };
            float warp[2] {};
        };

        struct Lfo
        {
            int shape = 0, rate = 4;
            bool retrigger = true;
        };

        struct Slot
        {
            ModSource source = ModSource::none;
            ModDest dest = ModDest::none;
            float amount = 0.0f; // -1..1
        };

        Osc osc[2];
        float subLevel = 0.5f, noiseLevel = 0.0f;
        int subOctave = 0; // 0 on the note, -1 an octave below

        FilterType filterType = FilterType::lp12;
        float cutoff = 20000.0f, resonance = 0.0f, drive = 0.0f, keyTrack = 0.0f;

        float attackMs = 1.0f, decayMs = 200.0f, sustain = 1.0f, releaseMs = 30.0f;  // amp
        float env2AttackMs = 1.0f, env2DecayMs = 300.0f, env2Sustain = 0.0f, env2ReleaseMs = 100.0f;

        Lfo lfo[2];
        Slot slots[numModSlots];

        bool hitSeqOn = false;
        int hitSeqLength = 4;
        int hitSeqRates[numHitSteps] { 5, 7, 7, 3, 5, 8, 5, 3 };

        float pitchEnvSemis = 0.0f, pitchDecayMs = 30.0f;
        bool mono = true;
        float glideMs = 0.0f;
        int bendRange = 12;
    };

    // Phase-domain wavetable warps: the read position is reshaped before the table lookup,
    // and a few modes also scale the output. `other` is the other oscillator's last sample
    // and `sub` the sub's, for the FM / AM / RM modes.
    struct WarpResult { double phase; float gain = 1.0f; float ring = 1.0f; };
    WarpResult warpPhase (Warp, float amount, double phase, float other, float sub);

    // How much a warp raises the oscillator's top harmonic, for band-limiting.
    double warpBrightness (Warp, float amount);

    const juce::StringArray& warpNames();
    const juce::StringArray& modSourceNames();
    const juce::StringArray& modDestNames();
    const juce::StringArray& filterTypeNames();
}

class ForgeEngine
{
public:
    static constexpr int numVoices = 8;

    ForgeEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Swapped in from the message thread; the engine never owns or frees it.
    void setUserTable (int oscillator, const UserWavetable* table) noexcept { userTables[oscillator].store (table); }

    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Forge::Settings&,
                  double bpm, std::optional<double> ppqPosition);

    double getCurrentHz() const noexcept { return currentHz; }
    float getLfoPhase() const noexcept { return lastLfoPhase; }
    int getHitStep() const noexcept { return hitStep; }

    // Starts the hit sequencer again from step 1, e.g. when the host transport starts.
    void restartHitSequence() noexcept { hitStep = -1; }

    // Per-sample FX vowel modulation (0 centred, from the mod matrix) for the last block.
    const float* getVowelTrace() const noexcept { return vowelTrace.data(); }

private:
    struct Voice
    {
        int note = -1;
        bool active = false;
        float velocity = 0.0f;
        double hz = 0.0, targetHz = 0.0;
        double phases[2][Forge::maxUnison] {};
        double subPhase = 0.0, lfoPhase[2] {};
        int lfo1Rate = 4;
        float lfoSmoothed[2] {};
        float lastOsc[2] {};
        float pitchEnv = 0.0f;
        float ic1[2][2] {}, ic2[2][2] {}; // two filter stages, per side
        juce::ADSR amp, env2;
        juce::uint32 startedAt = 0;
    };

    void handleMidi (const juce::MidiMessage&, const Forge::Settings&);
    void noteOn (int note, float velocity, const Forge::Settings&);
    void noteOff (int note, const Forge::Settings&);
    void startVoice (Voice&, int note, float velocity, bool retrigger, const Forge::Settings&);
    void renderVoice (Voice&, float* left, float* right, float* vowelOut, int numSamples, const Forge::Settings&, double bpm);

    juce::SharedResourcePointer<WavetableBank> bank;
    std::atomic<const UserWavetable*> userTables[2] { nullptr, nullptr };
    std::array<Voice, numVoices> voices;
    std::vector<int> heldNotes;
    juce::uint32 noteCounter = 0;
    double lastNoteHz = 0.0;
    float bendSemitones = 0.0f, modWheel = 0.0f;
    int hitStep = -1;

    double sampleRate = 44100.0;
    double freeLfoPhase[2] {};
    float lastLfoPhase = 0.0f;
    double currentHz = 0.0;
    juce::Random random;

    juce::AudioBuffer<float> voiceBuffer;
    std::vector<float> vowelTrace;
};
