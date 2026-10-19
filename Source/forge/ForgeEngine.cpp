#include "ForgeEngine.h"
#include "../dsp/RiddimEngine.h" // lfoShapeValue
#include "../Parameters.h"       // LFO rates

using namespace Forge;

namespace
{
    constexpr int controlInterval = 16; // samples between modulation updates
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline double wrap (double p) { return p - std::floor (p); }
    inline double semitonesToRatio (double semis) { return std::exp2 (semis / 12.0); }

    // Bend: reshapes the phase around the cycle's centre with a power curve.
    // exponent > 1 lingers in the middle of the wave, < 1 lingers at the edges.
    inline double bend (double p, double exponent)
    {
        const auto q = 2.0 * p - 1.0;
        return 0.5 + 0.5 * std::copysign (std::pow (std::abs (q), exponent), q);
    }

    // Asymmetry: the first half of the wave plays over [0, knee), the second over [knee, 1).
    inline double asym (double p, double knee)
    {
        return p < knee ? 0.5 * p / knee : 0.5 + 0.5 * (p - knee) / (1.0 - knee);
    }

    // Bipolar amount (0.5 = untouched) as -1..1.
    inline double bipolar (float amount) { return juce::jlimit (-1.0, 1.0, (amount - 0.5) * 2.0); }
}

WarpResult Forge::warpPhase (Warp mode, float amount, double p, float other, float sub)
{
    const auto a = (double) juce::jlimit (0.0f, 1.0f, amount);

    switch (mode)
    {
        case Warp::sync:          return { wrap (p * (1.0 + a * 15.0)) };
        case Warp::syncWindow:
        {
            // Sync with a fade over the original cycle, so the reset click turns into a vowel-ish tone.
            const auto window = std::sin (juce::MathConstants<double>::pi * p);
            return { wrap (p * (1.0 + a * 15.0)), (float) (1.0 - juce::jmin (1.0, a * 4.0) * (1.0 - window)) };
        }
        case Warp::bendPlus:      return { bend (p, 1.0 + a * 4.0) };
        case Warp::bendMinus:     return { bend (p, 1.0 / (1.0 + a * 4.0)) };
        case Warp::bendPlusMinus: { const auto b = bipolar (amount);
                                    return { bend (p, b >= 0.0 ? 1.0 + b * 4.0 : 1.0 / (1.0 - b * 4.0)) }; }
        case Warp::pwm:
        {
            // The whole cycle squeezed into the first part, then held at its start value.
            const auto width = 1.0 - a * 0.97;
            return { p < width ? p / width : 0.0 };
        }
        case Warp::asymPlus:      return { asym (p, 0.5 + a * 0.45) };
        case Warp::asymMinus:     return { asym (p, 0.5 - a * 0.45) };
        case Warp::asymPlusMinus: return { asym (p, 0.5 + bipolar (amount) * 0.45) };
        case Warp::flip:          return { p, p < a ? -1.0f : 1.0f };                     // inverts the start of the cycle
        case Warp::mirror:        { const auto m = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;    // forwards then backwards
                                    return { p + (m - p) * a }; }
        case Warp::quantize:      { const auto steps = std::exp2 (10.0 - a * 8.0);        // stepped, crushed phase
                                    return { std::floor (p * steps) / steps }; }
        case Warp::fmOther:       return { wrap (p + a * a * 3.0 * other) };
        case Warp::amOther:       return { p, (float) (1.0 - a + a * (0.5 + 0.5 * other)) };
        case Warp::rmOther:       return { p, (float) (1.0 - a + a * other) };
        case Warp::fmSub:         return { wrap (p + a * a * 3.0 * sub) };
        case Warp::off:
        case Warp::count:         break;
    }

    return { p };
}

