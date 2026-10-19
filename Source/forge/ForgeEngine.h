#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/Wavetables.h"
#include "ForgeFilter.h"
#include <optional>

// Riddim Forge's synth voice: three wavetable oscillators with two warp slots each, a sub
// and a noise source, two multimode filters, three envelopes, four LFOs (drawable), eight
// macros and a 16-slot mod matrix, plus the hit sequencer that gives every new note its own
// LFO 1 rate (the riddim rate-switching trick, without automation).
namespace Forge
{
    constexpr int numOscs = 3;
    constexpr int numLfos = 4;
    constexpr int numMacros = 8;
    constexpr int numModSlots = 16;
    constexpr int numHitSteps = 8;
    constexpr int maxUnison = 16;
    // The table index that plays the oscillator's imported wavetable (after the built-ins).
    inline int importedTable() { return WavetableBank::getTableNames().size(); }
    constexpr int lfoTableSize = 1024; // samples per drawn LFO cycle

    enum class Warp
    {
        off, sync, syncWindow, bendPlus, bendMinus, bendPlusMinus, pwm, asymPlus, asymMinus, asymPlusMinus,
        flip, mirror, quantize, fmOther, amOther, rmOther, fmSub, pdOther, count
    };

    // Which source an oscillator's FM / AM / RM / PD warps listen to.
    enum class WarpSource { a, b, c, sub, noise, count };

    enum class ModSource
    {
        none, lfo1, lfo2, lfo3, lfo4, env2, env3, ampEnv,
        macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8,
        velocity, note, modWheel, aftertouch, random, count
    };

    enum class ModDest
    {
        none,
        posA, warpA1, warpA2, levelA, pitchA, panA, detuneA,
        posB, warpB1, warpB2, levelB, pitchB, panB, detuneB,
        posC, warpC1, warpC2, levelC, pitchC, panC, detuneC,
        subLevel, noiseLevel, noiseColour,
        cutoff, resonance, drive, filterVar, filterMix,
        cutoff2, resonance2, drive2, filterVar2, filterMix2,
        volume, masterPitch, pitchEnvAmount,
        lfo1Rate, lfo2Rate, lfo3Rate, lfo4Rate,
        // Global effects, taken from the newest voice.
        fxVowel, distDrive, distMix, combMix, smash, heat,
        fxFilterCutoff, fxFilterRes, fxFilterMix, eqGain1, eqGain2, eqFreq1, eqFreq2,
        bodeShift, bodeMix, hyperMix, dimensionMix, chorusMix, phaserMix, flangerMix, delayMix, reverbMix,
        count
    };

    constexpr int firstFxDest = (int) ModDest::fxVowel;
    constexpr int numFxDests = (int) ModDest::count - firstFxDest;

    enum class Route { filter1, filter2, both, direct, count };
    enum class LfoMode { trigger, envelope, free, count };
    enum class NoiseType { white, pink, brown, hum, crackle, digital, metal, count };
    enum class SubShape { sine, triangle, saw, square, count };

    struct Settings
    {
        struct Osc
        {
            bool on = true;
            int table = 0;
            float position = 0.0f, level = 0.8f, pan = 0.0f;
            int octave = 0, semitone = 0;
            float fine = 0.0f;              // cents
            int unison = 1;
            float detune = 0.2f, blend = 0.5f, width = 0.8f, randomPhase = 0.0f;
            Warp warpMode[2] { Warp::off, Warp::off };
            float warp[2] {};
            WarpSource warpSource = WarpSource::b;
            Route route = Route::filter1;
        };

        struct Filter
        {
            bool on = true;
            FilterType type = FilterType::lp12;
            float cutoff = 20000.0f, resonance = 0.0f, drive = 0.0f, var = 0.5f, keyTrack = 0.0f, mix = 1.0f;
        };

        struct Lfo
        {
            int shape = 0;          // built-in shapes, or the last index for the drawn curve
            bool sync = true;
            int rate = 4;           // index into lfoRateNames()
            float hz = 2.0f;
            LfoMode mode = LfoMode::trigger;
            float smooth = 0.0f;
        };

        struct Slot
        {
            ModSource source = ModSource::none, aux = ModSource::none;
            ModDest dest = ModDest::none;
            float amount = 0.0f; // -1..1
            bool bipolar = false;
        };

        Osc osc[numOscs];
        bool subOn = true;
        float subLevel = 0.5f;
        int subOctave = 0;          // 0, -1 or -2
        SubShape subShape = SubShape::sine;
        Route subRoute = Route::direct;

        float noiseLevel = 0.0f, noiseColour = 0.5f;
        NoiseType noiseType = NoiseType::white;
        Route noiseRoute = Route::filter1;

        Filter filter[2];
        bool filtersParallel = false;

        float attackMs = 1.0f, decayMs = 200.0f, sustain = 1.0f, releaseMs = 30.0f;  // amp
        float envAttackMs[2] { 1.0f, 1.0f }, envDecayMs[2] { 300.0f, 300.0f }, envSustain[2] {}, envReleaseMs[2] { 100.0f, 100.0f };

        Lfo lfo[numLfos];
        float macros[numMacros] {};
        Slot slots[numModSlots];

        bool hitSeqOn = false;
        int hitSeqLength = 4;
        int hitSeqRates[numHitSteps] { 11, 14, 14, 8, 11, 15, 11, 8 };

