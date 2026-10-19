#include "ForgeRack.h"

using namespace Forge;

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline double advance (double phase, double hz, double sampleRate)
    {
        phase += hz / sampleRate;
        return phase - std::floor (phase);
    }

    // Published all-pass coefficients (Olli Niemitalo) for a wideband 90 degree phase split.
    constexpr float hilbertCoeffs[2][4] = { { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f },
                                            { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f } };
}

//==============================================================================
void Rack::DelayLine::prepare (int maxSamples)
{
    auto size = 1;
    while (size < maxSamples + 4)
        size <<= 1;
    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    write = 0;
}

float Rack::DelayLine::read (float delaySamples) const
{
    const auto position = (float) write - 1.0f - juce::jlimit (0.0f, (float) mask - 4.0f, delaySamples);
    const auto base = (int) std::floor (position);
    const auto frac = position - (float) base;
    const auto a = buffer[(size_t) (base & mask)];
    const auto b = buffer[(size_t) ((base + 1) & mask)];
    return a + frac * (b - a);
}

void Rack::Biquad::set (int shape, double hz, double q, double gainDb, double sampleRate)
{
    // RBJ cookbook shelves and peak.
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w0 = twoPi * juce::jlimit (20.0, sampleRate * 0.45, hz) / sampleRate;
    const auto cosW = std::cos (w0), alpha = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
    double nb0, nb1, nb2, na0, na1, na2;

    if (shape == 1)
    {
        nb0 = 1 + alpha * a; nb1 = -2 * cosW; nb2 = 1 - alpha * a;
        na0 = 1 + alpha / a; na1 = -2 * cosW; na2 = 1 - alpha / a;
    }
    else
    {
        const auto sq = 2.0 * std::sqrt (a) * alpha;
        const auto sign = shape == 0 ? 1.0 : -1.0; // low shelf / high shelf
        nb0 = a * ((a + 1) - sign * (a - 1) * cosW + sq);
        nb1 = sign * 2 * a * ((a - 1) - sign * (a + 1) * cosW);
        nb2 = a * ((a + 1) - sign * (a - 1) * cosW - sq);
        na0 = (a + 1) + sign * (a - 1) * cosW + sq;
        na1 = -sign * 2 * ((a - 1) + sign * (a + 1) * cosW);
        na2 = (a + 1) + sign * (a - 1) * cosW - sq;
    }

    b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
    a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
}

float Rack::Biquad::process (float x, int ch)
{
    const auto y = b0 * x + z1[ch];
    z1[ch] = b1 * x - a1 * y + z2[ch];
    z2[ch] = b2 * x - a2 * y;
    return y;
}

void Rack::Hilbert::process (float x, float& real, float& imag)
{
    float out[2];
    for (int chain = 0; chain < 2; ++chain)
    {
        auto in = x;
        for (int s = 0; s < 4; ++s)
        {
            auto& st = state[chain][s];
            const auto c = hilbertCoeffs[chain][s] * hilbertCoeffs[chain][s];
            const auto y = c * (in + st[3]) - st[1];
            st[1] = st[0]; st[0] = in;
            st[3] = st[2]; st[2] = y;
            in = y;
        }
        out[chain] = in;
    }

    // The first chain runs one sample behind the second.
    real = delayedReal;
    delayedReal = out[0];
    imag = out[1];
}

//==============================================================================
void Rack::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        hyperLines[ch].prepare ((int) (sampleRate * 0.05));
        dimensionLines[ch].prepare ((int) (sampleRate * 0.05));
        chorusLines[ch].prepare ((int) (sampleRate * 0.06));
        flangerLines[ch].prepare ((int) (sampleRate * 0.02));
        echoLines[ch].prepare ((int) (sampleRate * 4.1));
        fxFilter[ch].prepare (sampleRate);
    }

    reverb.setSampleRate (sampleRate);
    wetScratch.setSize (2, maxBlockSize);
    reset();
}

void Rack::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto* line : { &hyperLines[ch], &dimensionLines[ch], &chorusLines[ch], &flangerLines[ch], &echoLines[ch] })
            line->clear();
        fxFilter[ch].reset();
        hilbert[ch] = {};
        eq[0].z1[ch] = eq[0].z2[ch] = eq[1].z1[ch] = eq[1].z2[ch] = 0.0f;
    }

    std::fill (&phaserState[0][0], &phaserState[0][0] + 12, 0.0f);
    phaserLast[0] = phaserLast[1] = echoTone[0] = echoTone[1] = 0.0f;
    bodeFeedbackState[0] = bodeFeedbackState[1] = 0.0f;
    reverb.reset();
}

