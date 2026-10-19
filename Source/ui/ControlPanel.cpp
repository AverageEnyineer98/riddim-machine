#include "ControlPanel.h"
#include "../Parameters.h"
#include "../dsp/RiddimEngine.h"

namespace
{
    constexpr int titleHeight = 20;
    constexpr int stripHeight = 30;
    constexpr int labelHeight = 14;
    constexpr int gap = 8;

    const juce::Colour oscColour    { 0xffb84dff };
    const juce::Colour sourceColour { 0xff7a6bff };
    const juce::Colour filterColour { 0xff33e0ff };
    const juce::Colour ampColour    { 0xff4dffa6 };
    const juce::Colour lfoColour    { 0xffff4dd2 };
    const juce::Colour fxColour     { 0xffff8a3d };
    const juce::Colour voiceColour  { 0xffffd24d };
    const juce::Colour combColour   { 0xffff6b6b };
    const juce::Colour dynamicsColour { 0xffff4d88 };
    const juce::Colour heatColour   { 0xffffb03d };
}

//==============================================================================
Knob::Knob (APVTS& state, const juce::String& paramID, const juce::String& name, juce::Colour colour)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      attachment (state, paramID, slider)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramID)->convertFrom0to1 (
                                                state.getParameter (paramID)->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (11.5f));
    label.setColour (juce::Label::textColourId, Palette::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromBottom (labelHeight));
    const auto size = juce::jmin (area.getWidth(), area.getHeight(), 62);
    slider.setBounds (area.withSizeKeepingCentre (size, size));
}

//==============================================================================
Section::Section (const juce::String& t, juce::Colour c) : title (t), colour (c) {}

void Section::addKnob (APVTS& state, const juce::String& paramID, const juce::String& name)
{
    knobs.push_back (std::make_unique<Knob> (state, paramID, name, colour));
    addAndMakeVisible (*knobs.back());
}

void Section::addToStrip (juce::Component& c, float weight)
{
    strip.emplace_back (&c, weight);
    addAndMakeVisible (c);
}

void Section::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (colour);
    g.fillRoundedRectangle (10.0f, 8.0f, 3.0f, 10.0f, 1.5f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), 18, 4, getWidth() - 24, titleHeight - 2, juce::Justification::centredLeft);
}

void Section::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    area.removeFromTop (titleHeight);

    if (! strip.empty())
    {
        auto stripArea = area.removeFromTop (stripHeight).reduced (2, 3);
        float total = 0.0f;
        for (const auto& [c, w] : strip)
            total += w;

        const auto width = (float) stripArea.getWidth() - (float) (strip.size() - 1) * 4.0f;
        for (const auto& [c, w] : strip)
        {
            c->setBounds (stripArea.removeFromLeft (juce::roundToInt (width * w / total)));
            stripArea.removeFromLeft (4);
        }
    }

    if (knobs.empty())
        return;

    const auto knobWidth = area.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
        k->setBounds (area.removeFromLeft (knobWidth));
}

//==============================================================================
WavetablePreview::WavetablePreview (APVTS& state, char oscillator, juce::Colour c)
    : table (*state.getRawParameterValue (ParamID::osc (oscillator, ParamID::table))),
      position (*state.getRawParameterValue (ParamID::osc (oscillator, ParamID::pos))),
      colour (c)
{
    startTimerHz (30);
}

void WavetablePreview::timerCallback()
{
    const auto t = juce::roundToInt (table.load());
    const auto f = juce::roundToInt (position.load() * (float) (WavetableBank::numFrames - 1));

    if (t != lastTable || f != lastFrame)
    {
        lastTable = t;
        lastFrame = f;
        repaint();
    }
}

