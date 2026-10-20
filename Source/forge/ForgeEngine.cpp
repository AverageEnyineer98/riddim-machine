#include "ForgeEngine.h"
#include "../dsp/RiddimEngine.h" // lfoShapeValue

using namespace Forge;

namespace
{
    constexpr int controlInterval = 16; // samples between modulation updates
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr double pi = juce::MathConstants<double>::pi;

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

    constexpr int numBuiltInLfoShapes = 11;
}

//==============================================================================
WarpResult Forge::warpPhase (Warp mode, float amount, double p, float other, float sub)
{
    const auto a = (double) juce::jlimit (0.0f, 1.0f, amount);

    switch (mode)
    {
        case Warp::sync:          return { wrap (p * (1.0 + a * 15.0)) };
        case Warp::syncWindow:
        {
            // Sync with a fade over the original cycle, so the reset click turns into a vowel-ish tone.
            const auto window = std::sin (pi * p);
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
        // Phase distortion whose knee follows the source: the wave's halves stretch and squeeze
        // at the source's rate, the vocal growl most riddim patches are built on.
        case Warp::pdOther:       return { asym (p, juce::jlimit (0.03, 0.97, 0.5 + 0.47 * a * (double) other)) };
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
        case Warp::pdOther:       return 1.0 + a * 8.0;
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

float Forge::builtInLfoShape (int shape, double phase)
{
    const auto p = wrap (phase);

    switch (shape)
    {
        case 8:  return (float) (0.5 + 0.5 * std::cos (twoPi * p));           // Dip: full, down, back up
        case 9:  return p < 0.75 ? 1.0f : 0.0f;                                // Chop 3/4
        case 10: return (float) std::pow (1.0 - p, 3.0);                       // Exp down
        default: return lfoShapeValue (shape, p);
    }
}

//==============================================================================
float LfoCurve::evaluate (float x) const
{
    if (points.empty())
        return 0.0f;

    x = juce::jlimit (0.0f, 1.0f, x);

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (x <= b.x)
        {
            const auto span = b.x - a.x;
            auto t = span > 1.0e-6f ? (x - a.x) / span : 1.0f;
            if (a.curve > 0.0f)
                t = std::pow (t, 1.0f + a.curve * 7.0f);
            else if (a.curve < 0.0f)
                t = 1.0f - std::pow (1.0f - t, 1.0f - a.curve * 7.0f);
            return a.y + (b.y - a.y) * t;
        }
    }

    return points.back().y;
}

void LfoCurve::render (float* table, int size) const
{
    for (int i = 0; i < size; ++i)
        table[i] = evaluate ((float) i / (float) size);
}

juce::String LfoCurve::toString() const
{
    juce::StringArray parts;
    for (const auto& p : points)
        parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.curve, 3));
    return parts.joinIntoString (";");
}

LfoCurve LfoCurve::fromString (const juce::String& text)
{
    LfoCurve curve;
    curve.points.clear();

    for (const auto& part : juce::StringArray::fromTokens (text, ";", {}))
    {
        const auto values = juce::StringArray::fromTokens (part, ",", {});
        if (values.size() >= 2)
            curve.points.push_back ({ juce::jlimit (0.0f, 1.0f, values[0].getFloatValue()),
                                      juce::jlimit (0.0f, 1.0f, values[1].getFloatValue()),
                                      values.size() > 2 ? juce::jlimit (-1.0f, 1.0f, values[2].getFloatValue()) : 0.0f });
    }

    std::stable_sort (curve.points.begin(), curve.points.end(), [] (const Point& a, const Point& b) { return a.x < b.x; }); // keeps vertical jumps in order

    if (curve.points.size() < 2)
        return LfoCurve();

    curve.points.front().x = 0.0f;
    curve.points.back().x = 1.0f;
    return curve;
}