//==============================================================================
void Rack::processPre (juce::dsp::AudioBlock<float> block, const RackSettings& s)
{
    const auto numSamples = (int) block.getNumSamples();
    const auto stereo = block.getNumChannels() > 1;
    auto* left = block.getChannelPointer (0);
    auto* right = stereo ? block.getChannelPointer (1) : nullptr;

    if (s.hyperOn && (s.hyperMix > 0.0f || s.dimensionMix > 0.0f))
    {
        // Hyper: a stack of slowly drifting delay taps, alternating sides, like a unison after the synth.
        const auto voices = juce::jlimit (1, 8, s.hyperUnison);
        const auto rateHz = 0.2 + s.hyperRate * 6.0;
        const auto depth = s.hyperDetune * (float) sampleRate * 0.003f;
        const auto base = (float) sampleRate * 0.007f;
        const auto norm = 1.0f / std::sqrt ((float) voices * 0.5f + 0.5f);
        const auto dimensionDelay = (float) sampleRate * (0.004f + s.dimensionSize * 0.022f);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry[2] { left[i], stereo ? right[i] : left[i] };
            float hyper[2] {};

            for (int ch = 0; ch < 2; ++ch)
                hyperLines[ch].push (dry[ch]);

            for (int v = 0; v < voices; ++v)
            {
                const auto ch = v & 1;
                const auto offset = (float) std::sin (twoPi * hyperPhase[v]);
                hyper[ch] += hyperLines[ch].read (base + depth * (1.0f + offset) * (1.0f + (float) v * 0.3f));
                hyperPhase[v] = advance (hyperPhase[v], rateHz * (1.0 + v * 0.17), sampleRate);
            }

            float out[2];
            for (int ch = 0; ch < 2; ++ch)
                out[ch] = dry[ch] + s.hyperMix * (hyper[ch] * norm - dry[ch]);

            // Dimension: each side hears a short delay of the other.
            for (int ch = 0; ch < 2; ++ch)
                dimensionLines[ch].push (out[ch]);
            const auto crossL = dimensionLines[1].read (dimensionDelay);
            const auto crossR = dimensionLines[0].read (dimensionDelay * 1.37f);
            out[0] += s.dimensionMix * 0.7f * (crossL - out[0] * 0.3f);
            out[1] += s.dimensionMix * 0.7f * (crossR - out[1] * 0.3f);

            left[i] = out[0];
            if (stereo)
                right[i] = out[1];
        }
    }
}

