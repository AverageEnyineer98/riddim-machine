#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Wavetables.h"
#include <optional>

// Parameter values for one block, read from the processor's parameter tree.
struct EngineSettings
{
    struct Osc
    {
        int table = 0;
        float position = 0.0f, level = 0.0f;
        int octave = 0, semitone = 0, unison = 1;
        float detune = 0.0f;
        int warpMode = 0;   // 0 off, 1 sync, 2 bend, 3 mirror, 4 quantize
        float warp = 0.0f;
    };

    Osc osc[2];
    float fmAmount = 0.0f, subLevel = 0.0f, noiseLevel = 0.0f;

    int filterType = 0;
    float cutoff = 1000.0f, resonance = 0.0f, filterEnv = 0.0f, filterDecayMs = 300.0f;

    float attackMs = 2.0f, decayMs = 200.0f, sustain = 1.0f, releaseMs = 80.0f;

    int lfoShape = 0, lfoRate = 4;
    bool lfoRetrigger = true;
    float lfoToCutoff = 0.0f, lfoToPosA = 0.0f, lfoToPosB = 0.0f, lfoToFm = 0.0f, lfoToVolume = 0.0f, lfoToWarpA = 0.0f;

    // Pitch drop (or rise) at the start of every hit.
    float pitchEnvSemis = 0.0f, pitchDecayMs = 60.0f;

    bool mono = true;
    float glideMs = 0.0f;
    int bendRange = 12;
};

// Unipolar (0..1) LFO shapes, shared with the UI so the display matches the sound.
float lfoShapeValue (int shape, double phase);

class RiddimEngine
{
public:
    static constexpr int numVoices = 8;
    static constexpr int maxUnison = 5;

    RiddimEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // `ppqPosition` is set when the host transport is playing, for LFO sync.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineSettings& settings,
                  double bpm, std::optional<double> ppqPosition);

    float getLfoPhase() const noexcept { return lastLfoPhase; }

    // The newest sounding voice's pitch (with bend) and LFO value, for key-tracked and
    // LFO-driven FX. Pitch is 0 when nothing is playing.
    double getCurrentHz() const noexcept { return currentHz; }
    float getLfoValue() const noexcept { return lastLfoValue; }

    // Per-sample LFO values of the newest voice for the last processed block.
    const float* getLfoTrace() const noexcept { return lfoTrace.data(); }

private:
    struct Voice
    {
        int note = -1;
        bool active = false;
        float velocity = 0.0f;
        double hz = 0.0, targetHz = 0.0;
        double phases[2][maxUnison] {};
        double subPhase = 0.0, lfoPhase = 0.0;
        float lfoSmoothed = 0.0f;
        float filterEnv = 0.0f, pitchEnv = 0.0f;
        float ic1[2] {}, ic2[2] {};
        juce::ADSR amp;
        juce::uint32 startedAt = 0;
    };

    void handleMidi (const juce::MidiMessage&, const EngineSettings&);
    void noteOn (int note, float velocity, const EngineSettings&);
    void noteOff (int note, const EngineSettings&);
    void startVoice (Voice&, int note, float velocity, bool retriggerEnvelope, const EngineSettings&);
    void renderVoice (Voice&, float* left, float* right, float* lfoOut, int numSamples, const EngineSettings&, double lfoHz);

    juce::SharedResourcePointer<WavetableBank> bank;
    std::array<Voice, numVoices> voices;
    std::vector<int> heldNotes;
    juce::uint32 noteCounter = 0;
    double lastNoteHz = 0.0;
    float bendSemitones = 0.0f;

    double sampleRate = 44100.0;
    double freeLfoPhase = 0.0;
    float lastLfoPhase = 0.0f, lastLfoValue = 0.0f;
    double currentHz = 0.0;
    juce::Random random;

    juce::AudioBuffer<float> voiceBuffer;
    std::vector<float> lfoTrace;
};