LfoCurve LfoCurve::fromShape (int shape)
{
    LfoCurve curve;

    switch (shape)
    {
        case 0:  curve.points = { { 0, 0, -0.35f }, { 0.25f, 0.5f, 0.35f }, { 0.5f, 1, -0.35f }, { 0.75f, 0.5f, 0.35f }, { 1, 0, 0 } }; break;
        case 1:  curve.points = { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } }; break;
        case 2:  curve.points = { { 0, 1, 0 }, { 1, 0, 0 } }; break;
        case 3:  curve.points = { { 0, 0, 0 }, { 1, 1, 0 } }; break;
        case 4:  curve.points = { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } }; break;
        case 8:  curve.points = { { 0, 1, -0.4f }, { 0.5f, 0, 0.4f }, { 1, 1, 0 } }; break;
        case 9:  curve.points = { { 0, 1, 0 }, { 0.75f, 1, 0 }, { 0.75f, 0, 0 }, { 1, 0, 0 } }; break;
        case 10: curve.points = { { 0, 1, 0.6f }, { 1, 0, 0 } }; break;
        default:
        {
            // Sample anything else into straight segments.
            curve.points.clear();
            constexpr int n = 32;
            for (int i = 0; i <= n; ++i)
                curve.points.push_back ({ (float) i / n, builtInLfoShape (shape, (double) i / n - (i == n ? 1.0e-6 : 0.0)), 0.0f });
            break;
        }
    }

    return curve;
}

//==============================================================================
const juce::StringArray& Forge::warpNames()
{
    static const juce::StringArray names { "Off", "Sync", "Sync Window", "Bend +", "Bend -", "Bend +/-", "PWM",
                                           "Asym +", "Asym -", "Asym +/-", "Flip", "Mirror", "Quantize",
                                           "FM (src)", "AM (src)", "RM (src)", "FM (sub)", "PD (src)" };
    return names;
}

const juce::StringArray& Forge::warpSourceNames()
{
    static const juce::StringArray names { "Osc A", "Osc B", "Osc C", "Sub", "Noise" };
    return names;
}

const juce::StringArray& Forge::modSourceNames()
{
    static const juce::StringArray names { "-", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Env 2", "Env 3", "Amp Env",
                                           "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
                                           "Velocity", "Note", "Mod Wheel", "Aftertouch", "Random" };
    return names;
}

const juce::StringArray& Forge::modDestNames()
{
    static const juce::StringArray names {
        "-",
        "A Position", "A Warp 1", "A Warp 2", "A Level", "A Pitch", "A Pan", "A Detune",
        "B Position", "B Warp 1", "B Warp 2", "B Level", "B Pitch", "B Pan", "B Detune",
        "C Position", "C Warp 1", "C Warp 2", "C Level", "C Pitch", "C Pan", "C Detune",
        "Sub Level", "Noise Level", "Noise Colour",
        "F1 Cutoff", "F1 Resonance", "F1 Drive", "F1 Var", "F1 Mix",
        "F2 Cutoff", "F2 Resonance", "F2 Drive", "F2 Var", "F2 Mix",
        "Volume", "Master Pitch", "Pitch Env",
        "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "LFO 4 Rate",
        "FX Vowel", "Dist Drive", "Dist Mix", "Comb Mix", "Smash", "Heat",
        "FX Filter Cutoff", "FX Filter Res", "FX Filter Mix", "EQ Gain 1", "EQ Gain 2", "EQ Freq 1", "EQ Freq 2",
        "Shift Amount", "Shift Mix", "Hyper Mix", "Dimension Mix", "Chorus Mix", "Phaser Mix", "Flanger Mix",
        "Delay Mix", "Reverb Mix" };
    jassert (names.size() == (int) ModDest::count);
    return names;
}

const juce::StringArray& Forge::routeNames()
{
    static const juce::StringArray names { "Filter 1", "Filter 2", "Both", "Direct" };
    return names;
}

const juce::StringArray& Forge::lfoShapeNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Riddim Step",
                                           "Half Wob", "Stutter", "Dip", "Chop 3/4", "Exp Down", "Custom" };
    return names;
}

const juce::StringArray& Forge::lfoModeNames()
{
    static const juce::StringArray names { "Trigger", "Envelope", "Free" };
    return names;
}