double Forge::warpBrightness (Warp mode, float amount)
{
    const auto a = (double) juce::jlimit (0.0f, 1.0f, amount);

    switch (mode)
    {
        case Warp::sync:
        case Warp::syncWindow:    return 1.0 + a * 15.0;
        case Warp::pwm:           return 1.0 / (1.0 - a * 0.97);
        case Warp::bendPlus:
        case Warp::bendMinus:
        case Warp::bendPlusMinus:
        case Warp::asymPlus:
        case Warp::asymMinus:
        case Warp::asymPlusMinus: return 1.0 + a * 3.0;
        case Warp::quantize:      return 64.0;
        case Warp::fmOther:
        case Warp::fmSub:         return 1.0 + a * 6.0;
        case Warp::flip:
        case Warp::mirror:        return 2.0;
        case Warp::amOther:
        case Warp::rmOther:
        case Warp::off:
        case Warp::count:         break;
    }

    return 1.0;
}

const juce::StringArray& Forge::warpNames()
{
    static const juce::StringArray names { "Off", "Sync", "Sync Window", "Bend +", "Bend -", "Bend +/-", "PWM",
                                           "Asym +", "Asym -", "Asym +/-", "Flip", "Mirror", "Quantize",
                                           "FM (other)", "AM (other)", "RM (other)", "FM (sub)" };
    return names;
}

const juce::StringArray& Forge::modSourceNames()
{
    static const juce::StringArray names { "-", "LFO 1", "LFO 2", "Env 2", "Velocity", "Note", "Mod Wheel" };
    return names;
}

const juce::StringArray& Forge::modDestNames()
{
    static const juce::StringArray names { "-", "A Position", "A Warp 1", "A Warp 2", "A Level", "A Pitch",
                                           "B Position", "B Warp 1", "B Warp 2", "B Level", "B Pitch",
                                           "Sub Level", "Noise", "Cutoff", "Resonance", "Drive", "Volume", "FX Vowel" };
    return names;
}

const juce::StringArray& Forge::filterTypeNames()
{
    static const juce::StringArray names { "Low Pass 12", "Low Pass 24", "Band Pass", "High Pass", "Notch" };
    return names;
}

//==============================================================================
ForgeEngine::ForgeEngine()
{
    for (auto& v : voices)
    {
        v.amp.setSampleRate (sampleRate);
        v.env2.setSampleRate (sampleRate / controlInterval);
    }
}

void ForgeEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    for (auto& v : voices)
    {
        v.amp.setSampleRate (sampleRate);
        v.env2.setSampleRate (sampleRate / controlInterval);
    }

    voiceBuffer.setSize (2, maxBlockSize);
    vowelTrace.assign ((size_t) maxBlockSize, 0.0f);
    reset();
}

void ForgeEngine::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.note = -1;
        v.amp.reset();
        v.env2.reset();
    }

    heldNotes.clear();
    bendSemitones = 0.0f;
    hitStep = -1;
}

void ForgeEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s,
                           double bpm, std::optional<double> ppqPosition)
{
    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (voiceBuffer.getNumSamples() < numSamples)
        voiceBuffer.setSize (2, numSamples, false, false, true);
    if ((int) vowelTrace.size() < numSamples)
        vowelTrace.resize ((size_t) numSamples);

    voiceBuffer.clear();
    std::fill_n (vowelTrace.begin(), numSamples, 0.0f);

    bpm = juce::jmax (20.0, bpm);

    for (auto& v : voices)
    {
        v.amp.setParameters ({ s.attackMs * 0.001f, s.decayMs * 0.001f, s.sustain, s.releaseMs * 0.001f });
        v.env2.setParameters ({ s.env2AttackMs * 0.001f, s.env2DecayMs * 0.001f, s.env2Sustain, s.env2ReleaseMs * 0.001f });
    }

    // Free-running LFOs lock to the host bar while the transport plays.
    for (int l = 0; l < 2; ++l)
        if (ppqPosition.has_value() && ! s.lfo[l].retrigger)
            freeLfoPhase[l] = wrap (*ppqPosition * Choices::lfoCyclesPerBeat[s.lfo[l].rate]);

    int position = 0;

    auto renderUpTo = [&] (int end)
    {
        if (end <= position)
            return;

        // The newest voice's vowel modulation is traced for the FX chain's formant.
        Voice* traced = nullptr;
        for (auto& v : voices)
            if (v.active && (traced == nullptr || v.startedAt > traced->startedAt))
                traced = &v;

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, voiceBuffer.getWritePointer (0, position), voiceBuffer.getWritePointer (1, position),
                             &v == traced ? vowelTrace.data() + position : nullptr, end - position, s, bpm);
        position = end;
    };

    for (const auto metadata : midi)
    {
        renderUpTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        handleMidi (metadata.getMessage(), s);
    }

    renderUpTo (numSamples);

    for (int l = 0; l < 2; ++l)
        freeLfoPhase[l] = wrap (freeLfoPhase[l] + Choices::lfoCyclesPerBeat[s.lfo[l].rate] * bpm / 60.0 * numSamples / sampleRate);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    juce::FloatVectorOperations::copy (left, voiceBuffer.getReadPointer (0), numSamples);
    if (right != left)
        juce::FloatVectorOperations::copy (right, voiceBuffer.getReadPointer (1), numSamples);
    else
        juce::FloatVectorOperations::add (left, voiceBuffer.getReadPointer (1), numSamples);

    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v.active && (newest == nullptr || v.startedAt > newest->startedAt))
            newest = &v;

    lastLfoPhase = (float) (newest != nullptr && s.lfo[0].retrigger ? newest->lfoPhase[0] : freeLfoPhase[0]);
    if (newest != nullptr)
        currentHz = newest->hz * semitonesToRatio (bendSemitones);
}

//==============================================================================
void ForgeEngine::handleMidi (const juce::MidiMessage& msg, const Settings& s)
{
    if (msg.isNoteOn())
        noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), s);
    else if (msg.isNoteOff())
        noteOff (msg.getNoteNumber(), s);
    else if (msg.isPitchWheel())
        bendSemitones = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * (float) s.bendRange;
    else if (msg.isControllerOfType (1))
        modWheel = (float) msg.getControllerValue() / 127.0f;
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        heldNotes.clear();
        hitStep = -1;
        for (auto& v : voices)
        {
            v.amp.noteOff();
            v.env2.noteOff();
        }
    }
}

void ForgeEngine::noteOn (int note, float velocity, const Settings& s)
{
    heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
    const auto wasHolding = ! heldNotes.empty();
    heldNotes.push_back (note);

    if (s.mono)
    {
        auto& v = voices[0];
        for (size_t i = 1; i < voices.size(); ++i)
            voices[i].amp.noteOff();

        startVoice (v, note, velocity, ! (wasHolding && v.active), s);
        return;
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.startedAt < target->startedAt)
                target = &v;
    }

    startVoice (*target, note, velocity, true, s);
}

void ForgeEngine::noteOff (int note, const Settings& s)
{
    heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

    if (s.mono)
    {
        auto& v = voices[0];
        if (v.note != note)
            return;

        if (! heldNotes.empty())
        {
            v.note = heldNotes.back();
            v.targetHz = juce::MidiMessage::getMidiNoteInHertz (v.note);
            if (s.glideMs <= 0.0f)
                v.hz = v.targetHz;
        }
        else
        {
            v.amp.noteOff();
            v.env2.noteOff();
        }
        return;
    }

    for (auto& v : voices)
        if (v.active && v.note == note)
        {
            v.amp.noteOff();
            v.env2.noteOff();
        }
}