        float pitchEnvSemis = 0.0f, pitchDecayMs = 30.0f;
        bool mono = true, legato = false;
        float glideMs = 0.0f;
        int bendRange = 12;
        int polyphony = 8;
        float masterTune = 0.0f; // semitones
    };

    // A drawn LFO cycle: points (x 0..1 rising, y 0..1) with a bend per segment (-1..1).
    struct LfoCurve
    {
        struct Point { float x, y, curve; };
        std::vector<Point> points { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };

        float evaluate (float x) const;
        void render (float* table, int size) const;
        juce::String toString() const;
        static LfoCurve fromString (const juce::String&);
        static LfoCurve fromShape (int builtInShape);
    };

    // Phase-domain wavetable warps: the read position is reshaped before the table lookup,
    // and a few modes also scale the output. `other` is the warp source's last sample.
    struct WarpResult { double phase; float gain = 1.0f; };
    WarpResult warpPhase (Warp, float amount, double phase, float other, float sub);

    // How much a warp raises the oscillator's top harmonic, for band-limiting.
    double warpBrightness (Warp, float amount);

    float builtInLfoShape (int shape, double phase);

    const juce::StringArray& warpNames();
    const juce::StringArray& warpSourceNames();
    const juce::StringArray& modSourceNames();
    const juce::StringArray& modDestNames();
    const juce::StringArray& routeNames();
    const juce::StringArray& lfoShapeNames();   // built-ins, then "Custom"
    const juce::StringArray& lfoModeNames();
    const juce::StringArray& lfoRateNames();
    const double* lfoRateCyclesPerBeat();
    const juce::StringArray& noiseTypeNames();
    const juce::StringArray& subShapeNames();
}

class ForgeEngine
{
public:
    static constexpr int maxVoices = 16;

    ForgeEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Swapped in from the message thread; the engine never owns or frees them.
    void setUserTable (int oscillator, const UserWavetable* table) noexcept { userTables[oscillator].store (table); }
    void setLfoTable (int lfo, const float* table) noexcept { lfoTables[lfo].store (table); }

    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Forge::Settings&,
                  double bpm, std::optional<double> ppqPosition);

    double getCurrentHz() const noexcept { return currentHz; }
    float getLfoPhase (int lfo = 0) const noexcept { return lastLfoPhase[lfo]; }
    float getLfoValue (int lfo) const noexcept { return lastLfoValue[lfo]; }
    int getHitStep() const noexcept { return hitStep; }
    int getNumActiveVoices() const noexcept;

    // Starts the hit sequencer again from step 1, e.g. when the host transport starts.
    void restartHitSequence() noexcept { hitStep = -1; }

    // Per-sample modulation of each global effect destination (from the newest voice), for
    // the last block. Index with (int) dest - Forge::firstFxDest.
    const float* getFxTrace (Forge::ModDest dest) const noexcept { return fxTraces[(size_t) ((int) dest - Forge::firstFxDest)].data(); }

private:
    struct Voice
    {
        int note = -1;
        bool active = false;
        float velocity = 0.0f, randomValue = 0.0f;
        double hz = 0.0, targetHz = 0.0;
        double phases[Forge::numOscs][Forge::maxUnison] {};
        double subPhase = 0.0, lfoPhase[Forge::numLfos] {};
        bool lfoDone[Forge::numLfos] {};
        int lfo1Rate = 4;
        float lfoSmoothed[Forge::numLfos] {};
        float lastOsc[Forge::numOscs] {};
        float pitchEnv = 0.0f;
        Forge::FilterChannel filters[2][2]; // filter, side
        juce::ADSR amp, env[2];
        float lfoRateMod[Forge::numLfos] {};
        juce::uint32 startedAt = 0;

        // Noise generator state.
        float pink[7] {}, brown = 0.0f, digitalHeld = 0.0f, crackleDecay = 0.0f, tone = 0.0f;
        double digitalPhase = 0.0, humPhase = 0.0, metalPhase[4] {};
    };

    void handleMidi (const juce::MidiMessage&, const Forge::Settings&);
    void noteOn (int note, float velocity, const Forge::Settings&);
    void noteOff (int note, const Forge::Settings&);
    void startVoice (Voice&, int note, float velocity, bool retrigger, const Forge::Settings&);
    void renderVoice (Voice&, float* left, float* right, int offset, int numSamples, const Forge::Settings&, double bpm, bool traced);
    float lfoValue (const Forge::Settings::Lfo&, int index, double phase) const;
    float nextNoise (Voice&, Forge::NoiseType, float colour, double noteHz);

    juce::SharedResourcePointer<WavetableBank> bank;
    std::atomic<const UserWavetable*> userTables[Forge::numOscs] { nullptr, nullptr, nullptr };
    std::atomic<const float*> lfoTables[Forge::numLfos] { nullptr, nullptr, nullptr, nullptr };
    std::array<Voice, maxVoices> voices;
    std::vector<int> heldNotes;
    juce::uint32 noteCounter = 0;
    double lastNoteHz = 0.0;
    float bendSemitones = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
    int hitStep = -1;

    double sampleRate = 44100.0;
    double freeLfoPhase[Forge::numLfos] {};
    float lastLfoPhase[Forge::numLfos] {}, lastLfoValue[Forge::numLfos] {};
    double currentHz = 0.0;
    juce::Random random;

    juce::AudioBuffer<float> voiceBuffer;
    std::vector<std::vector<float>> fxTraces;
};