const juce::StringArray& Forge::lfoRateNames()
{
    static const juce::StringArray names { "8/1", "4/1", "2/1", "1/1", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.",
                                           "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
    return names;
}

const double* Forge::lfoRateCyclesPerBeat()
{
    static const double rates[] { 1.0 / 32.0, 1.0 / 16.0, 0.125, 0.25, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0,
                                  2.0, 3.0, 8.0 / 3.0, 4.0, 6.0, 8.0, 12.0, 16.0 };
    return rates;
}

const juce::StringArray& Forge::noiseTypeNames()
{
    static const juce::StringArray names { "White", "Pink", "Brown", "Hum", "Crackle", "Digital", "Metal" };
    return names;
}

const juce::StringArray& Forge::subShapeNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw", "Square" };
    return names;
}

//==============================================================================
ForgeEngine::ForgeEngine()
{
    prepare (sampleRate, 512);
}

void ForgeEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    for (auto& v : voices)
    {
        v.amp.setSampleRate (sampleRate);
        for (auto& e : v.env)
            e.setSampleRate (sampleRate / controlInterval);
        for (auto& filter : v.filters)
            for (auto& side : filter)
                side.prepare (sampleRate);
    }

    voiceBuffer.setSize (2, maxBlockSize);
    fxTraces.assign ((size_t) numFxDests, std::vector<float> ((size_t) maxBlockSize, 0.0f));
    reset();
}

void ForgeEngine::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.note = -1;
        v.amp.reset();
        for (auto& e : v.env)
            e.reset();
    }

    heldNotes.clear();
    bendSemitones = 0.0f;
    hitStep = -1;
}

int ForgeEngine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

float ForgeEngine::lfoValue (const Settings::Lfo& lfo, int index, double phase) const
{
    if (lfo.shape >= numBuiltInLfoShapes)
        if (const auto* table = lfoTables[index].load())
        {
            const auto position = wrap (phase) * lfoTableSize;
            const auto i0 = (int) position;
            const auto frac = (float) (position - i0);
            const auto a = table[i0 & (lfoTableSize - 1)];
            const auto b = table[(i0 + 1) & (lfoTableSize - 1)];
            return a + frac * (b - a);
        }

    return builtInLfoShape (juce::jmin (lfo.shape, numBuiltInLfoShapes - 1), phase);
}

void ForgeEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s,
                           double bpm, std::optional<double> ppqPosition)
{
    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (voiceBuffer.getNumSamples() < numSamples)
        voiceBuffer.setSize (2, numSamples, false, false, true);
    for (auto& trace : fxTraces)
        if ((int) trace.size() < numSamples)
            trace.resize ((size_t) numSamples);

    voiceBuffer.clear();
    bpm = juce::jmax (20.0, bpm);

    // Effects modulated only by macros, the mod wheel or aftertouch still move with no notes held.
    {
        float sources[(int) ModSource::count] {};
        for (int m = 0; m < numMacros; ++m)
            sources[(int) ModSource::macro1 + m] = s.macros[m];
        sources[(int) ModSource::modWheel] = modWheel;
        sources[(int) ModSource::aftertouch] = aftertouch;

        float fxMod[numFxDests] {};
        for (const auto& slot : s.slots)
        {
            const auto d = (int) slot.dest - firstFxDest;
            if (d < 0 || slot.source == ModSource::none)
                continue;
            auto value = sources[(int) slot.source];
            if (slot.bipolar) value = value * 2.0f - 1.0f;
            if (slot.aux != ModSource::none) value *= sources[(int) slot.aux];
            fxMod[d] += value * slot.amount;
        }

        for (int d = 0; d < numFxDests; ++d)
            std::fill_n (fxTraces[(size_t) d].begin(), numSamples, fxMod[d]);
    }

    for (auto& v : voices)
    {
        v.amp.setParameters ({ s.attackMs * 0.001f, s.decayMs * 0.001f, s.sustain, s.releaseMs * 0.001f });
        for (int e = 0; e < 2; ++e)
            v.env[e].setParameters ({ s.envAttackMs[e] * 0.001f, s.envDecayMs[e] * 0.001f, s.envSustain[e], s.envReleaseMs[e] * 0.001f });
    }

    // Free LFOs: tempo-synced ones lock to the host bar while the transport plays.
    double freeHz[numLfos];
    for (int l = 0; l < numLfos; ++l)
    {
        const auto& lfo = s.lfo[l];
        freeHz[l] = lfo.sync ? lfoRateCyclesPerBeat()[juce::jlimit (0, lfoRateNames().size() - 1, lfo.rate)] * bpm / 60.0 : lfo.hz;
        if (ppqPosition.has_value() && lfo.mode == LfoMode::free && lfo.sync)
            freeLfoPhase[l] = wrap (*ppqPosition * lfoRateCyclesPerBeat()[juce::jlimit (0, lfoRateNames().size() - 1, lfo.rate)]);
    }

    // The newest voice's modulation is traced for the global effects.
    const Voice* traced = nullptr;
    int position = 0;

    auto renderUpTo = [&] (int end)
    {
        if (end <= position)
            return;

        traced = nullptr;
        for (auto& v : voices)
            if (v.active && (traced == nullptr || v.startedAt > traced->startedAt))
                traced = &v;

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, voiceBuffer.getWritePointer (0, position), voiceBuffer.getWritePointer (1, position),
                             position, end - position, s, bpm, &v == traced);
        position = end;
    };

    for (const auto metadata : midi)
    {
        renderUpTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        handleMidi (metadata.getMessage(), s);
    }

    renderUpTo (numSamples);

    for (int l = 0; l < numLfos; ++l)
        freeLfoPhase[l] = wrap (freeLfoPhase[l] + freeHz[l] * numSamples / sampleRate);

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

    for (int l = 0; l < numLfos; ++l)
    {
        const auto phase = newest != nullptr && s.lfo[l].mode != LfoMode::free ? newest->lfoPhase[l] : freeLfoPhase[l];
        lastLfoPhase[l] = (float) phase;
        lastLfoValue[l] = newest != nullptr ? newest->lfoSmoothed[l] : lfoValue (s.lfo[l], l, phase);
    }

    if (newest != nullptr)
        currentHz = newest->hz * semitonesToRatio (bendSemitones + s.masterTune);
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
    else if (msg.isChannelPressure())
        aftertouch = (float) msg.getChannelPressureValue() / 127.0f;
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        heldNotes.clear();
        hitStep = -1;
        for (auto& v : voices)
        {
            v.amp.noteOff();
            for (auto& e : v.env)
                e.noteOff();
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

        // Legato: overlapping notes glide without restarting the envelopes.
        startVoice (v, note, velocity, ! (s.legato && wasHolding && v.active), s);
        return;
    }

    const auto limit = juce::jlimit (1, maxVoices, s.polyphony);
    Voice* target = nullptr;
    for (int i = 0; i < limit; ++i)
        if (! voices[(size_t) i].active) { target = &voices[(size_t) i]; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (int i = 0; i < limit; ++i)
            if (voices[(size_t) i].startedAt < target->startedAt)
                target = &voices[(size_t) i];
    }

    startVoice (*target, note, velocity, true, s);
}

void ForgeEngine::noteOff (int note, const Settings& s)
{
    heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

    auto release = [] (Voice& v)
    {
        v.amp.noteOff();
        for (auto& e : v.env)
            e.noteOff();
    };

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
            release (v);
        }
        return;
    }

    for (auto& v : voices)
        if (v.active && v.note == note)
            release (v);
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

    for (int l = 0; l < numLfos; ++l)
        if (s.lfo[l].mode != LfoMode::free)
        {
            v.lfoPhase[l] = 0.0;
            v.lfoDone[l] = false;
        }

    if (retrigger)
    {
        v.velocity = velocity;
        v.randomValue = random.nextFloat();

        if (! v.active)
        {
            // Single oscillators start at a fixed phase for a consistent punch unless random
            // phase is turned up; unison voices always spread.
            for (int o = 0; o < numOscs; ++o)
                for (int u = 0; u < maxUnison; ++u)
                    v.phases[o][u] = s.osc[o].unison > 1 ? random.nextDouble()
                                                          : s.osc[o].randomPhase * random.nextDouble();

            v.subPhase = 0.0;
            for (auto& filter : v.filters)
                for (auto& side : filter)
                    side.reset();
            std::fill (std::begin (v.lastOsc), std::end (v.lastOsc), 0.0f);
            std::fill (std::begin (v.lfoRateMod), std::end (v.lfoRateMod), 0.0f);

            for (int l = 0; l < numLfos; ++l)
                v.lfoSmoothed[l] = lfoValue (s.lfo[l], l, s.lfo[l].mode == LfoMode::free ? freeLfoPhase[l] : 0.0);
        }

        v.active = true;
        v.amp.noteOn();
        for (auto& e : v.env)
            e.noteOn();
    }
}

