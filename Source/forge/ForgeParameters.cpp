#include "ForgeParameters.h"
#include "../Parameters.h"
#include "../ParameterHelpers.h"

using namespace ParamHelpers;

const juce::StringArray& forgeTableNames()
{
    static const auto names = []
    {
        auto list = WavetableBank::getTableNames();
        list.add ("Imported");
        return list;
    }();
    return names;
}

namespace
{
    void addOscillator (Layout& layout, char which, bool on, int defaultTable)
    {
        const auto prefix = juce::String ("Osc ") + which + " ";
        addBool (layout, ForgeID::osc (which, ForgeID::on), prefix + "On", on);
        addChoice (layout, ForgeID::osc (which, ForgeID::table), prefix + "Wavetable", forgeTableNames(), defaultTable);
        addFloat (layout, ForgeID::osc (which, ForgeID::pos), prefix + "Position", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, ForgeID::osc (which, ForgeID::level), prefix + "Level", { 0.0f, 1.0f }, 0.8f);
        addInt (layout, ForgeID::osc (which, ForgeID::octave), prefix + "Octave", -3, 3, 0);
        addInt (layout, ForgeID::osc (which, ForgeID::semi), prefix + "Semitone", -12, 12, 0);
        addFloat (layout, ForgeID::osc (which, ForgeID::fine), prefix + "Fine", { -100.0f, 100.0f }, 0.0f, "ct");
        addInt (layout, ForgeID::osc (which, ForgeID::unison), prefix + "Unison", 1, Forge::maxUnison, 1);
        addFloat (layout, ForgeID::osc (which, ForgeID::detune), prefix + "Detune", { 0.0f, 1.0f }, 0.2f);
        addFloat (layout, ForgeID::osc (which, ForgeID::blend), prefix + "Blend", { 0.0f, 1.0f }, 0.5f);
        addFloat (layout, ForgeID::osc (which, ForgeID::width), prefix + "Width", { 0.0f, 1.0f }, 0.8f);
        addChoice (layout, ForgeID::osc (which, ForgeID::warp1Mode), prefix + "Warp 1 Mode", Forge::warpNames(), 0);
        addFloat (layout, ForgeID::osc (which, ForgeID::warp1), prefix + "Warp 1", { 0.0f, 1.0f }, 0.0f);
        addChoice (layout, ForgeID::osc (which, ForgeID::warp2Mode), prefix + "Warp 2 Mode", Forge::warpNames(), 0);
        addFloat (layout, ForgeID::osc (which, ForgeID::warp2), prefix + "Warp 2", { 0.0f, 1.0f }, 0.0f);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createForgeParameterLayout()
{
    Layout layout;

    addOscillator (layout, 'A', true, 0);
    addOscillator (layout, 'B', false, 4);

    addFloat (layout, ForgeID::subLevel, "Sub Level", { 0.0f, 1.0f }, 0.5f);
    addChoice (layout, ForgeID::subOctave, "Sub Octave", { "0", "-1" }, 0);
    addFloat (layout, ForgeID::noise, "Noise", { 0.0f, 1.0f }, 0.0f);

    juce::NormalisableRange<float> cutoffRange { 20.0f, 20000.0f };
    cutoffRange.setSkewForCentre (800.0f);
    addChoice (layout, ForgeID::filterType, "Filter Type", Forge::filterTypeNames(), 0);
    addFloat (layout, ForgeID::cutoff, "Cutoff", cutoffRange, 20000.0f, "Hz");
    addFloat (layout, ForgeID::resonance, "Resonance", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::drive, "Filter Drive", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::keyTrack, "Key Track", { 0.0f, 1.0f }, 0.0f);

    addFloat (layout, ForgeID::attack, "Attack", timeRange (0.5f, 3000.0f), 1.0f, "ms");
    addFloat (layout, ForgeID::decay, "Decay", timeRange (5.0f, 3000.0f), 200.0f, "ms");
    addFloat (layout, ForgeID::sustain, "Sustain", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ForgeID::release, "Release", timeRange (5.0f, 5000.0f), 25.0f, "ms");
    addFloat (layout, ForgeID::env2Attack, "Env 2 Attack", timeRange (0.5f, 3000.0f), 1.0f, "ms");
    addFloat (layout, ForgeID::env2Decay, "Env 2 Decay", timeRange (5.0f, 3000.0f), 300.0f, "ms");
    addFloat (layout, ForgeID::env2Sustain, "Env 2 Sustain", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::env2Release, "Env 2 Release", timeRange (5.0f, 5000.0f), 100.0f, "ms");

    for (int l = 0; l < 2; ++l)
    {
        const auto prefix = "LFO " + juce::String (l + 1) + " ";
        addChoice (layout, ForgeID::lfo (l, ForgeID::shape), prefix + "Shape", Choices::lfoShapes, 0);
        addChoice (layout, ForgeID::lfo (l, ForgeID::rate), prefix + "Rate", Choices::lfoRates, l == 0 ? 5 : 2);
        addBool (layout, ForgeID::lfo (l, ForgeID::retrig), prefix + "Retrigger", true);
    }

    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        const auto prefix = "Mod " + juce::String (i + 1) + " ";
        addChoice (layout, ForgeID::slot (i, ForgeID::source), prefix + "Source", Forge::modSourceNames(), 0);
        addChoice (layout, ForgeID::slot (i, ForgeID::dest), prefix + "Destination", Forge::modDestNames(), 0);
        addFloat (layout, ForgeID::slot (i, ForgeID::amount), prefix + "Amount", { -1.0f, 1.0f }, 0.0f);
    }

    addBool (layout, ForgeID::hitSeqOn, "Hit Sequencer", false);
    addInt (layout, ForgeID::hitSeqLen, "Hit Sequencer Length", 1, Forge::numHitSteps, 4);
    const Forge::Settings defaults;
    for (int i = 0; i < Forge::numHitSteps; ++i)
        addChoice (layout, ForgeID::hitRate (i), "Hit " + juce::String (i + 1) + " Rate", Choices::lfoRates, defaults.hitSeqRates[i]);

    addFloat (layout, ForgeID::pitchEnv, "Pitch Env", { -48.0f, 48.0f }, 0.0f, "st");
    addFloat (layout, ForgeID::pitchDecay, "Pitch Decay", timeRange (1.0f, 500.0f), 30.0f, "ms");
    addBool (layout, ForgeID::mono, "Mono", true);
    addFloat (layout, ForgeID::glide, "Glide", timeRange (0.0f, 1000.0f), 0.0f, "ms");
    addInt (layout, ForgeID::bendRange, "Bend Range", 1, 24, 12);

    addFxParameters (layout, -6.0f);
    addBool (layout, ParamID::combTrack, "Comb Key Track", true);
    return layout;
}

ForgeSettingsReader::ForgeSettingsReader (const juce::AudioProcessorValueTreeState& state)
{
    auto find = [&state] (const juce::String& id)
    {
        auto* value = state.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };

    for (int o = 0; o < 2; ++o)
    {
        const auto which = o == 0 ? 'A' : 'B';
        auto& p = osc[o];
        p.on       = find (ForgeID::osc (which, ForgeID::on));
        p.table    = find (ForgeID::osc (which, ForgeID::table));
        p.pos      = find (ForgeID::osc (which, ForgeID::pos));
        p.level    = find (ForgeID::osc (which, ForgeID::level));
        p.octave   = find (ForgeID::osc (which, ForgeID::octave));
        p.semi     = find (ForgeID::osc (which, ForgeID::semi));
        p.fine     = find (ForgeID::osc (which, ForgeID::fine));
        p.unison   = find (ForgeID::osc (which, ForgeID::unison));
        p.detune   = find (ForgeID::osc (which, ForgeID::detune));
        p.blend    = find (ForgeID::osc (which, ForgeID::blend));
        p.width    = find (ForgeID::osc (which, ForgeID::width));
        p.warpMode[0] = find (ForgeID::osc (which, ForgeID::warp1Mode));
        p.warp[0]     = find (ForgeID::osc (which, ForgeID::warp1));
        p.warpMode[1] = find (ForgeID::osc (which, ForgeID::warp2Mode));
        p.warp[1]     = find (ForgeID::osc (which, ForgeID::warp2));
    }

    for (int l = 0; l < 2; ++l)
        lfo[l] = { find (ForgeID::lfo (l, ForgeID::shape)), find (ForgeID::lfo (l, ForgeID::rate)), find (ForgeID::lfo (l, ForgeID::retrig)) };

    for (int i = 0; i < Forge::numModSlots; ++i)
        slots[i] = { find (ForgeID::slot (i, ForgeID::source)), find (ForgeID::slot (i, ForgeID::dest)), find (ForgeID::slot (i, ForgeID::amount)) };

    for (int i = 0; i < Forge::numHitSteps; ++i)
        hitRates[i] = find (ForgeID::hitRate (i));

    subLevel    = find (ForgeID::subLevel);
    subOctave   = find (ForgeID::subOctave);
    noise       = find (ForgeID::noise);
    filterType  = find (ForgeID::filterType);
    cutoff      = find (ForgeID::cutoff);
    resonance   = find (ForgeID::resonance);
    drive       = find (ForgeID::drive);
    keyTrack    = find (ForgeID::keyTrack);
    attack      = find (ForgeID::attack);
    decay       = find (ForgeID::decay);
    sustain     = find (ForgeID::sustain);
    release     = find (ForgeID::release);
    env2Attack  = find (ForgeID::env2Attack);
    env2Decay   = find (ForgeID::env2Decay);
    env2Sustain = find (ForgeID::env2Sustain);
    env2Release = find (ForgeID::env2Release);
    hitSeqOn    = find (ForgeID::hitSeqOn);
    hitSeqLen   = find (ForgeID::hitSeqLen);
    pitchEnv    = find (ForgeID::pitchEnv);
    pitchDecay  = find (ForgeID::pitchDecay);
    mono        = find (ForgeID::mono);
    glide       = find (ForgeID::glide);
    bendRange   = find (ForgeID::bendRange);
}

Forge::Settings ForgeSettingsReader::read() const
{
    auto get = [] (Value v) { return v->load(); };
    auto getInt = [] (Value v) { return juce::roundToInt (v->load()); };
    auto getBool = [] (Value v) { return v->load() > 0.5f; };

    Forge::Settings s;

    for (int o = 0; o < 2; ++o)
    {
        const auto& p = osc[o];
        auto& out = s.osc[o];
        out.on       = getBool (p.on);
        out.table    = getInt (p.table);
        out.position = get (p.pos);
        out.level    = get (p.level);
        out.octave   = getInt (p.octave);
        out.semitone = getInt (p.semi);
        out.fine     = get (p.fine);
        out.unison   = getInt (p.unison);
        out.detune   = get (p.detune);
        out.blend    = get (p.blend);
        out.width    = get (p.width);
        for (int w = 0; w < 2; ++w)
        {
            out.warpMode[w] = (Forge::Warp) getInt (p.warpMode[w]);
            out.warp[w]     = get (p.warp[w]);
        }
    }

    s.subLevel   = get (subLevel);
    s.subOctave  = getInt (subOctave) == 1 ? -1 : 0;
    s.noiseLevel = get (noise);

    s.filterType = (Forge::FilterType) getInt (filterType);
    s.cutoff     = get (cutoff);
    s.resonance  = get (resonance);
    s.drive      = get (drive);
    s.keyTrack   = get (keyTrack);

    s.attackMs      = get (attack);
    s.decayMs       = get (decay);
    s.sustain       = get (sustain);
    s.releaseMs     = get (release);
    s.env2AttackMs  = get (env2Attack);
    s.env2DecayMs   = get (env2Decay);
    s.env2Sustain   = get (env2Sustain);
    s.env2ReleaseMs = get (env2Release);

    for (int l = 0; l < 2; ++l)
    {
        s.lfo[l].shape     = getInt (lfo[l].shape);
        s.lfo[l].rate      = getInt (lfo[l].rate);
        s.lfo[l].retrigger = getBool (lfo[l].retrig);
    }

    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        s.slots[i].source = (Forge::ModSource) getInt (slots[i].source);
        s.slots[i].dest   = (Forge::ModDest) getInt (slots[i].dest);
        s.slots[i].amount = get (slots[i].amount);
    }

    s.hitSeqOn     = getBool (hitSeqOn);
    s.hitSeqLength = getInt (hitSeqLen);
    for (int i = 0; i < Forge::numHitSteps; ++i)
        s.hitSeqRates[i] = getInt (hitRates[i]);

    s.pitchEnvSemis = get (pitchEnv);
    s.pitchDecayMs  = get (pitchDecay);
    s.mono          = getBool (mono);
    s.glideMs       = get (glide);
    s.bendRange     = getInt (bendRange);
    return s;
}
