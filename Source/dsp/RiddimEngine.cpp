#include "RiddimEngine.h"
#include "../Parameters.h"

namespace
{
    constexpr int controlInterval = 16; // samples between modulation updates
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline double wrap (double p) { return p - std::floor (p); }

    inline double semitonesToRatio (double semis) { return std::exp2 (semis / 12.0); }

    // Serum-style phase warps, applied to the oscillator's read position.
    inline double warpPhase (int mode, float amount, double p)
    {
        switch (mode)
        {
            case 1:  return wrap (p * (1.0 + amount * 7.0));                        // hard sync
            case 2:  return 1.0 - std::pow (1.0 - p, 1.0 + amount * 4.0);           // bend: squeeze the cycle forward
            case 3:  { const auto m = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;            // mirror: forwards then backwards
                       return p + (m - p) * amount; }
            case 4:  { const auto steps = std::exp2 (10.0 - amount * 8.0);           // quantize: stepped, crushed phase
                       return std::floor (p * steps) / steps; }
            default: return p;
        }
    }

}

float lfoShapeValue (int shape, double phase)
{
    const auto p = (float) wrap (phase);

    switch (shape)
    {
        case 0:  return 0.5f - 0.5f * std::cos ((float) twoPi * p);                 // Sine, starts closed
        case 1:  return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;                      // Triangle
        case 2:  return 1.0f - p;                                                   // Saw down
        case 3:  return p;                                                          // Saw up
        case 4:  return p < 0.5f ? 1.0f : 0.0f;                                     // Square
        case 5:  { static const float steps[] { 1.0f, 0.15f, 0.65f, 0.05f };      // Riddim step
                   return steps[juce::jmin (3, (int) (p * 4.0f))]; }
        case 6:  return p < 0.15f ? p / 0.15f : std::pow (1.0f - (p - 0.15f) / 0.85f, 2.0f); // Half wob
        case 7:  return wrap (p * 4.0) < 0.5 ? 1.0f - 0.6f * p : 0.0f;              // Stutter
        default: return 0.0f;
    }
}

RiddimEngine::RiddimEngine()
{
    for (auto& v : voices)
        v.amp.setSampleRate (sampleRate);
}

void RiddimEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    for (auto& v : voices)
        v.amp.setSampleRate (sampleRate);

    voiceBuffer.setSize (2, maxBlockSize);
    lfoTrace.assign ((size_t) maxBlockSize, 0.0f);

    reset();
}

void RiddimEngine::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.note = -1;
        v.amp.reset();
    }

    heldNotes.clear();
    bendSemitones = 0.0f;
}