void Rack::processPost (juce::dsp::AudioBlock<float> block, const RackSettings& s, double bpm)
{
    const auto numSamples = (int) block.getNumSamples();
    const auto numChannels = juce::jmin (2, (int) block.getNumChannels());
    float* ch[2] { block.getChannelPointer (0), block.getChannelPointer ((size_t) (numChannels - 1)) };

    // ---- FX filter
    if (s.filterOn && s.filter.mix > 0.0f)
        for (int c = 0; c < numChannels; ++c)
        {
            fxFilter[c].setup (s.filter);
            for (int i = 0; i < numSamples; ++i)
                ch[c][i] = fxFilter[c].process (ch[c][i]);
        }

    // ---- EQ
    if (s.eqOn)
    {
        for (int b = 0; b < 2; ++b)
            eq[b].set (s.eqShape[b], s.eqFreq[b], s.eqQ[b], s.eqGain[b], sampleRate);

        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i)
                ch[c][i] = eq[1].process (eq[0].process (ch[c][i], c), c);
    }

    // ---- Frequency shifter
    if (s.bodeOn && s.bodeMix > 0.0f)
    {
        const auto shiftHz = (s.bodeShift < 0.0f ? -1.0 : 1.0) * (double) (s.bodeShift * s.bodeShift) * 2000.0;
        const auto fb = juce::jlimit (0.0f, 0.95f, s.bodeFeedback);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto c0 = (float) std::cos (twoPi * bodePhase), s0 = (float) std::sin (twoPi * bodePhase);
            bodePhase = advance (bodePhase, shiftHz < 0.0 ? shiftHz + sampleRate : shiftHz, sampleRate);

            for (int c = 0; c < numChannels; ++c)
            {
                float re, im;
                hilbert[c].process (ch[c][i] + fb * bodeFeedbackState[c], re, im);
                const auto shifted = re * c0 - im * s0;
                bodeFeedbackState[c] = shifted;
                ch[c][i] += s.bodeMix * (shifted - ch[c][i]);
            }
        }
    }

    // ---- Chorus
    if (s.chorusOn && s.chorusMix > 0.0f)
    {
        const auto base = s.chorusDelayMs * 0.001f * (float) sampleRate;
        const auto depth = s.chorusDepth * 0.004f * (float) sampleRate;
        const auto rate = 0.05 + s.chorusRate * 4.0;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                const auto lfo = (float) std::sin (twoPi * (chorusPhase + c * 0.25));
                const auto wet = chorusLines[c].read (base + depth * (1.0f + lfo));
                chorusLines[c].push (ch[c][i] + wet * s.chorusFeedback * 0.9f);
                ch[c][i] += s.chorusMix * (wet - ch[c][i]) * 0.7f;
            }
            chorusPhase = advance (chorusPhase, rate, sampleRate);
        }
    }

    // ---- Phaser
    if (s.phaserOn && s.phaserMix > 0.0f)
    {
        const auto rate = 0.02 + s.phaserRate * 6.0;
        float coeff[2] {};

        for (int i = 0; i < numSamples; ++i)
        {
            if ((i & 15) == 0)
                for (int c = 0; c < numChannels; ++c)
                {
                    const auto lfo = std::sin (twoPi * (phaserPhase + c * 0.25));
                    const auto hz = juce::jlimit (30.0, sampleRate * 0.4, s.phaserFreq * std::exp2 (s.phaserDepth * 3.0 * lfo));
                    const auto t = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
                    coeff[c] = (float) ((t - 1.0) / (t + 1.0));
                }

            for (int c = 0; c < numChannels; ++c)
            {
                auto in = ch[c][i] + s.phaserFeedback * 0.85f * phaserLast[c];
                for (auto& st : phaserState[c])
                {
                    const auto out = coeff[c] * in + st;
                    st = in - coeff[c] * out;
                    in = out;
                }
                phaserLast[c] = in;
                ch[c][i] += s.phaserMix * (0.5f * (ch[c][i] + in) - ch[c][i]);
            }
            phaserPhase = advance (phaserPhase, rate, sampleRate);
        }
    }

    // ---- Flanger
    if (s.flangerOn && s.flangerMix > 0.0f)
    {
        const auto rate = 0.02 + s.flangerRate * 5.0;
        const auto maxDelay = (float) sampleRate * 0.0045f;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                const auto lfo = 0.5f + 0.5f * (float) std::sin (twoPi * (flangerPhase + c * 0.2));
                const auto wet = flangerLines[c].read (8.0f + maxDelay * s.flangerDepth * lfo);
                flangerLines[c].push (ch[c][i] + wet * s.flangerFeedback * 0.9f);
                ch[c][i] += s.flangerMix * (0.5f * (ch[c][i] + wet) - ch[c][i]);
            }
            flangerPhase = advance (flangerPhase, rate, sampleRate);
        }
    }

    // ---- Delay
    if (s.delayOn && s.delayMix > 0.0f)
    {
        const auto delaySamples = (float) juce::jlimit (1.0, sampleRate * 4.0, s.delayBeats * 60.0 / juce::jmax (20.0, bpm) * sampleRate);
        const auto toneCoeff = 0.1f + s.delayTone * 0.85f;

        for (int i = 0; i < numSamples; ++i)
        {
            float wet[2];
            for (int c = 0; c < 2; ++c)
                wet[c] = echoLines[c].read (delaySamples);

            for (int c = 0; c < 2; ++c)
            {
                const auto input = ch[juce::jmin (c, numChannels - 1)][i];
                const auto feed = s.delayPingPong ? wet[1 - c] : wet[c];
                echoTone[c] += toneCoeff * (feed - echoTone[c]);
                // Ping-pong: only the left line takes the dry signal, then it bounces across.
                echoLines[c].push ((s.delayPingPong && c == 1 ? 0.0f : input) + echoTone[c] * s.delayFeedback);
            }

            for (int c = 0; c < numChannels; ++c)
                ch[c][i] += s.delayMix * wet[c];
        }
    }

    // ---- Reverb
    if (s.reverbOn && s.reverbMix > 0.0f)
    {
        juce::Reverb::Parameters p;
        p.roomSize = s.reverbSize;
        p.damping = s.reverbDamping;
        p.width = s.reverbWidth;
        p.wetLevel = s.reverbMix;
        p.dryLevel = 1.0f - s.reverbMix * 0.5f;
        reverb.setParameters (p);

        if (numChannels > 1)
            reverb.processStereo (ch[0], ch[1], numSamples);
        else
            reverb.processMono (ch[0], numSamples);
    }
}
