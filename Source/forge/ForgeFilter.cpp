#include "ForgeFilter.h"

using namespace Forge;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // F1, F2, F3 and their levels for A, E, I, O, U.
    constexpr float vowelFormants[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 },
                                            { 450, 800, 2830 },  { 325, 700, 2530 } };
    constexpr float vowelLevels[5][3] = { { 1.0f, 0.6f, 0.25f }, { 1.0f, 0.5f, 0.3f }, { 1.0f, 0.35f, 0.3f },
                                          { 1.0f, 0.55f, 0.2f }, { 1.0f, 0.3f, 0.15f } };

    inline float softClip (float x) { return std::tanh (x); }

    // A one-pole all-pass coefficient for a corner at hz.
    inline float allpassFor (double hz, double sampleRate)
    {
        const auto t = std::tan (pi * juce::jlimit (10.0, sampleRate * 0.45, hz) / sampleRate);
        return (float) ((t - 1.0) / (t + 1.0));
    }
}

const juce::StringArray& Forge::filterTypeNames()
{
    static const juce::StringArray names { "Low Pass 12", "Low Pass 24", "Band Pass 12", "Band Pass 24", "High Pass 12",
                                           "High Pass 24", "Notch", "Peak", "Ladder (Acid)",
                                           "Comb +", "Comb -", "Phaser +", "Phaser -", "Flanger +", "Flanger -",
                                           "Formant", "Ring Mod", "Sample & Hold" };
    return names;
}

void FilterChannel::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // Room for a comb tuned down to 15 Hz.
    auto size = 1;
    while (size < (int) (sampleRate / 15.0) + 4)
        size <<= 1;
    delay.assign ((size_t) size, 0.0f);
    delayMask = size - 1;
    reset();
}

void FilterChannel::reset()
{
    std::fill (std::begin (ic1), std::end (ic1), 0.0f);
    std::fill (std::begin (ic2), std::end (ic2), 0.0f);
    std::fill (std::begin (ladder), std::end (ladder), 0.0f);
    std::fill (std::begin (allpassState), std::end (allpassState), 0.0f);
    std::fill (std::begin (formantIc1), std::end (formantIc1), 0.0f);
    std::fill (std::begin (formantIc2), std::end (formantIc2), 0.0f);
    std::fill (delay.begin(), delay.end(), 0.0f);
    lastBp = lastLp = lastHp = dampState = phaserLast = held = 0.0f;
    ringPhase = holdPhase = 0.0;
}

void FilterChannel::setup (const FilterParams& p)
{
    params = p;
    params.mix = juce::jlimit (0.0f, 1.0f, p.mix);

    const auto res = juce::jlimit (0.0f, 1.0f, p.resonance);
    const auto var = juce::jlimit (0.0f, 1.0f, p.var);
    const auto cutoff = (double) juce::jlimit (8.0f, (float) sampleRate * 0.45f, p.cutoff);

    driveGain = 1.0f + juce::jlimit (0.0f, 1.0f, p.drive) * 9.0f;
    driveMakeup = 1.0f / (1.0f + juce::jlimit (0.0f, 1.0f, p.drive) * 2.0f);

    auto svfSetup = [this] (double hz, float damping)
    {
        k = damping;
        const auto g = (float) std::tan (pi * hz / sampleRate);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    };

    switch (p.type)
    {
        case FilterType::lp12: case FilterType::lp24: case FilterType::bp12: case FilterType::bp24:
        case FilterType::hp12: case FilterType::hp24: case FilterType::notch: case FilterType::peak:
            svfSetup (cutoff, 2.0f - 1.96f * res);
            break;

        case FilterType::ladder:
        {
            const auto g = std::tan (pi * cutoff / sampleRate);
            ladderG = (float) (g / (1.0 + g));
            ladderFeedback = res * 4.2f;
            break;
        }

        case FilterType::combPlus: case FilterType::combMinus:
        case FilterType::flangerPlus: case FilterType::flangerMinus:
        {
            delaySamples = (float) juce::jlimit (1.0, (double) delayMask - 2.0, sampleRate / cutoff);
            const auto isMinus = p.type == FilterType::combMinus || p.type == FilterType::flangerMinus;
            feedback = (isMinus ? -1.0f : 1.0f) * res * 0.97f;
            damping = var * 0.9f; // one-pole low pass inside the loop
            break;
        }

        case FilterType::phaserPlus: case FilterType::phaserMinus:
        {
            // Var spreads the stages apart; resonance feeds back for sharper notches.
            for (int s = 0; s < phaserStages; ++s)
                allpassCoeff[s] = allpassFor (cutoff * std::pow (1.0 + var * 1.5, (double) s - 2.5), sampleRate);
            feedback = (p.type == FilterType::phaserMinus ? -1.0f : 1.0f) * res * 0.9f;
            break;
        }

        case FilterType::formant:
        {
            // Var sweeps A E I O U; cutoff shifts every formant (800 Hz leaves them in place).
            const auto shift = juce::jlimit (0.35, 3.0, cutoff / 800.0);
            const auto position = var * 4.0f;
            const auto i = juce::jmin (3, (int) position);
            const auto mix = position - (float) i;

            for (int f = 0; f < 3; ++f)
            {
                const auto hz = (vowelFormants[i][f] + mix * (vowelFormants[i + 1][f] - vowelFormants[i][f])) * shift;
                formantGain[f] = vowelLevels[i][f] + mix * (vowelLevels[i + 1][f] - vowelLevels[i][f]);
                formantK[f] = 0.5f - 0.42f * res;
                const auto g = (float) std::tan (pi * juce::jlimit (40.0, sampleRate * 0.45, hz) / sampleRate);
                formantA1[f] = 1.0f / (1.0f + g * (g + formantK[f]));
                formantA2[f] = g * formantA1[f];
                formantA3[f] = g * formantA2[f];
            }
            break;
        }

        case FilterType::ringMod:
            ringInc = cutoff / sampleRate;
            break;

        case FilterType::sampleHold:
            holdInc = cutoff / sampleRate;
            break;

        case FilterType::count:
            break;
    }
}