void ForgeEngine::startVoice (Voice& v, int note, float velocity, bool retrigger, const Settings& s)
{
    const auto hz = juce::MidiMessage::getMidiNoteInHertz (note);
    const auto glideFrom = v.active && s.mono ? v.hz : lastNoteHz;

    v.note = note;
    v.targetHz = hz;
    v.hz = s.glideMs > 0.0f && glideFrom > 0.0 ? glideFrom : hz;
    v.startedAt = ++noteCounter;
    lastNoteHz = hz;
    v.pitchEnv = 1.0f;

    // Every hit, legato or not, takes the next rate from the hit sequencer.
    if (s.hitSeqOn)
    {
        hitStep = (hitStep + 1) % juce::jlimit (1, numHitSteps, s.hitSeqLength);
        v.lfo1Rate = s.hitSeqRates[hitStep];
    }
    else
    {
        v.lfo1Rate = s.lfo[0].rate;
    }

    for (int l = 0; l < 2; ++l)
        if (s.lfo[l].retrigger)
            v.lfoPhase[l] = 0.0;

    if (retrigger)
    {
        v.velocity = velocity;

        if (! v.active)
        {
            // Single oscillators start at zero phase for a consistent punch; unison spreads randomly.
            for (int o = 0; o < 2; ++o)
                for (int u = 0; u < maxUnison; ++u)
                    v.phases[o][u] = s.osc[o].unison > 1 ? random.nextDouble() : 0.0;

            v.subPhase = 0.0;
            std::fill (&v.ic1[0][0], &v.ic1[0][0] + 4, 0.0f);
            std::fill (&v.ic2[0][0], &v.ic2[0][0] + 4, 0.0f);
            v.lastOsc[0] = v.lastOsc[1] = 0.0f;

            for (int l = 0; l < 2; ++l)
                v.lfoSmoothed[l] = lfoShapeValue (s.lfo[l].shape, s.lfo[l].retrigger ? 0.0 : freeLfoPhase[l]);
        }

        v.active = true;
        v.amp.noteOn();
        v.env2.noteOn();
    }
}

