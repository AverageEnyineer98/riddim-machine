#pragma once

#include <juce_dsp/juce_dsp.h>

struct FxSettings
{
    int distType = 0;
    float distDrive = 0.0f, distMix = 1.0f;
    float vowelMix = 0.0f, vowel = 0.0f; // formant filter, vowel 0..1 sweeps A E I O U
    const float* vowelModTrace = nullptr; // per-sample 0..1 modulation (the synth's LFO), scaled by vowelModDepth
    float vowelModDepth = 0.0f;
    float combMix = 0.0f, combHz = 220.0f, combFeedback = 0.6f;
    float combTrackHz = 0.0f;            // when set, tunes the comb to the playing note instead of combHz
    float smash = 0.0f;     // multiband upward/downward compression depth
    float lowKeepHz = 0.0f; // below this the signal skips the chain and stays mono; 0 = off
    float tilt = 0.0f;      // -1 dark .. +1 bright
    float clip = 0.0f;
    float heat = 0.0f;      // macro pushing drive, smash and clip together
    bool resample = false;  // run distortion + smash a second time, like resampling a bounced bass
    bool gate = true;       // silence the output wherever the input is silent, so riddim gaps stay tight
    float mix = 1.0f;
    float outputDb = 0.0f;
};

// The riddim processing chain, shared by the synth and the FX plugin:
// low-end split -> distortion -> formant -> comb -> 3-band smash -> tilt -> rejoin lows -> clipper -> output.
class RiddimFx
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>&, const FxSettings&);
    int getLatencySamples() const;

private:
    struct SmashState
    {
        juce::dsp::LinkwitzRileyFilter<float> split1LP, split1HP, split2LP, split2HP;
        float envelope[3] {};
    };

    void distort (juce::dsp::Oversampling<float>&, juce::dsp::AudioBlock<float>, const FxSettings&, float drive);
    void formant (float* const* channels, int numChannels, int numSamples, const FxSettings&);
    void comb (float* const* channels, int numChannels, int numSamples, const FxSettings&);
    void smashBands (SmashState&, float* const* channels, int numChannels, int numSamples, float depth);
    void clipper (juce::dsp::AudioBlock<float>, float amount);

    double sampleRate = 44100.0;
    juce::AudioBuffer<float> lows;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> lowDelay { 256 };

    juce::dsp::LinkwitzRileyFilter<float> lowSplitLP, lowSplitHP;
    SmashState smashStates[2];

    juce::dsp::Oversampling<float> distOversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::dsp::Oversampling<float> resampleOversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::dsp::Oversampling<float> clipOversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    float crushHold[2] {};
    int crushCounter = 0;

    struct Bandpass { float ic1[2] {}, ic2[2] {}; };
    Bandpass formants[3];
    float smoothedVowel = 0.0f;

    std::vector<float> combBuffer[2];
    int combWrite = 0;
    float smoothedCombDelay = 0.0f;

    juce::dsp::IIR::Filter<float> lowShelf[2], highShelf[2];
    float lastTilt = -100.0f;

    std::vector<float> gateTrace;
    float gateEnvelope = 0.0f;

    juce::dsp::DryWetMixer<float> dryWet { 64 };
};