//==============================================================================
void RiddimEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineSettings& settings,
                            double bpm, std::optional<double> ppqPosition)
{
    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (voiceBuffer.getNumSamples() < numSamples)
        voiceBuffer.setSize (2, numSamples, false, false, true);
    if ((int) lfoTrace.size() < numSamples)
        lfoTrace.resize ((size_t) numSamples);

    for (auto& v : voices)
        v.amp.setParameters ({ settings.attackMs * 0.001f, settings.decayMs * 0.001f,
                               settings.sustain, settings.releaseMs * 0.001f });

    const auto lfoHz = Choices::lfoCyclesPerBeat[settings.lfoRate] * juce::jmax (20.0, bpm) / 60.0;

    // Free-running LFO locks to the host bar when the transport plays.
    if (ppqPosition.has_value() && ! settings.lfoRetrigger)
        freeLfoPhase = wrap (*ppqPosition * Choices::lfoCyclesPerBeat[settings.lfoRate]);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    // Render in segments between MIDI events so notes land sample-accurately.
    int position = 0;
    const auto blockStartPhase = freeLfoPhase;

    auto renderUpTo = [&] (int end)
    {
        if (end <= position)
            return;

        const auto count = end - position;
        voiceBuffer.clear (0, position, count);
        voiceBuffer.clear (1, position, count);

        // The newest voice's LFO is traced for the FX chain (formant movement).
        Voice* traced = nullptr;
        for (auto& v : voices)
            if (v.active && (traced == nullptr || v.startedAt > traced->startedAt))
                traced = &v;

        if (traced == nullptr)
            std::fill_n (lfoTrace.begin() + position, count, lfoShapeValue (settings.lfoShape, freeLfoPhase));

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, voiceBuffer.getWritePointer (0, position), voiceBuffer.getWritePointer (1, position),
                             &v == traced ? lfoTrace.data() + position : nullptr, count, settings, lfoHz);

        freeLfoPhase = wrap (blockStartPhase + lfoHz * (double) end / sampleRate);
        position = end;
    };

    for (const auto metadata : midi)
    {
        renderUpTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        handleMidi (metadata.getMessage(), settings);
    }

    renderUpTo (numSamples);

    juce::FloatVectorOperations::copy (left, voiceBuffer.getReadPointer (0), numSamples);
    if (right != left)
        juce::FloatVectorOperations::copy (right, voiceBuffer.getReadPointer (1), numSamples);
    else
        juce::FloatVectorOperations::add (left, voiceBuffer.getReadPointer (1), numSamples);

    // Report the LFO phase of the newest sounding voice for the UI.
    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v.active && (newest == nullptr || v.startedAt > newest->startedAt))
            newest = &v;

    lastLfoPhase = (float) (newest != nullptr && settings.lfoRetrigger ? newest->lfoPhase : freeLfoPhase);
    lastLfoValue = newest != nullptr ? newest->lfoSmoothed : lfoShapeValue (settings.lfoShape, freeLfoPhase);
    if (newest != nullptr)
        currentHz = newest->hz * semitonesToRatio (bendSemitones);
}

//==============================================================================
void RiddimEngine::handleMidi (const juce::MidiMessage& msg, const EngineSettings& settings)
{
    if (msg.isNoteOn())
        noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), settings);
    else if (msg.isNoteOff())
        noteOff (msg.getNoteNumber(), settings);
    else if (msg.isPitchWheel())
        bendSemitones = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * (float) settings.bendRange;
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        heldNotes.clear();
        for (auto& v : voices)
            v.amp.noteOff();
    }
}

void RiddimEngine::noteOn (int note, float velocity, const EngineSettings& settings)
{
    heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
    const auto wasHolding = ! heldNotes.empty();
    heldNotes.push_back (note);

    if (settings.mono)
    {
        // Legato: while a note is held, glide to the new one without restarting the envelope.
        auto& v = voices[0];
        for (size_t i = 1; i < voices.size(); ++i)
            voices[i].amp.noteOff();

        startVoice (v, note, velocity, ! (wasHolding && v.active), settings);
        return;
    }

    // Poly: a free voice, or steal the oldest.
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

    startVoice (*target, note, velocity, true, settings);
}

void RiddimEngine::noteOff (int note, const EngineSettings& settings)
{
    heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

    if (settings.mono)
    {
        auto& v = voices[0];

        if (v.note != note)
            return;

        if (! heldNotes.empty())
        {
            // Fall back to the last note still held.
            v.note = heldNotes.back();
            v.targetHz = juce::MidiMessage::getMidiNoteInHertz (v.note);
            if (settings.glideMs <= 0.0f)
                v.hz = v.targetHz;
        }
        else
        {
            v.amp.noteOff();
        }
        return;
    }

    for (auto& v : voices)
        if (v.active && v.note == note)
            v.amp.noteOff();
}

void RiddimEngine::startVoice (Voice& v, int note, float velocity, bool retriggerEnvelope, const EngineSettings& settings)
{
    const auto hz = juce::MidiMessage::getMidiNoteInHertz (note);
    const auto glideFrom = v.active && settings.mono ? v.hz : lastNoteHz;

    v.note = note;
    v.targetHz = hz;
    v.hz = settings.glideMs > 0.0f && glideFrom > 0.0 ? glideFrom : hz;
    v.startedAt = ++noteCounter;
    lastNoteHz = hz;

    if (settings.lfoRetrigger)
        v.lfoPhase = 0.0;

    // Every hit gets its pitch punch, legato or not.
    v.pitchEnv = 1.0f;

    if (retriggerEnvelope)
    {
        v.velocity = velocity;
        v.filterEnv = 1.0f;

        if (! v.active)
        {
            // Single oscillators start at zero phase for a consistent punch; unison spreads randomly.
            for (int o = 0; o < 2; ++o)
                for (int u = 0; u < maxUnison; ++u)
                    v.phases[o][u] = settings.osc[o].unison > 1 ? random.nextDouble() : 0.0;

            v.subPhase = 0.0;
            v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.0f;
            v.lfoSmoothed = lfoShapeValue (settings.lfoShape, settings.lfoRetrigger ? 0.0 : freeLfoPhase);
        }

        v.active = true;
        v.amp.noteOn();
    }
}