//==============================================================================
void ForgeEngine::renderVoice (Voice& v, float* left, float* right, float* vowelOut, int numSamples,
                               const Settings& s, double bpm)
{
    const auto sr = sampleRate;
    const auto glideCoeff = s.glideMs > 0.0f ? 1.0 - std::exp (-1.0 / (s.glideMs * 0.001 * sr / controlInterval)) : 1.0;
    const auto pitchEnvCoeff = (float) std::exp (-(double) controlInterval / (juce::jmax (1.0f, s.pitchDecayMs) * 0.001 * sr));
    const auto lfoSmoothCoeff = 1.0f - std::exp (-(float) controlInterval / (0.002f * (float) sr));
    const auto velocityGain = 0.35f + 0.65f * v.velocity;
    const UserWavetable* user[2] { userTables[0].load(), userTables[1].load() };

    const double lfoHz[2] { Choices::lfoCyclesPerBeat[v.lfo1Rate] * bpm / 60.0,
                            Choices::lfoCyclesPerBeat[s.lfo[1].rate] * bpm / 60.0 };

    int done = 0;

    while (done < numSamples)
    {
        const auto chunk = juce::jmin (controlInterval, numSamples - done);

        // ---- control-rate modulation
        v.hz += (v.targetHz - v.hz) * glideCoeff;
        v.pitchEnv *= pitchEnvCoeff;

        float lfo[2];
        for (int l = 0; l < 2; ++l)
        {
            const auto phase = s.lfo[l].retrigger ? v.lfoPhase[l] : wrap (freeLfoPhase[l] + lfoHz[l] * done / sr);
            v.lfoSmoothed[l] += (lfoShapeValue (s.lfo[l].shape, phase) - v.lfoSmoothed[l]) * lfoSmoothCoeff;
            lfo[l] = v.lfoSmoothed[l];
            if (s.lfo[l].retrigger)
                v.lfoPhase[l] = wrap (v.lfoPhase[l] + lfoHz[l] * chunk / sr);
        }

        const auto env2 = v.env2.getNextSample();

        float sources[(int) ModSource::count] {};
        sources[(int) ModSource::lfo1] = lfo[0];
        sources[(int) ModSource::lfo2] = lfo[1];
        sources[(int) ModSource::env2] = env2;
        sources[(int) ModSource::velocity] = v.velocity;
        sources[(int) ModSource::note] = (float) (v.note - 60) / 60.0f;
        sources[(int) ModSource::modWheel] = modWheel;

        float mod[(int) ModDest::count] {};
        for (const auto& slot : s.slots)
            if (slot.source != ModSource::none && slot.dest != ModDest::none)
                mod[(int) slot.dest] += sources[(int) slot.source] * slot.amount;

        if (vowelOut != nullptr)
            std::fill_n (vowelOut + done, chunk, mod[(int) ModDest::fxVowel]);

        const auto baseHz = v.hz * semitonesToRatio (bendSemitones + s.pitchEnvSemis * v.pitchEnv);

        // Oscillator setup: pitch, unison spread and gains, band-limit level, modulated position and warps.
        double incs[2][maxUnison] {};
        float gainsL[2][maxUnison] {}, gainsR[2][maxUnison] {};
        int levels[2] {};
        float positions[2] {}, warps[2][2] {};
        bool useUser[2] {};

        const ModDest posDest[2] { ModDest::posA, ModDest::posB };
        const ModDest warpDest[2][2] { { ModDest::warpA1, ModDest::warpA2 }, { ModDest::warpB1, ModDest::warpB2 } };
        const ModDest levelDest[2] { ModDest::levelA, ModDest::levelB };
        const ModDest pitchDest[2] { ModDest::pitchA, ModDest::pitchB };

        for (int o = 0; o < 2; ++o)
        {
            const auto& osc = s.osc[o];
            const auto oscHz = baseHz * semitonesToRatio (osc.octave * 12.0 + osc.semitone + osc.fine / 100.0
                                                          + mod[(int) pitchDest[o]] * 24.0);
            const auto n = juce::jlimit (1, maxUnison, osc.unison);
            const auto level = osc.on ? juce::jlimit (0.0f, 1.0f, osc.level + mod[(int) levelDest[o]]) : 0.0f;

            // Blend trades the centre voice against the detuned ones; 0.5 is even.
            const auto centreWeight = juce::jmin (1.0f, 2.0f * (1.0f - osc.blend));
            const auto sideWeight = juce::jmin (1.0f, 2.0f * osc.blend);
            float sumSquares = 0.0f;
            float weights[maxUnison] {};

            for (int u = 0; u < n; ++u)
            {
                const auto spread = n > 1 ? (float) u / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                const auto isCentre = n == 1 || std::abs (spread) < 1.0f / (float) n;
                weights[u] = isCentre ? centreWeight : sideWeight;
                sumSquares += weights[u] * weights[u];
                incs[o][u] = oscHz * semitonesToRatio (spread * osc.detune * 0.5) / sr;
                const auto pan = spread * osc.width;
                gainsL[o][u] = 1.0f - pan;
                gainsR[o][u] = 1.0f + pan;
            }

            const auto norm = level / std::sqrt (juce::jmax (1.0e-6f, sumSquares));
            for (int u = 0; u < n; ++u)
            {
                gainsL[o][u] *= weights[u] * norm;
                gainsR[o][u] *= weights[u] * norm;
            }

            auto brightness = 1.0;
            for (int w = 0; w < 2; ++w)
            {
                warps[o][w] = juce::jlimit (0.0f, 1.0f, osc.warp[w] + mod[(int) warpDest[o][w]]);
                brightness *= warpBrightness (osc.warpMode[w], warps[o][w]);
            }

            levels[o] = WavetableBank::levelForFrequency (oscHz * brightness * semitonesToRatio (osc.detune * 0.5), sr);
            positions[o] = juce::jlimit (0.0f, 1.0f, osc.position + mod[(int) posDest[o]]);
            useUser[o] = osc.table == userTable && user[o] != nullptr;
        }

        const auto subLevel = juce::jlimit (0.0f, 1.0f, s.subLevel + mod[(int) ModDest::subLevel]);
        const auto noiseLevel = juce::jlimit (0.0f, 1.0f, s.noiseLevel + mod[(int) ModDest::noiseLevel]);
        const auto subInc = baseHz * (s.subOctave < 0 ? 0.5 : 1.0) / sr;

        const auto keyTrack = std::pow ((float) (v.hz / 261.63), s.keyTrack);
        const auto cutoff = juce::jlimit (20.0f, (float) sr * 0.45f,
                                          s.cutoff * keyTrack * std::exp2 (mod[(int) ModDest::cutoff] * 7.0f));
        const auto resonance = juce::jlimit (0.0f, 1.0f, s.resonance + mod[(int) ModDest::resonance]);
        const auto drive = juce::jlimit (0.0f, 1.0f, s.drive + mod[(int) ModDest::drive]);
        const auto driveGain = 1.0f + drive * 9.0f, driveMakeup = 1.0f / (1.0f + drive * 2.0f);

        const auto k = 2.0f - 1.96f * resonance;
        const auto g = (float) std::tan (juce::MathConstants<double>::pi * cutoff / sr);
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        const auto stages = s.filterType == FilterType::lp24 ? 2 : 1;

        const auto volume = juce::jlimit (0.0f, 2.0f, 1.0f + mod[(int) ModDest::volume]);

        auto tableSample = [&] (int o, double phase)
        {
            return useUser[o] ? user[o]->sample (levels[o], positions[o], phase)
                              : bank->sample (juce::jlimit (0, bank->getNumTables() - 1, s.osc[o].table),
                                              levels[o], positions[o], phase);
        };

        // ---- audio-rate rendering
        for (int i = 0; i < chunk; ++i)
        {
            const auto sub = (float) std::sin (twoPi * v.subPhase);
            float inL = 0.0f, inR = 0.0f;

            // B first, so A's "other" modes hear this sample of B; B hears A's last one.
            for (int o = 1; o >= 0; --o)
            {
                const auto& osc = s.osc[o];
                const auto other = v.lastOsc[1 - o];
                const auto n = juce::jlimit (1, maxUnison, osc.unison);
                float mono = 0.0f;

                for (int u = 0; u < n; ++u)
                {
                    auto phase = v.phases[o][u];
                    auto gain = 1.0f;

                    for (int w = 0; w < 2; ++w)
                        if (osc.warpMode[w] != Warp::off)
                        {
                            const auto r = warpPhase (osc.warpMode[w], warps[o][w], phase, other, sub);
                            phase = r.phase;
                            gain *= r.gain;
                        }

                    const auto smp = tableSample (o, wrap (phase)) * gain;
                    mono += smp;
                    inL += smp * gainsL[o][u];
                    inR += smp * gainsR[o][u];
                    v.phases[o][u] = wrap (v.phases[o][u] + incs[o][u]);
                }

                v.lastOsc[o] = mono / (float) n;
            }

            const auto noise = noiseLevel > 0.0f ? (random.nextFloat() * 2.0f - 1.0f) * noiseLevel * 0.5f : 0.0f;
            const float in[2] { inL + noise, inR + noise };
            float out[2];

            // Drive into a state-variable filter (TPT); Low Pass 24 runs two stages.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto x = drive > 0.0f ? std::tanh (in[ch] * driveGain) * driveMakeup : in[ch];

                for (int stage = 0; stage < stages; ++stage)
                {
                    auto& ic1 = v.ic1[stage][ch];
                    auto& ic2 = v.ic2[stage][ch];
                    const auto v3 = x - ic2;
                    const auto v1 = a1 * ic1 + a2 * v3;
                    const auto v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2.0f * v1 - ic1;
                    ic2 = 2.0f * v2 - ic2;

                    switch (s.filterType)
                    {
                        case FilterType::lp12:
                        case FilterType::lp24: x = v2; break;
                        case FilterType::bp:   x = v1 * k; break;
                        case FilterType::hp:   x = x - k * v1 - v2; break;
                        case FilterType::notch:
                        case FilterType::count: x = x - k * v1; break;
                    }
                }

                out[ch] = x;
            }

            // Sub stays clean and centred, after the filter.
            v.subPhase = wrap (v.subPhase + subInc);

            const auto gain = v.amp.getNextSample() * velocityGain * volume * 0.5f;
            left[done + i]  += (out[0] + sub * subLevel) * gain;
            right[done + i] += (out[1] + sub * subLevel) * gain;
        }

        done += chunk;
    }

    if (! v.amp.isActive())
    {
        v.active = false;
        v.note = -1;
    }
}