//==============================================================================
float ForgeEngine::nextNoise (Voice& v, NoiseType type, float colour, double noteHz)
{
    const auto white = random.nextFloat() * 2.0f - 1.0f;
    float out = white;

    switch (type)
    {
        case NoiseType::white:
            break;

        case NoiseType::pink:
        {
            // Paul Kellet's economy pink filter.
            auto* b = v.pink;
            b[0] = 0.99886f * b[0] + white * 0.0555179f;
            b[1] = 0.99332f * b[1] + white * 0.0750759f;
            b[2] = 0.96900f * b[2] + white * 0.1538520f;
            b[3] = 0.86650f * b[3] + white * 0.3104856f;
            b[4] = 0.55000f * b[4] + white * 0.5329522f;
            b[5] = -0.7616f * b[5] - white * 0.0168980f;
            out = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f) * 0.11f;
            b[6] = white * 0.115926f;
            break;
        }

        case NoiseType::brown:
            v.brown = juce::jlimit (-1.0f, 1.0f, v.brown + white * 0.02f) * 0.998f;
            out = v.brown * 3.0f;
            break;

        case NoiseType::hum:
        {
            // Mains-style buzz: odd harmonics of 50-120 Hz with a little jitter and hiss.
            const auto base = 50.0 + colour * 70.0;
            v.humPhase = wrap (v.humPhase + base * (1.0 + white * 0.002) / sampleRate);
            float buzz = 0.0f;
            for (int h = 1; h <= 9; h += 2)
                buzz += (float) std::sin (twoPi * v.humPhase * h) / (float) h;
            out = buzz * 0.8f + white * 0.08f;
            break;
        }

        case NoiseType::crackle:
            if (random.nextFloat() < 0.0004f + colour * 0.004f)
                v.crackleDecay = (random.nextFloat() * 2.0f - 1.0f);
            v.crackleDecay *= 0.93f;
            out = v.crackleDecay * 2.0f;
            break;

        case NoiseType::digital:
        {
            // Sample-and-hold noise at a key-tracked rate.
            v.digitalPhase += noteHz * (1.0 + colour * 31.0) / sampleRate;
            if (v.digitalPhase >= 1.0)
            {
                v.digitalPhase -= std::floor (v.digitalPhase);
                v.digitalHeld = white;
            }
            return v.digitalHeld * 0.7f;
        }

        case NoiseType::metal:
        {
            // Four inharmonic sines ring-modulated by noise.
            constexpr double ratios[4] { 1.0, 1.414, 2.236, 3.162 };
            float ring = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                v.metalPhase[i] = wrap (v.metalPhase[i] + noteHz * ratios[i] * 2.0 / sampleRate);
                ring += (float) std::sin (twoPi * v.metalPhase[i]);
            }
            out = ring * 0.25f * (0.4f + 0.6f * std::abs (white));
            return out;
        }

        case NoiseType::count:
            break;
    }

    // Colour tilts white, pink and brown between dark and bright.
    if (type == NoiseType::white || type == NoiseType::pink || type == NoiseType::brown)
    {
        const auto coeff = 0.02f + colour * colour * 0.98f;
        v.tone += coeff * (out - v.tone);
        out = colour > 0.5f ? out - v.tone * (colour - 0.5f) * 1.6f : v.tone;
    }

    return out;
}

