#include "ForgeUI.h"
#include "../dsp/RiddimEngine.h" // lfoShapeValue
#include "../Parameters.h"

using namespace ForgeUI;
using namespace ForgePalette;

namespace
{
    constexpr int headerHeight = 26;

    void drawScopeBackground (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (juce::Colour (0xff050a16));
        g.fillRoundedRectangle (area, 5.0f);

        g.setColour (glassEdge.withAlpha (0.45f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY() + 2.0f, area.getBottom() - 2.0f);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX() + 2.0f, area.getRight() - 2.0f);

        g.setColour (glassEdge);
        g.drawRoundedRectangle (area, 5.0f, 1.0f);
    }

    // A closed version of `line` down to the scope's floor, for the soft fill under a curve.
    void fillUnder (juce::Graphics& g, juce::Path line, juce::Rectangle<float> area, float baseline, juce::Colour colour)
    {
        line.lineTo (area.getRight(), baseline);
        line.lineTo (area.getX(), baseline);
        line.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, area.getY(),
                                                 colour.withAlpha (0.0f), 0.0f, area.getBottom(), false));
        g.fillPath (line);
    }
}

//==============================================================================
ForgeUI::Knob::Knob (APVTS& state, const juce::String& paramID, const juce::String& text, juce::Colour colour)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      attachment (state, paramID, slider)
{
    auto* parameter = state.getParameter (paramID);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setDoubleClickReturnValue (true, (double) parameter->convertFrom0to1 (parameter->getDefaultValue()));
    slider.setTooltip (parameter->getName (64) + "  (double-click to reset)");
    slider.onValueChange = [this] { updateReadout(); };
    addAndMakeVisible (slider);

    name.setText (text.toUpperCase(), juce::dontSendNotification);
    value.setColour (juce::Label::textColourId, colour.interpolatedWith (juce::Colours::white, 0.25f));

    for (auto* label : { &name, &value })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setInterceptsMouseClicks (false, false);
        label->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (*label);
    }

    name.setFont (mono (9.5f, true));
    name.setColour (juce::Label::textColourId, textDim);
    value.setFont (mono (9.5f));
    updateReadout();
}

void ForgeUI::Knob::updateReadout()
{
    value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
}

void ForgeUI::Knob::resized()
{
    // Dial, name and readout as one group, centred in whatever space the card gives.
    const auto size = juce::jmin (getWidth(), getHeight() - 24, 64);
    auto area = getLocalBounds().withSizeKeepingCentre (getWidth(), size + 26);
    slider.setBounds (area.removeFromTop (size).withSizeKeepingCentre (size, size));
    area.removeFromTop (2);
    name.setBounds (area.removeFromTop (12));
    value.setBounds (area.removeFromTop (12));
}

//==============================================================================
Card::Card (const juce::String& t, juce::Colour c) : title (t.toUpperCase()), accent (c) {}

juce::Rectangle<int> Card::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).reduced (10, 6);
}

void Card::resized()
{
    if (layout)
        layout (getContentBounds());
}

void Card::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xd00f1c3a), 0.0f, 0.0f,
                                             juce::Colour (0xd0060c1c), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    // Lit top edge fading out to the right.
    g.setGradientFill (juce::ColourGradient (accent, bounds.getX() + 10.0f, 0.0f,
                                             accent.withAlpha (0.0f), bounds.getWidth() * 0.75f, 0.0f, false));
    g.fillRect (bounds.getX() + 10.0f, bounds.getY(), bounds.getWidth() * 0.75f, 1.5f);

    // Corner brackets.
    const auto len = 9.0f;
    g.setColour (accent.withAlpha (0.65f));
    for (const auto corner : { bounds.getTopLeft(), bounds.getTopRight(), bounds.getBottomLeft(), bounds.getBottomRight() })
    {
        const auto dx = corner.x < bounds.getCentreX() ? 1.0f : -1.0f;
        const auto dy = corner.y < bounds.getCentreY() ? 1.0f : -1.0f;
        const auto c = corner + juce::Point<float> (dx * 3.0f, dy * 3.0f);
        g.drawLine (c.x, c.y, c.x + dx * len, c.y, 1.5f);
        g.drawLine (c.x, c.y, c.x, c.y + dy * len, 1.5f);
    }

    // Title with a diamond marker.
    juce::Path diamond;
    diamond.addPolygon ({ 18.0f, 13.0f }, 4, 4.0f, juce::MathConstants<float>::pi * 0.25f);
    g.setColour (accent);
    g.fillPath (diamond);
    g.setColour (accent.withAlpha (0.3f));
    g.strokePath (diamond, juce::PathStrokeType (4.0f));

    g.setColour (text);
    g.setFont (mono (11.0f, true).withExtraKerningFactor (0.18f));
    g.drawText (title, 28, 4, getWidth() - 36, 18, juce::Justification::centredLeft);
}

