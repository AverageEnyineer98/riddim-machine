#include "ForgeParameters.h"
#include "../Parameters.h"
#include "../ParameterHelpers.h"

using namespace ParamHelpers;
using namespace Forge;

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

const juce::StringArray& forgeDelayTimeNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/1" };
    return names;
}

const double* forgeDelayTimeBeats()
{
    static const double beats[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats;
}

const juce::StringArray& forgeTempoModeNames()
{
    static const juce::StringArray names { "Tempo: Auto", "Tempo: Song", "Tempo: x2", "Tempo: x1/2" };
    return names;
}

double forgeTempoFactor (int tempoMode, double hostBpm)
{
    switch (tempoMode)
    {
        case 0:  return hostBpm < 100.0 ? 2.0 : 1.0;
        case 2:  return 2.0;
        case 3:  return 0.5;
        default: return 1.0;
    }
}

const juce::StringArray& forgeEqShapeNames()
{
    static const juce::StringArray names { "Low Shelf", "Peak", "High Shelf" };
    return names;
}

namespace
{
    juce::NormalisableRange<float> hzRange (float low, float high, float centre)
    {
        juce::NormalisableRange<float> r { low, high };
        r.setSkewForCentre (centre);
        return r;
    }

    void addOscillator (Layout& layout, int index)
    {
        const auto which = ForgeID::oscLetter (index);
        const auto prefix = juce::String ("Osc ") + which + " ";
        auto id = [which] (const char* name) { return ForgeID::osc (which, name); };

        addBool (layout, id (ForgeID::on), prefix + "On", index == 0);
        addChoice (layout, id (ForgeID::table), prefix + "Wavetable", forgeTableNames(), 0);
        addFloat (layout, id (ForgeID::pos), prefix + "Position", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, id (ForgeID::level), prefix + "Level", { 0.0f, 1.0f }, 0.75f);
        addFloat (layout, id (ForgeID::pan), prefix + "Pan", { -1.0f, 1.0f }, 0.0f);
        addInt (layout, id (ForgeID::octave), prefix + "Octave", -4, 4, 0);
        addInt (layout, id (ForgeID::semi), prefix + "Semitone", -24, 24, 0);
        addFloat (layout, id (ForgeID::fine), prefix + "Fine", { -100.0f, 100.0f }, 0.0f, "ct");
        addInt (layout, id (ForgeID::unison), prefix + "Unison", 1, Forge::maxUnison, 1);
        addFloat (layout, id (ForgeID::detune), prefix + "Detune", { 0.0f, 1.0f }, 0.2f);
        addFloat (layout, id (ForgeID::blend), prefix + "Blend", { 0.0f, 1.0f }, 0.5f);
        addFloat (layout, id (ForgeID::width), prefix + "Width", { 0.0f, 1.0f }, 0.8f);
        addFloat (layout, id (ForgeID::randPhase), prefix + "Random Phase", { 0.0f, 1.0f }, 0.0f);
        addChoice (layout, id (ForgeID::warp1Mode), prefix + "Warp 1 Mode", warpNames(), 0);
        addFloat (layout, id (ForgeID::warp1), prefix + "Warp 1", { 0.0f, 1.0f }, 0.0f);
        addChoice (layout, id (ForgeID::warp2Mode), prefix + "Warp 2 Mode", warpNames(), 0);
        addFloat (layout, id (ForgeID::warp2), prefix + "Warp 2", { 0.0f, 1.0f }, 0.0f);
        addChoice (layout, id (ForgeID::warpSrc), prefix + "Warp Source", warpSourceNames(), index == 1 ? 0 : 1);
        addChoice (layout, id (ForgeID::route), prefix + "Route", routeNames(), 0);
    }

    void addFilter (Layout& layout, int index)
    {
        const auto prefix = "Filter " + juce::String (index + 1) + " ";
        auto id = [index] (const char* name) { return ForgeID::filter (index, name); };

        addBool (layout, id (ForgeID::on), prefix + "On", index == 0);
        addChoice (layout, id (ForgeID::type), prefix + "Type", filterTypeNames(), 0);
        addFloat (layout, id (ForgeID::cutoffName), prefix + "Cutoff", hzRange (20.0f, 20000.0f, 800.0f), 20000.0f, "Hz");
        addFloat (layout, id (ForgeID::res), prefix + "Resonance", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, id (ForgeID::driveName), prefix + "Drive", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, id (ForgeID::var), prefix + "Var", { 0.0f, 1.0f }, 0.5f);
        addFloat (layout, id (ForgeID::keytrack), prefix + "Key Track", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, id (ForgeID::mix), prefix + "Mix", { 0.0f, 1.0f }, 1.0f);
    }

    //==========================================================================
    // Visits every synth setting in a fixed order: f (makeId, target). makeId is only called
    // when collecting, so reading on the audio thread builds no strings.
    template <typename F>
    void visitSettings (Settings& s, F&& f)
    {
        #define FORGE_P(idExpression, target) f ([&] { return juce::String (idExpression); }, target)

        for (int o = 0; o < numOscs; ++o)
        {
            const auto w = ForgeID::oscLetter (o);
            auto& osc = s.osc[o];
            FORGE_P (ForgeID::osc (w, ForgeID::on), osc.on);
            FORGE_P (ForgeID::osc (w, ForgeID::table), osc.table);
            FORGE_P (ForgeID::osc (w, ForgeID::pos), osc.position);
            FORGE_P (ForgeID::osc (w, ForgeID::level), osc.level);
            FORGE_P (ForgeID::osc (w, ForgeID::pan), osc.pan);
            FORGE_P (ForgeID::osc (w, ForgeID::octave), osc.octave);
            FORGE_P (ForgeID::osc (w, ForgeID::semi), osc.semitone);
            FORGE_P (ForgeID::osc (w, ForgeID::fine), osc.fine);
            FORGE_P (ForgeID::osc (w, ForgeID::unison), osc.unison);
            FORGE_P (ForgeID::osc (w, ForgeID::detune), osc.detune);
            FORGE_P (ForgeID::osc (w, ForgeID::blend), osc.blend);
            FORGE_P (ForgeID::osc (w, ForgeID::width), osc.width);
            FORGE_P (ForgeID::osc (w, ForgeID::randPhase), osc.randomPhase);
            FORGE_P (ForgeID::osc (w, ForgeID::warp1Mode), osc.warpMode[0]);
            FORGE_P (ForgeID::osc (w, ForgeID::warp1), osc.warp[0]);
            FORGE_P (ForgeID::osc (w, ForgeID::warp2Mode), osc.warpMode[1]);
            FORGE_P (ForgeID::osc (w, ForgeID::warp2), osc.warp[1]);
            FORGE_P (ForgeID::osc (w, ForgeID::warpSrc), osc.warpSource);
            FORGE_P (ForgeID::osc (w, ForgeID::route), osc.route);
        }

        FORGE_P (ForgeID::subOn, s.subOn);
        FORGE_P (ForgeID::subLevel, s.subLevel);
        FORGE_P (ForgeID::subOctave, s.subOctave);
        FORGE_P (ForgeID::subShape, s.subShape);
        FORGE_P (ForgeID::subRoute, s.subRoute);
        FORGE_P (ForgeID::noise, s.noiseLevel);
        FORGE_P (ForgeID::noiseColour, s.noiseColour);
        FORGE_P (ForgeID::noiseType, s.noiseType);
        FORGE_P (ForgeID::noiseRoute, s.noiseRoute);

        for (int i = 0; i < 2; ++i)
        {
            auto& flt = s.filter[i];
            FORGE_P (ForgeID::filter (i, ForgeID::on), flt.on);
            FORGE_P (ForgeID::filter (i, ForgeID::type), flt.type);
            FORGE_P (ForgeID::filter (i, ForgeID::cutoffName), flt.cutoff);
            FORGE_P (ForgeID::filter (i, ForgeID::res), flt.resonance);
            FORGE_P (ForgeID::filter (i, ForgeID::driveName), flt.drive);
            FORGE_P (ForgeID::filter (i, ForgeID::var), flt.var);
            FORGE_P (ForgeID::filter (i, ForgeID::keytrack), flt.keyTrack);
            FORGE_P (ForgeID::filter (i, ForgeID::mix), flt.mix);
        }
        FORGE_P (ForgeID::filtersParallel, s.filtersParallel);

        FORGE_P (ForgeID::attack, s.attackMs);
        FORGE_P (ForgeID::decay, s.decayMs);
        FORGE_P (ForgeID::sustain, s.sustain);
        FORGE_P (ForgeID::release, s.releaseMs);
        for (int e = 0; e < 2; ++e)
        {
            FORGE_P (ForgeID::env (e, ForgeID::envAttack), s.envAttackMs[e]);
            FORGE_P (ForgeID::env (e, ForgeID::envDecay), s.envDecayMs[e]);
            FORGE_P (ForgeID::env (e, ForgeID::envSustain), s.envSustain[e]);
            FORGE_P (ForgeID::env (e, ForgeID::envRelease), s.envReleaseMs[e]);
        }

        for (int l = 0; l < numLfos; ++l)
        {
            auto& lfo = s.lfo[l];
            FORGE_P (ForgeID::lfo (l, ForgeID::shape), lfo.shape);
            FORGE_P (ForgeID::lfo (l, ForgeID::sync), lfo.sync);
            FORGE_P (ForgeID::lfo (l, ForgeID::rate), lfo.rate);
            FORGE_P (ForgeID::lfo (l, ForgeID::hz), lfo.hz);
            FORGE_P (ForgeID::lfo (l, ForgeID::mode), lfo.mode);
            FORGE_P (ForgeID::lfo (l, ForgeID::smooth), lfo.smooth);
        }

        for (int m = 0; m < numMacros; ++m)
            FORGE_P (ForgeID::macro (m), s.macros[m]);

        for (int i = 0; i < numModSlots; ++i)
        {
            auto& slot = s.slots[i];
            FORGE_P (ForgeID::slot (i, ForgeID::source), slot.source);
            FORGE_P (ForgeID::slot (i, ForgeID::aux), slot.aux);
            FORGE_P (ForgeID::slot (i, ForgeID::dest), slot.dest);
            FORGE_P (ForgeID::slot (i, ForgeID::amount), slot.amount);
            FORGE_P (ForgeID::slot (i, ForgeID::bipolar), slot.bipolar);
        }

        FORGE_P (ForgeID::hitSeqOn, s.hitSeqOn);
        FORGE_P (ForgeID::hitSeqLen, s.hitSeqLength);
        for (int i = 0; i < numHitSteps; ++i)
            FORGE_P (ForgeID::hitRate (i), s.hitSeqRates[i]);

        FORGE_P (ForgeID::pitchEnv, s.pitchEnvSemis);
        FORGE_P (ForgeID::pitchDecay, s.pitchDecayMs);
        FORGE_P (ForgeID::mono, s.mono);
        FORGE_P (ForgeID::legato, s.legato);
        FORGE_P (ForgeID::glide, s.glideMs);
        FORGE_P (ForgeID::bendRange, s.bendRange);
        FORGE_P (ForgeID::polyphony, s.polyphony);
        FORGE_P (ForgeID::masterTune, s.masterTune);
    }

    template <typename F>
    void visitRack (RackSettings& r, F&& f)
    {
        FORGE_P (ForgeID::hyperOn, r.hyperOn);
        FORGE_P (ForgeID::hyperMix, r.hyperMix);
        FORGE_P (ForgeID::hyperDetune, r.hyperDetune);
        FORGE_P (ForgeID::hyperRate, r.hyperRate);
        FORGE_P (ForgeID::hyperUnison, r.hyperUnison);
        FORGE_P (ForgeID::dimSize, r.dimensionSize);
        FORGE_P (ForgeID::dimMix, r.dimensionMix);

        FORGE_P (ForgeID::fxfOn, r.filterOn);
        FORGE_P (ForgeID::fxfType, r.filter.type);
        FORGE_P (ForgeID::fxfCutoff, r.filter.cutoff);
        FORGE_P (ForgeID::fxfRes, r.filter.resonance);
        FORGE_P (ForgeID::fxfDrive, r.filter.drive);
        FORGE_P (ForgeID::fxfVar, r.filter.var);
        FORGE_P (ForgeID::fxfMix, r.filter.mix);

        FORGE_P (ForgeID::eqOn, r.eqOn);
        FORGE_P (ForgeID::eq1Freq, r.eqFreq[0]);
        FORGE_P (ForgeID::eq1Gain, r.eqGain[0]);
        FORGE_P (ForgeID::eq1Q, r.eqQ[0]);
        FORGE_P (ForgeID::eq1Shape, r.eqShape[0]);
        FORGE_P (ForgeID::eq2Freq, r.eqFreq[1]);
        FORGE_P (ForgeID::eq2Gain, r.eqGain[1]);
        FORGE_P (ForgeID::eq2Q, r.eqQ[1]);
        FORGE_P (ForgeID::eq2Shape, r.eqShape[1]);

        FORGE_P (ForgeID::bodeOn, r.bodeOn);
        FORGE_P (ForgeID::bodeShift, r.bodeShift);
        FORGE_P (ForgeID::bodeFeedback, r.bodeFeedback);
        FORGE_P (ForgeID::bodeMix, r.bodeMix);

        FORGE_P (ForgeID::chorusOn, r.chorusOn);
        FORGE_P (ForgeID::chorusRate, r.chorusRate);
        FORGE_P (ForgeID::chorusDepth, r.chorusDepth);
        FORGE_P (ForgeID::chorusDelay, r.chorusDelayMs);
        FORGE_P (ForgeID::chorusFeedback, r.chorusFeedback);
        FORGE_P (ForgeID::chorusMix, r.chorusMix);

        FORGE_P (ForgeID::phaserOn, r.phaserOn);
        FORGE_P (ForgeID::phaserRate, r.phaserRate);
        FORGE_P (ForgeID::phaserDepth, r.phaserDepth);
        FORGE_P (ForgeID::phaserFreq, r.phaserFreq);
        FORGE_P (ForgeID::phaserFeedback, r.phaserFeedback);
        FORGE_P (ForgeID::phaserMix, r.phaserMix);

        FORGE_P (ForgeID::flangerOn, r.flangerOn);
        FORGE_P (ForgeID::flangerRate, r.flangerRate);
        FORGE_P (ForgeID::flangerDepth, r.flangerDepth);
        FORGE_P (ForgeID::flangerFeedback, r.flangerFeedback);
        FORGE_P (ForgeID::flangerMix, r.flangerMix);

        FORGE_P (ForgeID::delayOn, r.delayOn);
        FORGE_P (ForgeID::delayTime, r.delayBeats);
        FORGE_P (ForgeID::delayFeedback, r.delayFeedback);
        FORGE_P (ForgeID::delayMix, r.delayMix);
        FORGE_P (ForgeID::delayTone, r.delayTone);
        FORGE_P (ForgeID::delayPingPong, r.delayPingPong);

        FORGE_P (ForgeID::reverbOn, r.reverbOn);
        FORGE_P (ForgeID::reverbSize, r.reverbSize);
        FORGE_P (ForgeID::reverbDamping, r.reverbDamping);
        FORGE_P (ForgeID::reverbWidth, r.reverbWidth);
        FORGE_P (ForgeID::reverbMix, r.reverbMix);

        #undef FORGE_P
    }

    struct Assign
    {
        const std::vector<std::atomic<float>*>& values;
        size_t index = 0;

        template <typename MakeId, typename T>
        void operator() (MakeId&&, T& target)
        {
            const auto v = values[index++]->load();
            if constexpr (std::is_same_v<T, bool>)         target = v > 0.5f;
            else if constexpr (std::is_same_v<T, float>)   target = v;
            else                                           target = static_cast<T> (juce::roundToInt (v));
        }
    };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createForgeParameterLayout()
{
    Layout layout;

    for (int o = 0; o < numOscs; ++o)
        addOscillator (layout, o);

    addBool (layout, ForgeID::subOn, "Sub On", true);
    addFloat (layout, ForgeID::subLevel, "Sub Level", { 0.0f, 1.0f }, 0.5f);
    addChoice (layout, ForgeID::subOctave, "Sub Octave", { "0", "-1", "-2" }, 0);
    addChoice (layout, ForgeID::subShape, "Sub Shape", subShapeNames(), 0);
    addChoice (layout, ForgeID::subRoute, "Sub Route", routeNames(), (int) Route::direct);
    addFloat (layout, ForgeID::noise, "Noise", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::noiseColour, "Noise Colour", { 0.0f, 1.0f }, 0.5f);
    addChoice (layout, ForgeID::noiseType, "Noise Type", noiseTypeNames(), 0);
    addChoice (layout, ForgeID::noiseRoute, "Noise Route", routeNames(), 0);

    addFilter (layout, 0);
    addFilter (layout, 1);
    addBool (layout, ForgeID::filtersParallel, "Filters Parallel", false);

    addFloat (layout, ForgeID::attack, "Attack", timeRange (0.5f, 3000.0f), 1.0f, "ms");
    addFloat (layout, ForgeID::decay, "Decay", timeRange (5.0f, 3000.0f), 200.0f, "ms");
    addFloat (layout, ForgeID::sustain, "Sustain", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ForgeID::release, "Release", timeRange (5.0f, 5000.0f), 25.0f, "ms");
    for (int e = 0; e < 2; ++e)
    {
        const auto prefix = "Env " + juce::String (e + 2) + " ";
        addFloat (layout, ForgeID::env (e, ForgeID::envAttack), prefix + "Attack", timeRange (0.5f, 3000.0f), 1.0f, "ms");
        addFloat (layout, ForgeID::env (e, ForgeID::envDecay), prefix + "Decay", timeRange (5.0f, 3000.0f), 300.0f, "ms");
        addFloat (layout, ForgeID::env (e, ForgeID::envSustain), prefix + "Sustain", { 0.0f, 1.0f }, 0.0f);
        addFloat (layout, ForgeID::env (e, ForgeID::envRelease), prefix + "Release", timeRange (5.0f, 5000.0f), 100.0f, "ms");
    }

    for (int l = 0; l < numLfos; ++l)
    {
        const auto prefix = "LFO " + juce::String (l + 1) + " ";
        addChoice (layout, ForgeID::lfo (l, ForgeID::shape), prefix + "Shape", lfoShapeNames(), 0);
        addBool (layout, ForgeID::lfo (l, ForgeID::sync), prefix + "Sync", true);
        addChoice (layout, ForgeID::lfo (l, ForgeID::rate), prefix + "Rate", lfoRateNames(), l == 0 ? 11 : 7);
        addFloat (layout, ForgeID::lfo (l, ForgeID::hz), prefix + "Hz", hzRange (0.01f, 40.0f, 2.0f), 2.0f, "Hz");
        addChoice (layout, ForgeID::lfo (l, ForgeID::mode), prefix + "Mode", lfoModeNames(), 0);
        addFloat (layout, ForgeID::lfo (l, ForgeID::smooth), prefix + "Smooth", { 0.0f, 1.0f }, 0.0f);
    }

    for (int m = 0; m < numMacros; ++m)
        addFloat (layout, ForgeID::macro (m), "Macro " + juce::String (m + 1), { 0.0f, 1.0f }, 0.0f);

    for (int i = 0; i < numModSlots; ++i)
    {
        const auto prefix = "Mod " + juce::String (i + 1) + " ";
        addChoice (layout, ForgeID::slot (i, ForgeID::source), prefix + "Source", modSourceNames(), 0);
        addChoice (layout, ForgeID::slot (i, ForgeID::aux), prefix + "Aux", modSourceNames(), 0);
        addChoice (layout, ForgeID::slot (i, ForgeID::dest), prefix + "Destination", modDestNames(), 0);
        addFloat (layout, ForgeID::slot (i, ForgeID::amount), prefix + "Amount", { -1.0f, 1.0f }, 0.0f);
        addBool (layout, ForgeID::slot (i, ForgeID::bipolar), prefix + "Bipolar", false);
    }

    addBool (layout, ForgeID::hitSeqOn, "Hit Sequencer", false);
    addInt (layout, ForgeID::hitSeqLen, "Hit Sequencer Length", 1, numHitSteps, 4);
    const Settings defaults;
    for (int i = 0; i < numHitSteps; ++i)
        addChoice (layout, ForgeID::hitRate (i), "Hit " + juce::String (i + 1) + " Rate", lfoRateNames(), defaults.hitSeqRates[i]);

    addFloat (layout, ForgeID::pitchEnv, "Pitch Env", { -48.0f, 48.0f }, 0.0f, "st");
    addFloat (layout, ForgeID::pitchDecay, "Pitch Decay", timeRange (1.0f, 500.0f), 30.0f, "ms");
    addBool (layout, ForgeID::mono, "Mono", true);
    addBool (layout, ForgeID::legato, "Legato", false);
    addFloat (layout, ForgeID::glide, "Glide", timeRange (0.0f, 1000.0f), 0.0f, "ms");
    addInt (layout, ForgeID::bendRange, "Bend Range", 1, 24, 12);
    addInt (layout, ForgeID::polyphony, "Polyphony", 1, ForgeEngine::maxVoices, 8);
    addFloat (layout, ForgeID::masterTune, "Master Tune", { -12.0f, 12.0f }, 0.0f, "st");
    addChoice (layout, ForgeID::tempoMode, "Tempo Mode", forgeTempoModeNames(), 0);

    // FX rack.
    addBool (layout, ForgeID::hyperOn, "Hyper On", false);
    addFloat (layout, ForgeID::hyperMix, "Hyper Mix", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ForgeID::hyperDetune, "Hyper Detune", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ForgeID::hyperRate, "Hyper Rate", { 0.0f, 1.0f }, 0.4f);
    addInt (layout, ForgeID::hyperUnison, "Hyper Unison", 1, 8, 4);
    addFloat (layout, ForgeID::dimSize, "Dimension Size", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ForgeID::dimMix, "Dimension Mix", { 0.0f, 1.0f }, 0.0f);

    addBool (layout, ForgeID::fxfOn, "FX Filter On", false);
    addChoice (layout, ForgeID::fxfType, "FX Filter Type", filterTypeNames(), 0);
    addFloat (layout, ForgeID::fxfCutoff, "FX Filter Cutoff", hzRange (20.0f, 20000.0f, 800.0f), 2000.0f, "Hz");
    addFloat (layout, ForgeID::fxfRes, "FX Filter Resonance", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ForgeID::fxfDrive, "FX Filter Drive", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::fxfVar, "FX Filter Var", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ForgeID::fxfMix, "FX Filter Mix", { 0.0f, 1.0f }, 1.0f);

    addBool (layout, ForgeID::eqOn, "EQ On", false);
    addFloat (layout, ForgeID::eq1Freq, "EQ 1 Freq", hzRange (20.0f, 20000.0f, 600.0f), 150.0f, "Hz");
    addFloat (layout, ForgeID::eq1Gain, "EQ 1 Gain", { -24.0f, 24.0f }, 0.0f, "dB");
    addFloat (layout, ForgeID::eq1Q, "EQ 1 Q", { 0.2f, 10.0f }, 0.7f);
    addChoice (layout, ForgeID::eq1Shape, "EQ 1 Shape", forgeEqShapeNames(), 0);
    addFloat (layout, ForgeID::eq2Freq, "EQ 2 Freq", hzRange (20.0f, 20000.0f, 600.0f), 3000.0f, "Hz");
    addFloat (layout, ForgeID::eq2Gain, "EQ 2 Gain", { -24.0f, 24.0f }, 0.0f, "dB");
    addFloat (layout, ForgeID::eq2Q, "EQ 2 Q", { 0.2f, 10.0f }, 0.7f);
    addChoice (layout, ForgeID::eq2Shape, "EQ 2 Shape", forgeEqShapeNames(), 2);

    addBool (layout, ForgeID::bodeOn, "Shift On", false);
    addFloat (layout, ForgeID::bodeShift, "Shift Amount", { -1.0f, 1.0f }, 0.1f);
    addFloat (layout, ForgeID::bodeFeedback, "Shift Feedback", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ForgeID::bodeMix, "Shift Mix", { 0.0f, 1.0f }, 0.5f);

    addBool (layout, ForgeID::chorusOn, "Chorus On", false);
    addFloat (layout, ForgeID::chorusRate, "Chorus Rate", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ForgeID::chorusDepth, "Chorus Depth", { 0.0f, 1.0f }, 0.3f);
    addFloat (layout, ForgeID::chorusDelay, "Chorus Delay", { 1.0f, 30.0f }, 8.0f, "ms");
    addFloat (layout, ForgeID::chorusFeedback, "Chorus Feedback", { 0.0f, 1.0f }, 0.0f);
    addFloat (layout, ForgeID::chorusMix, "Chorus Mix", { 0.0f, 1.0f }, 0.4f);

    addBool (layout, ForgeID::phaserOn, "Phaser On", false);
    addFloat (layout, ForgeID::phaserRate, "Phaser Rate", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ForgeID::phaserDepth, "Phaser Depth", { 0.0f, 1.0f }, 0.6f);
    addFloat (layout, ForgeID::phaserFreq, "Phaser Freq", hzRange (40.0f, 12000.0f, 800.0f), 800.0f, "Hz");
    addFloat (layout, ForgeID::phaserFeedback, "Phaser Feedback", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ForgeID::phaserMix, "Phaser Mix", { 0.0f, 1.0f }, 0.6f);

    addBool (layout, ForgeID::flangerOn, "Flanger On", false);
    addFloat (layout, ForgeID::flangerRate, "Flanger Rate", { 0.0f, 1.0f }, 0.15f);
    addFloat (layout, ForgeID::flangerDepth, "Flanger Depth", { 0.0f, 1.0f }, 0.6f);
    addFloat (layout, ForgeID::flangerFeedback, "Flanger Feedback", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ForgeID::flangerMix, "Flanger Mix", { 0.0f, 1.0f }, 0.5f);

    addBool (layout, ForgeID::delayOn, "Delay On", false);
    addChoice (layout, ForgeID::delayTime, "Delay Time", forgeDelayTimeNames(), 6);
    addFloat (layout, ForgeID::delayFeedback, "Delay Feedback", { 0.0f, 0.95f }, 0.35f);
    addFloat (layout, ForgeID::delayMix, "Delay Mix", { 0.0f, 1.0f }, 0.2f);
    addFloat (layout, ForgeID::delayTone, "Delay Tone", { 0.0f, 1.0f }, 0.6f);
    addBool (layout, ForgeID::delayPingPong, "Delay Ping Pong", true);

    addBool (layout, ForgeID::reverbOn, "Reverb On", false);
    addFloat (layout, ForgeID::reverbSize, "Reverb Size", { 0.0f, 1.0f }, 0.35f);
    addFloat (layout, ForgeID::reverbDamping, "Reverb Damping", { 0.0f, 1.0f }, 0.5f);
    addFloat (layout, ForgeID::reverbWidth, "Reverb Width", { 0.0f, 1.0f }, 1.0f);
    addFloat (layout, ForgeID::reverbMix, "Reverb Mix", { 0.0f, 1.0f }, 0.2f);

    addFxParameters (layout, -6.0f);
    addBool (layout, ParamID::combTrack, "Comb Key Track", true);
    return layout;
}

//==============================================================================
ForgeSettingsReader::ForgeSettingsReader (const juce::AudioProcessorValueTreeState& s) : state (s)
{
    auto collect = [this] (auto&& makeId, auto&)
    {
        auto* value = state.getRawParameterValue (makeId());
        jassert (value != nullptr);
        values.push_back (value);
    };

    Settings settings;
    visitSettings (settings, collect);
    rackStart = values.size();
    RackSettings rack;
    visitRack (rack, collect);
}

Settings ForgeSettingsReader::read() const
{
    Settings s;
    Assign assign { values };
    visitSettings (s, assign);

    s.subOctave = -s.subOctave; // the choice lists 0, -1, -2
    return s;
}

RackSettings ForgeSettingsReader::readRack() const
{
    RackSettings r;
    Assign assign { values, rackStart };
    visitRack (r, assign);

    r.delayBeats = (float) forgeDelayTimeBeats()[juce::jlimit (0, forgeDelayTimeNames().size() - 1, juce::roundToInt (r.delayBeats))];
    return r;
}
