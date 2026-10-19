#include "RiddimFx.h"

namespace
{
    constexpr float smashThresholdDb = -22.0f;
    constexpr float bandSplitLowHz = 220.0f, bandSplitHighHz = 2500.0f;
    constexpr int maxCombDelay = 8192;

    inline float fold (float x)
    {
        // Triangle wavefolder: reflects anything past +/-1 back into range.
        const auto t = 0.25f * x + 0.25f;
        return 4.0f * std::abs (t - std::floor (t + 0.5f)) - 1.0f;
    }

    // Formant frequencies and gains for A, E, I, O, U.
    constexpr float vowelFormants[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 280, 2250, 2900 },
                                            { 450, 800, 2830 }, { 325, 700, 2530 } };
    constexpr float formantGains[3] = { 1.0f, 0.7f, 0.35f };

    inline float softKneeClip (float x)
    {
        const auto a = std::abs (x);
        if (a <= 0.7f)
            return x;
        const auto shaped = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f);
        return std::copysign (juce::jmin (1.0f, shaped), x);
    }

    void prepareSplit (juce::dsp::LinkwitzRileyFilter<float>& lp, juce::dsp::LinkwitzRileyFilter<float>& hp,
                       const juce::dsp::ProcessSpec& spec, float hz)
    {
        lp.prepare (spec);
        hp.prepare (spec);
        lp.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        hp.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        lp.setCutoffFrequency (hz);
        hp.setCutoffFrequency (hz);
    }
}

void RiddimFx::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };

    lows.setSize (2, maxBlockSize);
    gateTrace.assign ((size_t) maxBlockSize, 1.0f);
    prepareSplit (lowSplitLP, lowSplitHP, spec, 110.0f);
    for (auto& state : smashStates)
    {
        prepareSplit (state.split1LP, state.split1HP, spec, bandSplitLowHz);
        prepareSplit (state.split2LP, state.split2HP, spec, bandSplitHighHz);
    }

    distOversampling.initProcessing ((size_t) maxBlockSize);
    resampleOversampling.initProcessing ((size_t) maxBlockSize);
    clipOversampling.initProcessing ((size_t) maxBlockSize);

    // The lows skip the distortion's oversampling, so they get its latency here to stay aligned.
    lowDelay.prepare (spec);
    lowDelay.setDelay ((float) (distOversampling.getLatencyInSamples() + resampleOversampling.getLatencyInSamples()));

    for (auto& b : combBuffer)
        b.assign (maxCombDelay, 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        lowShelf[ch].prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
        highShelf[ch].prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
    }
    lastTilt = -100.0f;

    dryWet.prepare (spec);
    dryWet.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    dryWet.setWetLatency ((float) getLatencySamples());

    reset();
}

void RiddimFx::reset()
{
    lowSplitLP.reset();
    lowSplitHP.reset();
    for (auto& state : smashStates)
    {
        for (auto* f : { &state.split1LP, &state.split1HP, &state.split2LP, &state.split2HP })
            f->reset();
        for (auto& e : state.envelope)
            e = 0.0f;
    }

    distOversampling.reset();
    resampleOversampling.reset();
    clipOversampling.reset();
    lowDelay.reset();
    for (auto& b : combBuffer)
        std::fill (b.begin(), b.end(), 0.0f);
    for (auto& f : formants)
        f = {};
    smoothedCombDelay = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        lowShelf[ch].reset();
        highShelf[ch].reset();
    }
    dryWet.reset();
}

int RiddimFx::getLatencySamples() const
{
    return juce::roundToInt (distOversampling.getLatencyInSamples() + resampleOversampling.getLatencyInSamples()
                             + clipOversampling.getLatencyInSamples());
}