void ForgeUI::layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap)
{
    const auto n = (int) items.size();
    if (n == 0)
        return;

    const auto width = (area.getWidth() - gap * (n - 1)) / n;
    for (auto* item : items)
    {
        item->setBounds (area.removeFromLeft (width));
        area.removeFromLeft (gap);
    }
}

//==============================================================================
WarpScope::WarpScope (ForgeProcessor& p, char osc, juce::Colour c) : processor (p), oscIndex (osc == 'A' ? 0 : 1), colour (c)
{
    auto& state = p.parameters;
    table       = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::table));
    position    = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::pos));
    warpMode[0] = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::warp1Mode));
    warp[0]     = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::warp1));
    warpMode[1] = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::warp2Mode));
    warp[1]     = state.getRawParameterValue (ForgeID::osc (osc, ForgeID::warp2));
    startTimerHz (20);
}

void WarpScope::timerCallback()
{
    const auto userIndex = (float) (processor.getImportedTable (0) != nullptr) + (float) (processor.getImportedTable (1) != nullptr) * 2.0f;
    const std::array<float, 8> now { table->load(), position->load(), warpMode[0]->load(), warp[0]->load(),
                                     warpMode[1]->load(), warp[1]->load(), userIndex, 0.0f };
    if (now != last)
    {
        last = now;
        repaint();
    }
}

void WarpScope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area);

    const auto tableIndex = juce::roundToInt (table->load());
    const auto* user = tableIndex == Forge::userTable ? processor.getImportedTable (oscIndex) : nullptr;
    const auto builtIn = juce::jlimit (0, bank->getNumTables() - 1, tableIndex);

    constexpr int points = 240;
    const auto inner = area.reduced (6.0f, 8.0f);
    juce::Path line;

    for (int i = 0; i <= points; ++i)
    {
        auto phase = (double) i / points;
        auto gain = 1.0f;
        const auto sub = (float) std::sin (juce::MathConstants<double>::twoPi * phase);

        for (int w = 0; w < 2; ++w)
        {
            const auto mode = (Forge::Warp) juce::roundToInt (warpMode[w]->load());
            if (mode != Forge::Warp::off)
            {
                const auto r = Forge::warpPhase (mode, warp[w]->load(), phase, 0.0f, sub);
                phase = r.phase;
                gain *= r.gain;
            }
        }

        phase -= std::floor (phase);
        const auto smp = gain * (user != nullptr ? user->sample (0, position->load(), phase)
                                                 : bank->sample (builtIn, 0, position->load(), phase));
        const auto x = inner.getX() + inner.getWidth() * (float) i / points;
        const auto y = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, smp) * inner.getHeight() * 0.5f;

        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    fillUnder (g, line, area, inner.getCentreY(), colour);
    glowStroke (g, line, colour, 1.6f);

    g.setColour (textDim);
    g.setFont (mono (9.0f));
    g.drawText (user != nullptr ? user->getName().toUpperCase() : forgeTableNames()[builtIn].toUpperCase(),
                area.reduced (7.0f, 4.0f), juce::Justification::topLeft);
}

//==============================================================================
LfoScope::LfoScope (APVTS& state, int lfo, VisualFeed* f, juce::Colour c)
    : shape (*state.getRawParameterValue (ForgeID::lfo (lfo, ForgeID::shape))), feed (f), colour (c)
{
    startTimerHz (30);
}

