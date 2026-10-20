#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ForgeFilter.h"

// Riddim Forge's extra effects, around the shared riddim chain:
//   Hyper / Dimension  ->  [riddim chain: distortion, formant, comb, smash, clip]  ->
//   FX Filter -> EQ -> Frequency Shifter -> Chorus -> Phaser -> Flanger -> Delay -> Reverb
// The processor runs the rack in short slices so the mod matrix can move every wet and
// key control while notes play.
namespace Forge
{
    struct RackSettings
    {
        bool hyperOn = false;
        float hyperMix = 0.0f, hyperDetune = 0.3f, hyperRate = 0.4f, dimensionSize = 0.0f, dimensionMix = 0.0f;
        int hyperUnison = 4;

        bool filterOn = false;
        FilterParams filter;

        bool eqOn = false;
        float eqFreq[2] { 200.0f, 2000.0f }, eqGain[2] {}, eqQ[2] { 0.7f, 0.7f };
        int eqShape[2] { 0, 2 }; // 0 low shelf, 1 peak, 2 high shelf

        bool bodeOn = false;
        float bodeShift = 0.0f;    // -1..1, mapped to about +/- 2 kHz on a curve
        float bodeFeedback = 0.0f, bodeMix = 0.0f;

        bool chorusOn = false;
        float chorusRate = 0.3f, chorusDepth = 0.3f, chorusDelayMs = 8.0f, chorusFeedback = 0.0f, chorusMix = 0.0f;

        bool phaserOn = false;
        float phaserRate = 0.3f, phaserDepth = 0.6f, phaserFreq = 800.0f, phaserFeedback = 0.4f, phaserMix = 0.0f;

        bool flangerOn = false;
        float flangerRate = 0.2f, flangerDepth = 0.6f, flangerFeedback = 0.5f, flangerMix = 0.0f;

        bool delayOn = false;
        float delayBeats = 0.75f;  // in quarter notes
        float delayFeedback = 0.3f, delayMix = 0.0f, delayTone = 0.6f;
        bool delayPingPong = true;

        bool reverbOn = false;
        float reverbSize = 0.4f, reverbDamping = 0.5f, reverbWidth = 1.0f, reverbMix = 0.0f;
    };

    class Rack
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // Hyper / Dimension, before the riddim chain.
        void processPre (juce::dsp::AudioBlock<float>, const RackSettings&);
        // Everything after it.
        void processPost (juce::dsp::AudioBlock<float>, const RackSettings&, double bpm);

    private:
        // A stereo modulated delay line shared by the chorus, flanger, hyper and delay.
        struct DelayLine
        {
            std::vector<float> buffer;
            int mask = 0, write = 0;
            void prepare (int maxSamples);
            void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
            void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }
            float read (float delaySamples) const;
        };

        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
            void set (int shape, double hz, double q, double gainDb, double sampleRate);
            float process (float x, int ch);
        };

        // Analytic signal from two all-pass chains (90 degrees apart), for the frequency shifter.
        struct Hilbert
        {
            float state[2][4][4] {}; // chain, stage, x1 x2 y1 y2
            float delayedReal = 0.0f;
            void process (float x, float& real, float& imag);
        };

        double sampleRate = 44100.0;

        DelayLine hyperLines[2];
        double hyperPhase[8] {};
        DelayLine dimensionLines[2];

        FilterChannel fxFilter[2];
        Biquad eq[2];

        Hilbert hilbert[2];
        double bodePhase = 0.0;
        float bodeFeedbackState[2] {};

        DelayLine chorusLines[2];
        double chorusPhase = 0.0;

        float phaserState[2][6] {}, phaserLast[2] {};
        double phaserPhase = 0.0;

        DelayLine flangerLines[2];
        double flangerPhase = 0.0;

        DelayLine echoLines[2];
        float echoTone[2] {};

        juce::Reverb reverb;
        juce::AudioBuffer<float> wetScratch;
    };
}