//==============================================================================
void RiddimFx::process (juce::AudioBuffer<float>& buffer, const FxSettings& s)
{
    const auto numChannels = juce::jmin (2, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();
    auto* const* channels = buffer.getArrayOfWritePointers();
    juce::dsp::AudioBlock<float> block (channels, (size_t) numChannels, (size_t) numSamples);

    dryWet.pushDrySamples (block);

    // Gate from the input level: comb ringing and smash can't fill the gaps between hits.
    if (s.gate)
    {
        if ((int) gateTrace.size() < numSamples)
            gateTrace.resize ((size_t) numSamples);

        const auto release = std::exp (-1.0f / (0.012f * (float) sampleRate));
        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = juce::jmax (level, std::abs (channels[ch][i]));
            gateEnvelope = juce::jmax (level, gateEnvelope * release);

            const auto db = juce::Decibels::gainToDecibels (gateEnvelope, -120.0f);
            gateTrace[(size_t) i] = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 20.0f);
        }
    }

    // The heat macro pushes the three "loudness" stages together.
    const auto drive = juce::jlimit (0.0f, 1.0f, s.distDrive + s.heat * 0.5f);
    const auto smash = juce::jlimit (0.0f, 1.0f, s.smash + s.heat * 0.5f);
    const auto clip  = juce::jlimit (0.0f, 1.0f, s.clip + s.heat * 0.6f);

    // Low-end keep: split off the lows so they stay clean and mono.
    const auto keepLows = s.lowKeepHz >= 30.0f;
    if (keepLows)
    {
        if (lows.getNumSamples() < numSamples)
            lows.setSize (2, numSamples, false, false, true);

        lowSplitLP.setCutoffFrequency (s.lowKeepHz);
        lowSplitHP.setCutoffFrequency (s.lowKeepHz);

        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto x = channels[ch][i];
                mono += lowSplitLP.processSample (ch, x);
                channels[ch][i] = lowSplitHP.processSample (ch, x);
            }
            mono /= (float) numChannels;
            lowDelay.pushSample (0, mono);
            const auto delayed = lowDelay.popSample (0);
            for (int ch = 0; ch < 2; ++ch)
                lows.setSample (ch, i, delayed);
        }
    }

    // Always runs (even when dry) so the latency stays constant.
    distort (distOversampling, block, s, drive);

    if (s.vowelMix > 0.0f)
        formant (channels, numChannels, numSamples, s);

    if (s.combMix > 0.0f)
        comb (channels, numChannels, numSamples, s);

    if (smash > 0.0f)
        smashBands (smashStates[0], channels, numChannels, numSamples, smash);

    // Resample: hit the result with distortion and smash again, the way riddim basses are
    // bounced and re-processed. The oversampler always runs so the latency stays constant.
    distort (resampleOversampling, block, s, s.resample ? drive : 0.0f);
    if (s.resample && smash > 0.0f)
        smashBands (smashStates[1], channels, numChannels, numSamples, smash);

    // Tilt EQ around 1 kHz.
    if (std::abs (s.tilt) > 0.001f)
    {
        if (std::abs (s.tilt - lastTilt) > 1.0e-4f)
        {
            lastTilt = s.tilt;
            const auto lowGain = juce::Decibels::decibelsToGain (-s.tilt * 6.0f);
            const auto highGain = juce::Decibels::decibelsToGain (s.tilt * 6.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                *lowShelf[ch].coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, 300.0f, 0.7f, lowGain);
                *highShelf[ch].coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 3000.0f, 0.7f, highGain);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                channels[ch][i] = highShelf[ch].processSample (lowShelf[ch].processSample (channels[ch][i]));
    }

    if (keepLows)
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::add (channels[ch], lows.getReadPointer (ch), numSamples);

    clipper (block, clip);

    if (s.gate)
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply (channels[ch], gateTrace.data(), numSamples);

    block.multiplyBy (juce::Decibels::decibelsToGain (s.outputDb));

    dryWet.setWetMixProportion (s.mix);
    dryWet.mixWetSamples (block);

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::clip (channels[ch], channels[ch], -1.0f, 1.0f, numSamples);
}