//==============================================================================
void ForgeEngine::renderVoice (Voice& v, float* left, float* right, int offset, int numSamples,
                               const Settings& s, double bpm, bool traced)
{
    const auto sr = sampleRate;
    const auto glideCoeff = s.glideMs > 0.0f ? 1.0 - std::exp (-1.0 / (s.glideMs * 0.001 * sr / controlInterval)) : 1.0;
    const auto pitchEnvCoeff = (float) std::exp (-(double) controlInterval / (juce::jmax (1.0f, s.pitchDecayMs) * 0.001 * sr));
    const UserWavetable* user[numOscs] { userTables[0].load(), userTables[1].load(), userTables[2].load() };
    const auto numTables = bank->getNumTables();

    double lfoBaseHz[numLfos];
    float lfoSmoothCoeff[numLfos];
    for (int l = 0; l < numLfos; ++l)
    {
        const auto& lfo = s.lfo[l];
        const auto rateIndex = juce::jlimit (0, lfoRateNames().size() - 1, l == 0 ? v.lfo1Rate : lfo.rate);
        lfoBaseHz[l] = lfo.sync ? lfoRateCyclesPerBeat()[rateIndex] * bpm / 60.0 : lfo.hz;
        const auto smoothSeconds = 0.0015f + lfo.smooth * lfo.smooth * 0.08f;
        lfoSmoothCoeff[l] = 1.0f - std::exp (-(float) controlInterval / (smoothSeconds * (float) sr));
    }

    int done = 0;

    while (done < numSamples)
    {
        const auto chunk = juce::jmin (controlInterval, numSamples - done);

        // ---- control-rate modulation
        v.hz += (v.targetHz - v.hz) * glideCoeff;
        v.pitchEnv *= pitchEnvCoeff;

        float sources[(int) ModSource::count] {};

        for (int l = 0; l < numLfos; ++l)
        {
            const auto& lfo = s.lfo[l];
            const auto hz = lfoBaseHz[l] * std::exp2 (juce::jlimit (-6.0f, 6.0f, v.lfoRateMod[l] * 4.0f));
            double phase;

            if (lfo.mode == LfoMode::free)
            {
                phase = wrap (freeLfoPhase[l] + lfoBaseHz[l] * (offset + done) / sr);
            }
            else
            {
                phase = v.lfoDone[l] ? 0.99999 : v.lfoPhase[l];
                const auto next = v.lfoPhase[l] + hz * chunk / sr;
                if (lfo.mode == LfoMode::envelope && next >= 1.0)
                    v.lfoDone[l] = true;
                v.lfoPhase[l] = wrap (next);
            }

            v.lfoSmoothed[l] += (lfoValue (lfo, l, phase) - v.lfoSmoothed[l]) * lfoSmoothCoeff[l];
            sources[(int) ModSource::lfo1 + l] = v.lfoSmoothed[l];
        }

        sources[(int) ModSource::env2] = v.env[0].getNextSample();
        sources[(int) ModSource::env3] = v.env[1].getNextSample();
        sources[(int) ModSource::ampEnv] = v.amp.isActive() ? 1.0f : 0.0f;
        for (int m = 0; m < numMacros; ++m)
            sources[(int) ModSource::macro1 + m] = s.macros[m];
        sources[(int) ModSource::velocity] = v.velocity;
        sources[(int) ModSource::note] = (float) (v.note - 60) / 60.0f;
        sources[(int) ModSource::modWheel] = modWheel;
        sources[(int) ModSource::aftertouch] = aftertouch;
        sources[(int) ModSource::random] = v.randomValue;

        float mod[(int) ModDest::count] {};
        for (const auto& slot : s.slots)
        {
            if (slot.source == ModSource::none || slot.dest == ModDest::none)
                continue;

            auto value = sources[(int) slot.source];
            if (slot.bipolar && slot.source != ModSource::note)
                value = value * 2.0f - 1.0f;
            if (slot.aux != ModSource::none)
                value *= sources[(int) slot.aux];
            mod[(int) slot.dest] += value * slot.amount;
        }

        for (int l = 0; l < numLfos; ++l)
            v.lfoRateMod[l] = mod[(int) ModDest::lfo1Rate + l];

        if (traced)
            for (int d = 0; d < numFxDests; ++d)
                std::fill_n (fxTraces[(size_t) d].begin() + offset + done, chunk, mod[firstFxDest + d]);

        const auto baseHz = v.hz * semitonesToRatio (bendSemitones + s.masterTune + mod[(int) ModDest::masterPitch] * 24.0
                                                     + (s.pitchEnvSemis + mod[(int) ModDest::pitchEnvAmount] * 48.0f) * v.pitchEnv);

        // Oscillator setup: pitch, unison spread and gains, band-limit level, modulated position and warps.
        double incs[numOscs][maxUnison] {};
        float gainsL[numOscs][maxUnison] {}, gainsR[numOscs][maxUnison] {};
        int levels[numOscs] {}, unison[numOscs] {};
        float positions[numOscs] {}, warps[numOscs][2] {};
        bool useUser[numOscs] {}, oscOn[numOscs] {};

        for (int o = 0; o < numOscs; ++o)
        {
            const auto& osc = s.osc[o];
            const auto base = (int) ModDest::posA + o * 7; // pos, warp1, warp2, level, pitch, pan, detune
            const auto level = osc.on ? juce::jlimit (0.0f, 1.0f, osc.level + mod[base + 3]) : 0.0f;
            oscOn[o] = osc.on;
            if (! osc.on)
                continue;

            const auto oscHz = baseHz * semitonesToRatio (osc.octave * 12.0 + osc.semitone + osc.fine / 100.0 + mod[base + 4] * 24.0);
            const auto n = juce::jlimit (1, maxUnison, osc.unison);
            const auto detune = juce::jlimit (0.0f, 1.0f, osc.detune + mod[base + 6]);
            const auto pan = juce::jlimit (-1.0f, 1.0f, osc.pan + mod[base + 5]);
            unison[o] = n;

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
                incs[o][u] = oscHz * semitonesToRatio (spread * detune * 0.75) / sr;
                const auto voicePan = juce::jlimit (-1.0f, 1.0f, spread * osc.width + pan);
                gainsL[o][u] = 1.0f - voicePan;
                gainsR[o][u] = 1.0f + voicePan;
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
                warps[o][w] = juce::jlimit (0.0f, 1.0f, osc.warp[w] + mod[base + 1 + w]);
                if (osc.warpMode[w] != Warp::off)
                    brightness *= warpBrightness (osc.warpMode[w], warps[o][w]);
            }

            levels[o] = WavetableBank::levelForFrequency (oscHz * brightness * semitonesToRatio (detune * 0.75), sr);
            positions[o] = juce::jlimit (0.0f, 1.0f, osc.position + mod[base]);
            useUser[o] = osc.table >= numTables && user[o] != nullptr;
        }

        const auto subLevel = s.subOn ? juce::jlimit (0.0f, 1.0f, s.subLevel + mod[(int) ModDest::subLevel]) : 0.0f;
        const auto noiseLevel = juce::jlimit (0.0f, 1.0f, s.noiseLevel + mod[(int) ModDest::noiseLevel]);
        const auto noiseColour = juce::jlimit (0.0f, 1.0f, s.noiseColour + mod[(int) ModDest::noiseColour]);
        auto needNoise = noiseLevel > 0.0f;
        for (int o = 0; o < numOscs; ++o)
            needNoise = needNoise || (oscOn[o] && s.osc[o].warpSource == WarpSource::noise);

        const auto subInc = baseHz * std::exp2 ((double) juce::jlimit (-2, 0, s.subOctave)) / sr;

        // Filters.
        bool filterOn[2];
        for (int f = 0; f < 2; ++f)
        {
            const auto& filter = s.filter[f];
            const auto base = f == 0 ? (int) ModDest::cutoff : (int) ModDest::cutoff2;
            filterOn[f] = filter.on;
            if (! filter.on)
                continue;

            const auto keyTrack = std::pow ((float) (v.hz / 261.63), filter.keyTrack);
            FilterParams params;
            params.type = filter.type;
            params.cutoff = juce::jlimit (8.0f, (float) sr * 0.45f, filter.cutoff * keyTrack * std::exp2 (mod[base] * 8.0f));
            params.resonance = juce::jlimit (0.0f, 1.0f, filter.resonance + mod[base + 1]);
            params.drive = juce::jlimit (0.0f, 1.0f, filter.drive + mod[base + 2]);
            params.var = juce::jlimit (0.0f, 1.0f, filter.var + mod[base + 3]);
            params.mix = juce::jlimit (0.0f, 1.0f, filter.mix + mod[base + 4]);
            v.filters[f][0].setup (params);
            v.filters[f][1].setup (params);
        }

        const auto volume = juce::jlimit (0.0f, 2.0f, 1.0f + mod[(int) ModDest::volume]);

        auto tableSample = [&] (int o, double phase)
        {
            return useUser[o] ? user[o]->sample (levels[o], positions[o], phase)
                              : bank->sample (juce::jlimit (0, numTables - 1, s.osc[o].table), levels[o], positions[o], phase);
        };

        auto routeInto = [] (Route route, float (&buses)[4][2], float l, float r)
        {
            buses[(int) route][0] += l;
            buses[(int) route][1] += r;
        };

        // ---- audio-rate rendering
        for (int i = 0; i < chunk; ++i)
        {
            float sub = 0.0f;
            switch (s.subShape)
            {
                case SubShape::sine:     sub = (float) std::sin (twoPi * v.subPhase); break;
                case SubShape::triangle: sub = (float) (v.subPhase < 0.5 ? 4.0 * v.subPhase - 1.0 : 3.0 - 4.0 * v.subPhase); break;
                case SubShape::saw:      sub = (float) (2.0 * v.subPhase - 1.0); break;
                case SubShape::square:   sub = v.subPhase < 0.5 ? 0.8f : -0.8f; break;
                case SubShape::count:    break;
            }
            v.subPhase = wrap (v.subPhase + subInc);

            const auto noise = needNoise ? nextNoise (v, s.noiseType, noiseColour, baseHz) : 0.0f;

            float buses[4][2] {}; // filter 1, filter 2, both, direct

            // C, then B, then A, so A's warps hear this sample of B and C.
            for (int o = numOscs - 1; o >= 0; --o)
            {
                if (! oscOn[o])
                {
                    v.lastOsc[o] = 0.0f;
                    continue;
                }

                const auto& osc = s.osc[o];
                float other = 0.0f;
                switch (osc.warpSource)
                {
                    case WarpSource::a:     other = v.lastOsc[0]; break;
                    case WarpSource::b:     other = v.lastOsc[1]; break;
                    case WarpSource::c:     other = v.lastOsc[2]; break;
                    case WarpSource::sub:   other = sub; break;
                    case WarpSource::noise: other = noise; break;
                    case WarpSource::count: break;
                }

                float mono = 0.0f, outL = 0.0f, outR = 0.0f;
                const auto n = unison[o];

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
                    outL += smp * gainsL[o][u];
                    outR += smp * gainsR[o][u];
                    v.phases[o][u] = wrap (v.phases[o][u] + incs[o][u]);
                }

                v.lastOsc[o] = mono / (float) n;
                routeInto (osc.route, buses, outL, outR);
            }

            if (noiseLevel > 0.0f)
                routeInto (s.noiseRoute, buses, noise * noiseLevel * 0.5f, noise * noiseLevel * 0.5f);
            if (s.subRoute != Route::direct)
                routeInto (s.subRoute, buses, sub * subLevel, sub * subLevel);

            float out[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                if (s.filtersParallel)
                {
                    auto a = buses[0][ch] + 0.5f * buses[2][ch];
                    auto b = buses[1][ch] + 0.5f * buses[2][ch];
                    if (filterOn[0]) a = v.filters[0][ch].process (a);
                    if (filterOn[1]) b = v.filters[1][ch].process (b);
                    out[ch] = a + b;
                }
                else
                {
                    auto x = buses[0][ch] + buses[2][ch];
                    if (filterOn[0]) x = v.filters[0][ch].process (x);
                    x += buses[1][ch];
                    if (filterOn[1]) x = v.filters[1][ch].process (x);
                    out[ch] = x;
                }

                out[ch] += buses[3][ch];
            }

            // The sub stays clean and centred unless it is routed into a filter.
            const auto directSub = s.subRoute == Route::direct ? sub * subLevel : 0.0f;
            const auto gain = v.amp.getNextSample() * volume * 0.5f;
            left[done + i]  += (out[0] + directSub) * gain;
            right[done + i] += (out[1] + directSub) * gain;
        }

        done += chunk;
    }

    if (! v.amp.isActive())
    {
        v.active = false;
        v.note = -1;
    }
}
