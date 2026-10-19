#include "Parameters.h"
#include "dsp/Wavetables.h"
#include "ParameterHelpers.h"

namespace
{
    using namespace ParamHelpers;

    void addOscillator (Layout& layout, char which, int defaultTable, float defaultLevel)
    {
        const auto prefix = juce::String ("Osc ") + which + " ";
        addChoice (layout, ParamID::osc (which, ParamID::table), prefix + "Wavetable", WavetableBank::getTableNames(), defaultTable);
        addFloat (layout, ParamID::osc (which, ParamID::pos), prefix + "Position", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, ParamID::osc (which, ParamID::level), prefix + "Level", { 0.0f, 1.0f }, defaultLevel);
        addInt (layout, ParamID::osc (which, ParamID::octave), prefix + "Octave", -3, 3, 0);
        addInt (layout, ParamID::osc (which, ParamID::semi), prefix + "Semitone", -12, 12, 0);
        addInt (layout, ParamID::osc (which, ParamID::unison), prefix + "Unison", 1, 5, 1);
        addFloat (layout, ParamID::osc (which, ParamID::detune), prefix + "Detune", { 0.0f, 1.0f }, 0.2f);
        addChoice (layout, ParamID::osc (which, ParamID::warpMode), prefix + "Warp Mode", Choices::warpModes, 0);
        addFloat (layout, ParamID::osc (which, ParamID::warp), prefix + "Warp", { 0.0f, 1.0f }, 0.0f);
    }
}

void addFxParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout, float defaultOutputDb)
{
    using namespace ParamHelpers;

    juce::NormalisableRange<float> combRange { 40.0f, 2000.0f };
    combRange.setSkewForCentre (250.0f);

    addChoice (layout, ParamID::distType, "Distortion", Choices::distTypes, 0);
    addFloat (layout, ParamID::distDrive, "Drive", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ParamID::distMix, "Dist Mix", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ParamID::vowelMix, "Formant Mix", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::vowel, "Vowel", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::combMix, "Comb Mix", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::combFreq, "Comb Freq", combRange, 220.0f, "Hz");
    addFloat (layout, ParamID::combFeedback, "Comb Feedback", { 0.0f, 0.95f }, 0.6f);
    addFloat (layout, ParamID::squash, "Smash", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ParamID::lowKeep, "Low Keep", { 0.0f, 300.0f }, 110.0f, "Hz");
    addFloat (layout, ParamID::tilt, "Tilt", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::clip, "Clip", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ParamID::heat, "Heat", { 0.0f, 1.0f }, 0.0f);
    addBool (layout, ParamID::resample, "Resample", false);
    addBool (layout, ParamID::gate, "Gate", true);
    addFloat (layout, ParamID::fxMix, "FX Mix", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ParamID::master, "Output", { -24.0f, 6.0f }, defaultOutputDb, "dB");
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout layout;

    addOscillator (layout, 'A', 0, 0.8f);
    addOscillator (layout, 'B', 1, 0.0f);

    addFloat (layout, ParamID::fmAmount, "FM B > A", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::subLevel, "Sub Level", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ParamID::noiseLevel, "Noise", { 0.0f, 1.0f }, 0.0f);

    juce::NormalisableRange<float> cutoffRange { 20.0f, 20000.0f };
    cutoffRange.setSkewForCentre (800.0f);
    addChoice (layout, ParamID::filterType, "Filter Type", Choices::filterTypes, 0);
    addFloat (layout, ParamID::cutoff, "Cutoff", cutoffRange, 1200.0f, "Hz");
    addFloat (layout, ParamID::resonance, "Resonance", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ParamID::filterEnv, "Filter Env", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::filterDecay, "Filter Decay", timeRange (5.0f, 3000.0f), 300.0f, "ms");

    addFloat (layout, ParamID::attack, "Attack", timeRange (0.5f, 3000.0f), 2.0f, "ms");
    addFloat (layout, ParamID::decay, "Decay", timeRange (5.0f, 3000.0f), 200.0f, "ms");
    addFloat (layout, ParamID::sustain, "Sustain", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ParamID::release, "Release", timeRange (5.0f, 5000.0f), 40.0f, "ms");

    addChoice (layout, ParamID::lfoShape, "LFO Shape", Choices::lfoShapes, 0);
    addChoice (layout, ParamID::lfoRate, "LFO Rate", Choices::lfoRates, 4);
    addBool (layout, ParamID::lfoRetrig, "LFO Retrigger", true);
    addFloat (layout, ParamID::lfoCutoff, "LFO > Cutoff", { -1.0f, 1.0f }, 0.6f);
    addFloat (layout, ParamID::lfoPosA, "LFO > Pos A", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::lfoPosB, "LFO > Pos B", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::lfoFm, "LFO > FM", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::lfoVolume, "LFO > Volume", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::lfoVowel, "LFO > Vowel", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::lfoWarpA, "LFO > Warp A", { -1.0f, 1.0f }, 0.0f);
    addFloat (layout, ParamID::pitchEnv, "Pitch Env", { -48.0f, 48.0f }, 0.0f, "st");
    addFloat (layout, ParamID::pitchDecay, "Pitch Decay", timeRange (1.0f, 500.0f), 40.0f, "ms");
    addBool (layout, ParamID::combTrack, "Comb Key Track", true);

    addBool (layout, ParamID::mono, "Mono", true);
    addFloat (layout, ParamID::glide, "Glide", timeRange (0.0f, 1000.0f), 0.0f, "ms");
    addInt (layout, ParamID::bendRange, "Bend Range", 1, 24, 12);

    addFxParameters (layout, -6.0f);
    return layout;
}

juce::AudioProcessorValueTreeState::ParameterLayout createFxParameterLayout()
{
    Layout layout;
    addFxParameters (layout, 0.0f);

    // The synth's LFO drives the formant; the FX plugin gets its own tempo-synced one.
    addChoice (layout, ParamID::fxLfoShape, "Movement Shape", Choices::lfoShapes, 1);
    addChoice (layout, ParamID::fxLfoRate, "Movement Rate", Choices::lfoRates, 5);
    addFloat (layout, ParamID::fxLfoVowel, "Movement > Vowel", { -1.0f, 1.0f }, 0.0f);
    return layout;
}

FxSettings readFxSettings (const juce::AudioProcessorValueTreeState& state)
{
    auto get = [&state] (const char* id) { return state.getRawParameterValue (id)->load(); };

    FxSettings s;
    s.distType     = juce::roundToInt (get (ParamID::distType));
    s.distDrive    = get (ParamID::distDrive);
    s.distMix      = get (ParamID::distMix);
    s.vowelMix     = get (ParamID::vowelMix);
    s.vowel        = get (ParamID::vowel);
    s.combMix      = get (ParamID::combMix);
    s.combHz       = get (ParamID::combFreq);
    s.combFeedback = get (ParamID::combFeedback);
    s.smash        = get (ParamID::squash);
    s.lowKeepHz    = get (ParamID::lowKeep);
    s.tilt         = get (ParamID::tilt);
    s.clip         = get (ParamID::clip);
    s.heat         = get (ParamID::heat);
    s.resample     = get (ParamID::resample) > 0.5f;
    s.gate         = get (ParamID::gate) > 0.5f;
    s.mix          = get (ParamID::fxMix);
    s.outputDb     = get (ParamID::master);
    return s;
}