void WavetablePreview::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (bounds, 5.0f);

    if (lastTable < 0)
        return;

    const auto* frame = bank->getFrame (juce::jlimit (0, bank->getNumTables() - 1, lastTable),
                                        juce::jlimit (0, WavetableBank::numFrames - 1, lastFrame));
    const auto area = bounds.reduced (4.0f, 3.0f);
    constexpr int points = 128;

    juce::Path path;
    for (int i = 0; i < points; ++i)
    {
        const auto value = frame[i * WavetableBank::frameSize / points];
        const auto x = area.getX() + area.getWidth() * (float) i / (float) (points - 1);
        const auto y = area.getCentreY() - value * area.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

//==============================================================================
LfoDisplay::LfoDisplay (APVTS& state, VisualFeed& f)
    : shape (*state.getRawParameterValue (ParamID::lfoShape)), feed (f)
{
    startTimerHz (40);
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (bounds, 5.0f);

    const auto area = bounds.reduced (5.0f, 4.0f);
    const auto s = juce::roundToInt (shape.load());
    auto pointAt = [&] (double phase)
    {
        return juce::Point<float> (area.getX() + area.getWidth() * (float) phase,
                                   area.getBottom() - area.getHeight() * lfoShapeValue (s, juce::jmin (phase, 0.9999)));
    };

    juce::Path path;
    constexpr int points = 96;
    for (int i = 0; i < points; ++i)
    {
        const auto p = pointAt ((double) i / (double) (points - 1));
        if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
    }

    g.setColour (lfoColour.withAlpha (0.8f));
    g.strokePath (path, juce::PathStrokeType (1.5f));

    const auto dot = pointAt (feed.getLfoPhase());
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
}

//==============================================================================
ParamMenu::ParamMenu (APVTS& state, const juce::String& paramID, const juce::StringArray& items)
    : attachment (state, paramID, (box.addItemList (items, 1), box))
{
}

ParamToggle::ParamToggle (APVTS& state, const juce::String& paramID, const juce::String& text)
    : attachment (state, paramID, (button.setButtonText (text), button))
{
}

void layoutSectionRow (juce::Rectangle<int> row, std::initializer_list<Section*> sections)
{
    int totalKnobs = 0;
    for (auto* s : sections)
        totalKnobs += s->getNumKnobs();

    const auto available = row.getWidth() - gap * ((int) sections.size() - 1);
    for (auto* s : sections)
    {
        s->setBounds (row.removeFromLeft (available * s->getNumKnobs() / totalKnobs));
        row.removeFromLeft (gap);
    }
}

//==============================================================================
FxPanel::FxPanel (APVTS& s, Variant v)
    : distortion ("Distortion", fxColour), formant ("Formant", lfoColour), comb ("Comb", combColour), dynamics ("Smash / Clip", dynamicsColour),
      tone ("Tone / Low End", filterColour), macro ("Heat", heatColour),
      distType (s, ParamID::distType, Choices::distTypes),
      resample (s, ParamID::resample, "RESAMPLE"),
      gate (s, ParamID::gate, "GATE")
{
    distortion.addToStrip (distType.box, 1.0f);
    distortion.addKnob (s, ParamID::distDrive, "Drive");
    distortion.addKnob (s, ParamID::distMix, "Mix");

    formant.addKnob (s, ParamID::vowelMix, "Mix");
    formant.addKnob (s, ParamID::vowel, "Vowel");

    if (v == Variant::fxPlugin)
    {
        // The FX plugin's own tempo-synced movement LFO sweeps the vowel.
        movementShape = std::make_unique<ParamMenu> (s, ParamID::fxLfoShape, Choices::lfoShapes);
        movementRate = std::make_unique<ParamMenu> (s, ParamID::fxLfoRate, Choices::lfoRates);
        formant.addToStrip (movementShape->box, 1.3f);
        formant.addToStrip (movementRate->box, 1.0f);
        formant.addKnob (s, ParamID::fxLfoVowel, "Movement");
    }

    if (v == Variant::synth)
    {
        combTrack = std::make_unique<ParamToggle> (s, ParamID::combTrack, "KEY TRACK");
        combTrack->button.setTooltip ("Tune the comb to the note being played (Freq is ignored)");
        comb.addToStrip (combTrack->button, 1.0f);
    }

    comb.addKnob (s, ParamID::combMix, "Mix");
    comb.addKnob (s, ParamID::combFreq, "Freq");
    comb.addKnob (s, ParamID::combFeedback, "Feedback");

    dynamics.addKnob (s, ParamID::squash, "Smash");
    dynamics.addKnob (s, ParamID::clip, "Clip");

    tone.addKnob (s, ParamID::tilt, "Tilt");
    tone.addKnob (s, ParamID::lowKeep, "Low Keep");

    resample.button.setTooltip ("Run distortion and smash twice, like bouncing and re-processing the bass");
    gate.button.setTooltip ("Silence the output between hits so the gaps stay tight");
    macro.addToStrip (resample.button, 1.2f);
    macro.addToStrip (gate.button, 0.8f);
    macro.addKnob (s, ParamID::heat, "Heat");
    macro.addKnob (s, ParamID::fxMix, "FX Mix");
    macro.addKnob (s, ParamID::master, "Output");

    for (auto* c : { &distortion, &formant, &comb, &dynamics, &tone, &macro })
        addAndMakeVisible (c);
}

void FxPanel::resized()
{
    layoutSectionRow (getLocalBounds(), { &distortion, &formant, &comb, &dynamics, &tone, &macro });
}

//==============================================================================
ControlPanel::ControlPanel (APVTS& s, VisualFeed& feed)
    : oscA ("Osc A", oscColour), oscB ("Osc B", oscColour), source ("Sub / FM", sourceColour),
      filter ("Filter", filterColour), amp ("Amp Env", ampColour), lfo ("Wobble LFO", lfoColour),
      voice ("Voice", voiceColour),
      tableA (s, ParamID::osc ('A', ParamID::table), WavetableBank::getTableNames()),
      tableB (s, ParamID::osc ('B', ParamID::table), WavetableBank::getTableNames()),
      warpA (s, ParamID::osc ('A', ParamID::warpMode), Choices::warpModes),
      warpB (s, ParamID::osc ('B', ParamID::warpMode), Choices::warpModes),
      filterType (s, ParamID::filterType, Choices::filterTypes),
      lfoShape (s, ParamID::lfoShape, Choices::lfoShapes),
      lfoRate (s, ParamID::lfoRate, Choices::lfoRates),
      lfoRetrig (s, ParamID::lfoRetrig, "RETRIG"),
      mono (s, ParamID::mono, "MONO"),
      previewA (s, 'A', oscColour), previewB (s, 'B', oscColour),
      lfoDisplay (s, feed),
      fxPanel (s, FxPanel::Variant::synth)
{
    for (auto [section, which, menu, warpMenu, preview] : { std::tuple { &oscA, 'A', &tableA, &warpA, &previewA },
                                                            std::tuple { &oscB, 'B', &tableB, &warpB, &previewB } })
    {
        section->addToStrip (menu->box, 1.3f);
        section->addToStrip (warpMenu->box, 0.8f);
        section->addToStrip (*preview, 1.0f);
        section->addKnob (s, ParamID::osc (which, ParamID::pos), "Position");
        section->addKnob (s, ParamID::osc (which, ParamID::level), "Level");
        section->addKnob (s, ParamID::osc (which, ParamID::octave), "Octave");
        section->addKnob (s, ParamID::osc (which, ParamID::semi), "Semi");
        section->addKnob (s, ParamID::osc (which, ParamID::unison), "Unison");
        section->addKnob (s, ParamID::osc (which, ParamID::detune), "Detune");
        section->addKnob (s, ParamID::osc (which, ParamID::warp), "Warp");
    }

    source.addKnob (s, ParamID::subLevel, "Sub");
    source.addKnob (s, ParamID::fmAmount, "FM B>A");
    source.addKnob (s, ParamID::noiseLevel, "Noise");

    filter.addToStrip (filterType.box, 1.0f);
    filter.addKnob (s, ParamID::cutoff, "Cutoff");
    filter.addKnob (s, ParamID::resonance, "Reso");
    filter.addKnob (s, ParamID::filterEnv, "Env Amt");
    filter.addKnob (s, ParamID::filterDecay, "Env Decay");

    amp.addKnob (s, ParamID::attack, "Attack");
    amp.addKnob (s, ParamID::decay, "Decay");
    amp.addKnob (s, ParamID::sustain, "Sustain");
    amp.addKnob (s, ParamID::release, "Release");

    lfo.addToStrip (lfoShape.box, 1.3f);
    lfo.addToStrip (lfoRate.box, 0.8f);
    lfo.addToStrip (lfoRetrig.button, 0.8f);
    lfo.addToStrip (lfoDisplay, 1.4f);
    lfo.addKnob (s, ParamID::lfoCutoff, "> Cutoff");
    lfo.addKnob (s, ParamID::lfoPosA, "> Pos A");
    lfo.addKnob (s, ParamID::lfoPosB, "> Pos B");
    lfo.addKnob (s, ParamID::lfoFm, "> FM");
    lfo.addKnob (s, ParamID::lfoVolume, "> Volume");
    lfo.addKnob (s, ParamID::lfoVowel, "> Vowel");
    lfo.addKnob (s, ParamID::lfoWarpA, "> Warp A");

    voice.addToStrip (mono.button, 1.0f);
    voice.addKnob (s, ParamID::glide, "Glide");
    voice.addKnob (s, ParamID::bendRange, "Bend");
    voice.addKnob (s, ParamID::pitchEnv, "Pitch Env");
    voice.addKnob (s, ParamID::pitchDecay, "P. Decay");

    for (auto* c : std::initializer_list<juce::Component*> { &oscA, &oscB, &source, &filter, &amp, &lfo, &voice, &fxPanel })
        addAndMakeVisible (c);
}

void ControlPanel::resized()
{
    auto area = getLocalBounds();
    const auto rowHeight = (area.getHeight() - gap * 2) / 3;

    layoutSectionRow (area.removeFromTop (rowHeight), { &oscA, &oscB, &source });
    area.removeFromTop (gap);
    layoutSectionRow (area.removeFromTop (rowHeight), { &filter, &amp, &lfo, &voice });
    area.removeFromTop (gap);
    fxPanel.setBounds (area);
}
