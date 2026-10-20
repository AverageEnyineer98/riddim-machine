#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// One channel of Riddim Forge's multimode filter, used twice per voice and once in the FX
// rack. Besides the usual low / band / high pass shapes it has the colour filters riddim
// patches lean on: combs, phasers, flangers, a vowel formant, a ring mod and a sample-and-hold.
namespace Forge
{
    enum class FilterType
    {
        lp12, lp24, bp12, bp24, hp12, hp24, notch, peak, ladder,
        combPlus, combMinus, phaserPlus, phaserMinus, flangerPlus, flangerMinus,
        formant, ringMod, sampleHold, count
    };

    const juce::StringArray& filterTypeNames();

    struct FilterParams
    {
        FilterType type = FilterType::lp12;
        float cutoff = 20000.0f;   // Hz: the corner, comb pitch, phaser centre, formant shift or hold rate
        float resonance = 0.0f;    // 0..1
        float drive = 0.0f;        // 0..1, a soft clip before the filter
        float var = 0.5f;          // 0..1, the type's extra control (comb damping, phaser spread, vowel...)
        float mix = 1.0f;          // dry / wet
    };

    class FilterChannel
    {
    public:
        void prepare (double sampleRate);
        void reset();

        // Call at control rate; process() then runs with these settings.
        void setup (const FilterParams&);
        float process (float input) noexcept;

    private:
        float svf (float x, int stage) noexcept;

        double sampleRate = 44100.0;
        FilterParams params;

        // State-variable (TPT) coefficients and two stages of state.
        float k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1[2] {}, ic2[2] {};
        float lastBp = 0.0f, lastLp = 0.0f, lastHp = 0.0f;

        // Ladder.
        float ladderG = 0.0f, ladderFeedback = 0.0f, ladder[4] {};

        // Combs and flangers: a fractional delay with a damped feedback path.
        std::vector<float> delay;
        int delayMask = 0, writeIndex = 0;
        float delaySamples = 1.0f, feedback = 0.0f, damping = 0.0f, dampState = 0.0f;

        // Phasers: six first-order all-passes with feedback.
        static constexpr int phaserStages = 6;
        float allpassCoeff[phaserStages] {}, allpassState[phaserStages] {}, phaserLast = 0.0f;

        // Formant: three band passes at the vowel's formants.
        float formantA1[3] {}, formantA2[3] {}, formantA3[3] {}, formantK[3] {}, formantGain[3] {};
        float formantIc1[3] {}, formantIc2[3] {};

        // Ring mod and sample-and-hold.
        double ringPhase = 0.0, ringInc = 0.0;
        double holdPhase = 0.0, holdInc = 0.0;
        float held = 0.0f;

        float driveGain = 1.0f, driveMakeup = 1.0f;
    };
}