//==============================================================================
void RiddimFx::distort (juce::dsp::Oversampling<float>& oversampling, juce::dsp::AudioBlock<float> block,
                        const FxSettings& s, float amount)
{
    // 2x oversampling keeps the aliasing down.
    auto up = oversampling.processSamplesUp (block);

    if (amount <= 0.0f || s.distMix <= 0.0f)
    {
        oversampling.processSamplesDown (block);
        return;
    }

    const auto drive = 1.0f + 40.0f * amount * amount;
    const auto bits = std::exp2 (16.0f - amount * 13.0f);
    const auto holdLength = 1 + (int) (amount * 24.0f);

    for (size_t i = 0; i < up.getNumSamples(); ++i)
    {
        if (++crushCounter >= holdLength)
            crushCounter = 0;

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto& sample = up.getChannelPointer (ch)[i];
            const auto dry = sample;
            float wet;

            switch (s.distType)
            {
                case 0:  wet = std::tanh (dry * drive); break;
                case 1:  wet = juce::jlimit (-1.0f, 1.0f, dry * drive); break;
                case 2:  wet = fold (dry * drive); break;
                default:
                {
                    // Bitcrush: sample-and-hold then quantise.
                    auto& hold = crushHold[juce::jmin ((size_t) 1, ch)];
                    if (crushCounter == 0)
                        hold = std::round (dry * bits) / bits;
                    wet = hold;
                    break;
                }
            }

            sample = dry + (wet * 0.8f - dry) * s.distMix;
        }
    }

    oversampling.processSamplesDown (block);
}

void RiddimFx::formant (float* const* channels, int numChannels, int numSamples, const FxSettings& s)
{
    // Three resonant band passes at the vowel's formants: the "yoi" / talking sound.
    constexpr int chunkSize = 16;
    const auto smoothing = 1.0f - std::exp (-(float) chunkSize / (0.004f * (float) sampleRate));
    constexpr float k = 0.12f; // high Q

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const auto count = juce::jmin (chunkSize, numSamples - start);
        const auto mod = s.vowelModTrace != nullptr ? s.vowelModTrace[start] * s.vowelModDepth : 0.0f;
        const auto target = juce::jlimit (0.0f, 1.0f, s.vowel + mod);
        smoothedVowel += (target - smoothedVowel) * smoothing;

        const auto v = smoothedVowel * 4.0f;
        const auto i = juce::jmin (3, (int) v);
        const auto mix = v - (float) i;

        float a1[3], a2[3], a3[3];
        for (int f = 0; f < 3; ++f)
        {
            const auto hz = vowelFormants[i][f] + mix * (vowelFormants[i + 1][f] - vowelFormants[i][f]);
            const auto g = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, (float) sampleRate * 0.45f) / (float) sampleRate);
            a1[f] = 1.0f / (1.0f + g * (g + k));
            a2[f] = g * a1[f];
            a3[f] = g * a2[f];
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = channels[ch] + start;

            for (int n = 0; n < count; ++n)
            {
                const auto x = data[n];
                float wet = 0.0f;

                for (int f = 0; f < 3; ++f)
                {
                    auto& bp = formants[f];
                    const auto v3 = x - bp.ic2[ch];
                    const auto v1 = a1[f] * bp.ic1[ch] + a2[f] * v3;
                    const auto v2 = bp.ic2[ch] + a2[f] * bp.ic1[ch] + a3[f] * v3;
                    bp.ic1[ch] = 2.0f * v1 - bp.ic1[ch];
                    bp.ic2[ch] = 2.0f * v2 - bp.ic2[ch];
                    wet += v1 * k * formantGains[f];
                }

                // The narrow peaks lose a lot of level, so make it up before mixing.
                data[n] = x + (wet * 4.0f - x) * s.vowelMix;
            }
        }
    }
}