float FilterChannel::svf (float x, int stage) noexcept
{
    const auto v3 = x - ic2[stage];
    const auto v1 = a1 * ic1[stage] + a2 * v3;
    const auto v2 = ic2[stage] + a2 * ic1[stage] + a3 * v3;
    ic1[stage] = 2.0f * v1 - ic1[stage];
    ic2[stage] = 2.0f * v2 - ic2[stage];
    lastBp = v1;
    lastLp = v2;
    lastHp = x - k * v1 - v2;
    return v2;
}

float FilterChannel::process (float input) noexcept
{
    const auto x = params.drive > 0.0f ? softClip (input * driveGain) * driveMakeup : input;
    float y = x;

    switch (params.type)
    {
        case FilterType::lp12:  y = svf (x, 0); break;
        case FilterType::lp24:  y = svf (svf (x, 0), 1); break;
        case FilterType::bp12:  svf (x, 0); y = lastBp * k; break;
        case FilterType::bp24:  svf (x, 0); { const auto b = lastBp * k; svf (b, 1); y = lastBp * k; } break;
        case FilterType::hp12:  svf (x, 0); y = lastHp; break;
        case FilterType::hp24:  svf (x, 0); { const auto h = lastHp; svf (h, 1); y = lastHp; } break;
        case FilterType::notch: svf (x, 0); y = x - k * lastBp; break;
        case FilterType::peak:  svf (x, 0); y = x + lastBp * k * (1.0f + 6.0f * (1.0f - k * 0.5f)); break;

        case FilterType::ladder:
        {
            // Four one-pole stages with saturated feedback around them.
            auto in = softClip (x - ladderFeedback * ladder[3]);
            for (auto& stage : ladder)
            {
                stage += ladderG * (in - stage);
                in = stage;
            }
            y = ladder[3] * (1.0f + ladderFeedback * 0.25f);
            break;
        }

        case FilterType::combPlus: case FilterType::combMinus:
        case FilterType::flangerPlus: case FilterType::flangerMinus:
        {
            const auto read = (float) writeIndex - delaySamples;
            const auto base = (int) std::floor (read);
            const auto frac = read - (float) base;
            const auto d0 = delay[(size_t) (base & delayMask)];
            const auto d1 = delay[(size_t) ((base + 1) & delayMask)];
            const auto delayed = d0 + frac * (d1 - d0);

            dampState += (1.0f - damping) * (delayed - dampState);
            delay[(size_t) writeIndex] = x + feedback * dampState;
            writeIndex = (writeIndex + 1) & delayMask;

            const auto isComb = params.type == FilterType::combPlus || params.type == FilterType::combMinus;
            y = isComb ? (x + feedback * dampState) * (1.0f - std::abs (feedback) * 0.5f)
                       : 0.5f * (x + (feedback < 0.0f ? -dampState : dampState));
            break;
        }

        case FilterType::phaserPlus: case FilterType::phaserMinus:
        {
            auto in = x + feedback * phaserLast;
            for (int s = 0; s < phaserStages; ++s)
            {
                const auto out = allpassCoeff[s] * in + allpassState[s];
                allpassState[s] = in - allpassCoeff[s] * out;
                in = out;
            }
            phaserLast = in;
            y = 0.5f * (x + in);
            break;
        }

        case FilterType::formant:
        {
            y = 0.0f;
            for (int f = 0; f < 3; ++f)
            {
                const auto v3 = x - formantIc2[f];
                const auto v1 = formantA1[f] * formantIc1[f] + formantA2[f] * v3;
                const auto v2 = formantIc2[f] + formantA2[f] * formantIc1[f] + formantA3[f] * v3;
                formantIc1[f] = 2.0f * v1 - formantIc1[f];
                formantIc2[f] = 2.0f * v2 - formantIc2[f];
                y += v1 * formantK[f] * formantGain[f] * 2.0f;
            }
            break;
        }

        case FilterType::ringMod:
            y = x * (float) std::sin (twoPi * ringPhase);
            ringPhase += ringInc;
            ringPhase -= std::floor (ringPhase);
            break;

        case FilterType::sampleHold:
            holdPhase += holdInc;
            if (holdPhase >= 1.0)
            {
                holdPhase -= std::floor (holdPhase);
                held = x;
            }
            // Resonance adds bit reduction on top of the rate reduction.
            if (params.resonance > 0.0f)
            {
                const auto steps = std::exp2 (16.0f - params.resonance * 13.0f);
                y = std::round (held * steps) / steps;
            }
            else
            {
                y = held;
            }
            break;

        case FilterType::count:
            break;
    }

    return x + params.mix * (y - x);
}