void LfoScope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area);

    const auto inner = area.reduced (6.0f, 8.0f);
    const auto s = juce::roundToInt (shape.load());
    juce::Path line;
    constexpr int points = 200;

    for (int i = 0; i <= points; ++i)
    {
        const auto p = (double) i / points;
        const auto x = inner.getX() + inner.getWidth() * (float) p;
        const auto y = inner.getBottom() - lfoShapeValue (s, juce::jmin (p, 0.9999)) * inner.getHeight();
        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    fillUnder (g, line, area, inner.getBottom(), colour);
    glowStroke (g, line, colour, 1.6f);

    if (feed != nullptr)
    {
        const auto phase = feed->getLfoPhase();
        const auto x = inner.getX() + inner.getWidth() * phase;
        const auto y = inner.getBottom() - lfoShapeValue (s, phase) * inner.getHeight();
        g.setColour (colour.withAlpha (0.25f));
        g.drawVerticalLine ((int) x, area.getY() + 2.0f, area.getBottom() - 2.0f);
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }
}

//==============================================================================
EnvelopeScope::EnvelopeScope (APVTS& state, const char* a, const char* d, const char* s, const char* r, juce::Colour c)
    : attack (*state.getRawParameterValue (a)), decay (*state.getRawParameterValue (d)),
      sustain (*state.getRawParameterValue (s)), release (*state.getRawParameterValue (r)), colour (c)
{
    startTimerHz (15);
}

void EnvelopeScope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area);

    // Square-root time axis, so short riddim envelopes stay readable next to long ones.
    const auto a = std::sqrt (attack.load()), d = std::sqrt (decay.load()), r = std::sqrt (release.load());
    const auto hold = 0.25f * (a + d + r) + 1.0f;
    const auto total = a + d + hold + r;
    const auto inner = area.reduced (6.0f, 8.0f);
    auto xAt = [&] (float t) { return inner.getX() + inner.getWidth() * t / total; };
    const auto sustainY = inner.getBottom() - sustain.load() * inner.getHeight();

    juce::Path line;
    line.startNewSubPath (inner.getX(), inner.getBottom());
    line.lineTo (xAt (a), inner.getY());
    line.quadraticTo (xAt (a + d * 0.25f), sustainY, xAt (a + d), sustainY);
    line.lineTo (xAt (a + d + hold), sustainY);
    line.quadraticTo (xAt (a + d + hold + r * 0.25f), inner.getBottom(), xAt (total), inner.getBottom());

    fillUnder (g, line, area, inner.getBottom(), colour);
    glowStroke (g, line, colour, 1.6f);
}

//==============================================================================
HitSequencer::HitSequencer (ForgeProcessor& p)
    : processor (p), length (*p.parameters.getRawParameterValue (ForgeID::hitSeqLen))
{
    for (int i = 0; i < Forge::numHitSteps; ++i)
    {
        rates.push_back (std::make_unique<ParamMenu> (p.parameters, ForgeID::hitRate (i), Choices::lfoRates));
        rates.back()->box.setTooltip ("LFO 1 rate for hit " + juce::String (i + 1));
        addAndMakeVisible (rates.back()->box);
    }
    startTimerHz (20);
}

juce::Rectangle<int> HitSequencer::cellBounds (int step) const
{
    const auto width = getWidth() / Forge::numHitSteps;
    return { step * width + 3, 16, width - 6, getHeight() - 16 - 30 };
}

void HitSequencer::resized()
{
    const auto width = getWidth() / Forge::numHitSteps;
    for (int i = 0; i < Forge::numHitSteps; ++i)
        rates[(size_t) i]->box.setBounds (i * width + 2, getHeight() - 24, width - 4, 22);
}

void HitSequencer::timerCallback()
{
    const auto step = processor.getHitStep();
    const auto len = juce::roundToInt (length.load());
    if (step != shownStep || len != shownLength)
    {
        shownStep = step;
        shownLength = len;
        for (int i = 0; i < Forge::numHitSteps; ++i)
            rates[(size_t) i]->box.setAlpha (i < len ? 1.0f : 0.35f);
        repaint();
    }
}