void RiddimFx::comb (float* const* channels, int numChannels, int numSamples, const FxSettings& s)
{
    // Feedback comb: the metallic, vocal resonance in a lot of riddim growls. Tuned to the
    // playing note when the synth supplies one, so it stays in key.
    const auto hz = s.combTrackHz > 0.0f ? s.combTrackHz : s.combHz;
    const auto targetDelay = juce::jlimit (2.0f, (float) maxCombDelay - 4.0f, (float) (sampleRate / juce::jmax (20.0f, hz)));
    const auto feedback = juce::jlimit (0.0f, 0.95f, s.combFeedback);
    const auto glide = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));

    if (smoothedCombDelay <= 0.0f)
        smoothedCombDelay = targetDelay;

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedCombDelay += (targetDelay - smoothedCombDelay) * glide;

        auto readPos = (float) combWrite - smoothedCombDelay;
        if (readPos < 0.0f)
            readPos += (float) maxCombDelay;
        const auto i0 = (int) readPos;
        const auto i1 = (i0 + 1) % maxCombDelay;
        const auto frac = readPos - (float) i0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& buf = combBuffer[ch];
            const auto delayed = buf[(size_t) i0] + frac * (buf[(size_t) i1] - buf[(size_t) i0]);
            const auto x = channels[ch][i];
            const auto y = x + feedback * delayed;
            buf[(size_t) combWrite] = y;
            channels[ch][i] = x + (y * (1.0f - feedback) - x) * s.combMix;
        }

        combWrite = (combWrite + 1) % maxCombDelay;
    }
}

void RiddimFx::smashBands (SmashState& state, float* const* channels, int numChannels, int numSamples, float depth)
{
    // Three-band upward + downward compression ("OTT" style), stereo-linked per band.
    const auto attack = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
    const auto release = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate));
    const auto ratioDown = 1.0f + 9.0f * depth;
    const auto ratioUp = 1.0f + 1.5f * depth;
    const auto maxBoostDb = 10.0f * depth;
    const auto makeup = juce::Decibels::decibelsToGain (10.0f * depth);

    for (int i = 0; i < numSamples; ++i)
    {
        float bands[3][2] {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto x = channels[ch][i];
            bands[0][ch] = state.split1LP.processSample (ch, x);
            const auto rest = state.split1HP.processSample (ch, x);
            bands[1][ch] = state.split2LP.processSample (ch, rest);
            bands[2][ch] = state.split2HP.processSample (ch, rest);
        }

        float out[2] {};

        for (int b = 0; b < 3; ++b)
        {
            const auto level = juce::jmax (std::abs (bands[b][0]), std::abs (bands[b][1]));
            auto& env = state.envelope[b];
            env += (level - env) * (level > env ? attack : release);

            const auto envDb = juce::Decibels::gainToDecibels (env, -120.0f);
            float gainDb;

            if (envDb > smashThresholdDb)
            {
                gainDb = (smashThresholdDb + (envDb - smashThresholdDb) / ratioDown) - envDb;
            }
            else
            {
                // Upward: lift quiet detail, fading out near silence so noise isn't boosted.
                const auto lift = (smashThresholdDb - (smashThresholdDb - envDb) / ratioUp) - envDb;
                const auto fadeIn = juce::jlimit (0.0f, 1.0f, (envDb + 60.0f) / 20.0f);
                gainDb = juce::jmin (lift, maxBoostDb) * fadeIn;
            }

            const auto gain = juce::Decibels::decibelsToGain (gainDb);
            for (int ch = 0; ch < numChannels; ++ch)
                out[ch] += bands[b][ch] * gain;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = out[ch] * makeup;
    }
}

void RiddimFx::clipper (juce::dsp::AudioBlock<float> block, float amount)
{
    auto up = clipOversampling.processSamplesUp (block);
    const auto drive = juce::Decibels::decibelsToGain (amount * 18.0f);

    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        auto* data = up.getChannelPointer (ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            data[i] = softKneeClip (data[i] * drive);
    }

    clipOversampling.processSamplesDown (block);
}