//==============================================================================
void RiddimEngine::renderVoice (Voice& v, float* left, float* right, float* lfoOut, int numSamples,
                                const EngineSettings& s, double lfoHz)
{
    const auto sr = sampleRate;
    const auto glideCoeff = s.glideMs > 0.0f ? 1.0 - std::exp (-1.0 / (s.glideMs * 0.001 * sr / controlInterval)) : 1.0;
    const auto filterEnvCoeff = (float) std::exp (-(double) controlInterval / (s.filterDecayMs * 0.001 * sr));
    const auto pitchEnvCoeff = (float) std::exp (-(double) controlInterval / (juce::jmax (1.0f, s.pitchDecayMs) * 0.001 * sr));
    const auto lfoSmoothCoeff = 1.0f - std::exp (-(float) controlInterval / (0.002f * (float) sr));
    const auto velocityGain = 0.35f + 0.65f * v.velocity;
    const auto k = 2.0f - 1.96f * s.resonance;

    int done = 0;

    while (done < numSamples)
    {
        const auto chunk = juce::jmin (controlInterval, numSamples - done);

        // ---- control-rate modulation
        v.hz += (v.targetHz - v.hz) * glideCoeff;

        const auto lfoPhase = s.lfoRetrigger ? v.lfoPhase : wrap (freeLfoPhase + lfoHz * done / sr);
        const auto lfoRaw = lfoShapeValue (s.lfoShape, lfoPhase);
        v.lfoSmoothed += (lfoRaw - v.lfoSmoothed) * lfoSmoothCoeff;
        const auto lfo = v.lfoSmoothed;
        if (lfoOut != nullptr)
            std::fill_n (lfoOut + done, chunk, lfo);
        if (s.lfoRetrigger)
            v.lfoPhase = wrap (v.lfoPhase + lfoHz * chunk / sr);

        v.filterEnv *= filterEnvCoeff;

        v.pitchEnv *= pitchEnvCoeff;

        const auto pitchRatio = semitonesToRatio (bendSemitones + s.pitchEnvSemis * v.pitchEnv);
        const auto baseHz = v.hz * pitchRatio;

        double incs[2][maxUnison] {};
        float pans[2][maxUnison] {};
        int levels[2] {};
        float positions[2] {};
        const float posMod[2] { s.lfoToPosA, s.lfoToPosB };
        const float warps[2] { juce::jlimit (0.0f, 1.0f, s.osc[0].warp + s.lfoToWarpA * lfo), s.osc[1].warp };

        for (int o = 0; o < 2; ++o)
        {
            const auto& osc = s.osc[o];
            const auto oscHz = baseHz * semitonesToRatio (osc.octave * 12.0 + osc.semitone);
            const auto n = osc.unison;

            for (int u = 0; u < n; ++u)
            {
                const auto spread = n > 1 ? (float) u / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                incs[o][u] = oscHz * semitonesToRatio (spread * osc.detune * 0.4) / sr;
                pans[o][u] = spread * 0.8f;
            }

            // Sync and quantize add harmonics above the oscillator's pitch, so band-limit for them too.
            const auto warpBrightness = osc.warpMode == 1 ? 1.0 + warps[o] * 7.0 : osc.warpMode == 4 ? 4.0 : 1.0;
            levels[o] = WavetableBank::levelForFrequency (oscHz * warpBrightness * semitonesToRatio (osc.detune * 0.4), sr);
            positions[o] = juce::jlimit (0.0f, 1.0f, osc.position + posMod[o] * lfo);
        }

        const auto fm = juce::jlimit (0.0f, 1.0f, s.fmAmount + s.lfoToFm * lfo);
        const auto fmDepth = (double) (fm * fm * 2.5f);
        // The sub sits on the note's fundamental, where a riddim sub lives; an octave lower
        // falls under 30 Hz on the usual F1 to G1 notes.
        const auto subInc = baseHz / sr;

        const auto cutoffOctaves = s.lfoToCutoff * lfo * 7.0f + s.filterEnv * v.filterEnv * 6.0f;
        const auto cutoff = juce::jlimit (20.0f, (float) sr * 0.45f, s.cutoff * std::exp2 (cutoffOctaves));
        const auto g = (float) std::tan (juce::MathConstants<double>::pi * cutoff / sr);
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        const auto volume = 1.0f - s.lfoToVolume + s.lfoToVolume * lfo;
        const float unisonGain[2] { s.osc[0].level / std::sqrt ((float) s.osc[0].unison),
                                    s.osc[1].level / std::sqrt ((float) s.osc[1].unison) };

        // ---- audio-rate rendering
        for (int i = 0; i < chunk; ++i)
        {
            float inL = 0.0f, inR = 0.0f;

            // Osc B first: it is also the FM source for A.
            float bMono = 0.0f;
            for (int u = 0; u < s.osc[1].unison; ++u)
            {
                const auto smp = bank->sample (s.osc[1].table, levels[1], positions[1],
                                               warpPhase (s.osc[1].warpMode, warps[1], v.phases[1][u]));
                bMono += smp;
                inL += smp * unisonGain[1] * (1.0f - pans[1][u]);
                inR += smp * unisonGain[1] * (1.0f + pans[1][u]);
                v.phases[1][u] = wrap (v.phases[1][u] + incs[1][u]);
            }
            bMono /= (float) s.osc[1].unison;

            for (int u = 0; u < s.osc[0].unison; ++u)
            {
                const auto phase = warpPhase (s.osc[0].warpMode, warps[0], wrap (v.phases[0][u] + fmDepth * bMono));
                const auto smp = bank->sample (s.osc[0].table, levels[0], positions[0], phase);
                inL += smp * unisonGain[0] * (1.0f - pans[0][u]);
                inR += smp * unisonGain[0] * (1.0f + pans[0][u]);
                v.phases[0][u] = wrap (v.phases[0][u] + incs[0][u]);
            }

            const auto noise = s.noiseLevel > 0.0f ? (random.nextFloat() * 2.0f - 1.0f) * s.noiseLevel * 0.5f : 0.0f;
            inL += noise;
            inR += noise;

            // State-variable filter (TPT), one per side.
            float out[2];
            const float in[2] { inL, inR };

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto v3 = in[ch] - v.ic2[ch];
                const auto v1 = a1 * v.ic1[ch] + a2 * v3;
                const auto v2 = v.ic2[ch] + a2 * v.ic1[ch] + a3 * v3;
                v.ic1[ch] = 2.0f * v1 - v.ic1[ch];
                v.ic2[ch] = 2.0f * v2 - v.ic2[ch];

                switch (s.filterType)
                {
                    case 0:  out[ch] = v2; break;
                    case 1:  out[ch] = v1 * k; break;
                    case 2:  out[ch] = in[ch] - k * v1 - v2; break;
                    default: out[ch] = in[ch] - k * v1; break;
                }
            }

            // Sub stays clean and centred, after the filter.
            const auto sub = (float) std::sin (twoPi * v.subPhase) * s.subLevel;
            v.subPhase = wrap (v.subPhase + subInc);

            const auto gain = v.amp.getNextSample() * velocityGain * volume * 0.5f;
            left[done + i]  += (out[0] + sub) * gain;
            right[done + i] += (out[1] + sub) * gain;
        }

        done += chunk;
    }

    if (! v.amp.isActive())
    {
        v.active = false;
        v.note = -1;
    }
}