void HitSequencer::paint (juce::Graphics& g)
{
    for (int i = 0; i < Forge::numHitSteps; ++i)
    {
        const auto cell = cellBounds (i).toFloat();
        const auto inUse = i < shownLength;
        const auto playing = inUse && i == shownStep;

        g.setColour (playing ? cyan : (inUse ? text : textDim.withAlpha (0.5f)));
        g.setFont (mono (9.5f, true));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), cell.withY (0.0f).withHeight (14.0f), juce::Justification::centred);

        if (playing)
        {
            g.setColour (cyan.withAlpha (0.18f));
            g.fillRoundedRectangle (cell.expanded (2.0f), 6.0f);
            g.setGradientFill (juce::ColourGradient (cyan, 0.0f, cell.getY(), violet, 0.0f, cell.getBottom(), false));
        }
        else
        {
            g.setColour (inUse ? juce::Colour (0xff10203c) : juce::Colour (0xff0a1222));
        }
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (inUse ? (playing ? cyan : glassEdge.brighter (0.3f)) : glassEdge.withAlpha (0.5f));
        g.drawRoundedRectangle (cell, 4.0f, 1.0f);

        // Rate as bars: faster rates draw more lines.
        if (inUse)
        {
            const auto rate = juce::roundToInt (processor.parameters.getRawParameterValue (ForgeID::hitRate (i))->load());
            const auto bars = juce::jlimit (1, 16, (int) (Choices::lfoCyclesPerBeat[rate] * 2.0));
            g.setColour (playing ? void_.withAlpha (0.8f) : cyan.withAlpha (0.55f));
            const auto inner = cell.reduced (6.0f, 8.0f);
            for (int b = 0; b < bars; ++b)
            {
                const auto y = inner.getY() + inner.getHeight() * ((float) b + 0.5f) / (float) bars;
                g.drawLine (inner.getX(), y, inner.getRight(), y, 1.5f);
            }
        }
    }
}

//==============================================================================
ModMatrix::Row::Row (APVTS& state, int index)
    : source (state, ForgeID::slot (index, ForgeID::source), Forge::modSourceNames()),
      dest (state, ForgeID::slot (index, ForgeID::dest), Forge::modDestNames()),
      amount (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      attachment (state, ForgeID::slot (index, ForgeID::amount), amount)
{
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.setPopupDisplayEnabled (true, true, nullptr);
    amount.setTooltip ("Amount (double-click to reset)");
}

ModMatrix::ModMatrix (APVTS& state)
{
    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        rows.push_back (std::make_unique<Row> (state, i));
        auto& row = *rows.back();
        for (auto* c : std::initializer_list<juce::Component*> { &row.source.box, &row.dest.box, &row.amount })
            addAndMakeVisible (c);

        // Relight the row number when a route is made or cleared.
        row.source.box.onChange = row.dest.box.onChange = [this] { repaint(); };
    }
}

void ModMatrix::resized()
{
    const auto columns = 2, perColumn = Forge::numModSlots / columns;
    const auto columnWidth = (getWidth() - 16) / columns;
    const auto rowHeight = juce::jmin (32, (getHeight() - 16) / perColumn);

    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        auto area = juce::Rectangle<int> ((i / perColumn) * (columnWidth + 16), 16 + (i % perColumn) * rowHeight,
                                          columnWidth, rowHeight).reduced (0, 3);
        area.removeFromLeft (24);
        auto& row = *rows[(size_t) i];
        row.source.box.setBounds (area.removeFromLeft ((int) (area.getWidth() * 0.3f)));
        area.removeFromLeft (4);
        row.dest.box.setBounds (area.removeFromLeft ((int) (area.getWidth() * 0.5f)));
        area.removeFromLeft (8);
        row.amount.setBounds (area);
    }
}

void ModMatrix::paint (juce::Graphics& g)
{
    const auto columns = 2, perColumn = Forge::numModSlots / columns;
    const auto columnWidth = (getWidth() - 16) / columns;
    const auto rowHeight = juce::jmin (32, (getHeight() - 16) / perColumn);

    g.setFont (mono (9.0f, true));
    for (int c = 0; c < columns; ++c)
    {
        const auto x = c * (columnWidth + 16);
        g.setColour (textDim);
        g.drawText ("SOURCE", x + 24, 0, 80, 14, juce::Justification::centredLeft);
        g.drawText ("DESTINATION", x + 24 + (int) ((columnWidth - 24) * 0.3f) + 4, 0, 100, 14, juce::Justification::centredLeft);
        g.drawText ("AMOUNT", x + columnWidth - 80, 0, 80, 14, juce::Justification::centredRight);
    }

    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        const auto x = (i / perColumn) * (columnWidth + 16);
        const auto y = 16 + (i % perColumn) * rowHeight;
        const auto active = rows[(size_t) i]->source.box.getSelectedItemIndex() > 0 && rows[(size_t) i]->dest.box.getSelectedItemIndex() > 0;
        g.setColour (active ? cyan : textDim.withAlpha (0.6f));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), x, y, 22, rowHeight, juce::Justification::centredLeft);
    }
}
