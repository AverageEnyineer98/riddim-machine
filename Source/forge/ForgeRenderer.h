#pragma once

#include "../dsp/RiddimFx.h"
#include "ForgeEngine.h"
#include "ForgeParameters.h"
#include "ForgeRack.h"

// Everything that turns MIDI into Riddim Forge's sound: the synth engine, the riddim chain
// and the FX rack, with the mod matrix driving the effects. The plugin, the render test and
// the preset tools all play through this, so they sound the same.
class ForgeRenderer
{
public:
    explicit ForgeRenderer (const juce::AudioProcessorValueTreeState&);

    void prepare (double sampleRate, int maxBlockSize);
    int getLatencySamples() const { return fx.getLatencySamples(); }

    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, double bpm, std::optional<double> ppqPosition);

    // Not owned; must outlive the renderer or be replaced first.
    void setUserTable (int oscillator, const UserWavetable* table) noexcept { engine.setUserTable (oscillator, table); }

    // Message thread: renders a drawn curve and hands it to the audio thread.
    void setLfoCurve (int lfo, const Forge::LfoCurve&);

    ForgeEngine& getEngine() noexcept { return engine; }
    const ForgeEngine& getEngine() const noexcept { return engine; }

private:
    void applyModulation (FxSettings&, Forge::RackSettings&, int sample) const;

    const juce::AudioProcessorValueTreeState& parameters;
    ForgeSettingsReader reader;
    ForgeEngine engine;
    RiddimFx fx;
    Forge::Rack rack;

    std::array<std::array<std::array<float, Forge::lfoTableSize>, 2>, Forge::numLfos> lfoTables {};
    int liveLfoTable[Forge::numLfos] {};
};
