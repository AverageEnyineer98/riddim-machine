#include "ForgeUI.h"
#include <juce_dsp/juce_dsp.h>

using namespace ForgeUI;
using namespace ForgePalette;
using Forge::ModDest;
using Forge::ModSource;

namespace
{
    constexpr int headerHeight = 24;
    const juce::Colour amber { 0xffffb84d };

    void drawScopeBackground (juce::Graphics& g, juce::Rectangle<float> area, int columns = 8, int rows = 4)
    {
        g.setColour (juce::Colour (0xff050a16));
        g.fillRoundedRectangle (area, 5.0f);

        g.setColour (glassEdge.withAlpha (0.4f));
        for (int i = 1; i < columns; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / (float) columns), area.getY() + 2.0f, area.getBottom() - 2.0f);
        for (int i = 1; i < rows; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / (float) rows), area.getX() + 2.0f, area.getRight() - 2.0f);

        g.setColour (glassEdge);
        g.drawRoundedRectangle (area, 5.0f, 1.0f);
    }

    // A closed version of `line` down to `baseline`, for the soft fill under a curve.
    void fillUnder (juce::Graphics& g, juce::Path line, juce::Rectangle<float> area, float baseline, juce::Colour colour)
    {
        const auto bounds = line.getBounds();
        line.lineTo (bounds.getRight(), baseline);
        line.lineTo (bounds.getX(), baseline);
        line.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0.0f, area.getY(),
                                                 colour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
        g.fillPath (line);
    }

    juce::String paramIdFor (ModDest d)
    {
        using namespace ForgeID;
        const auto i = (int) d;
        if (i >= (int) ModDest::posA && i <= (int) ModDest::detuneC)
        {
            const auto osc = oscLetter ((i - (int) ModDest::posA) / 7);
            const char* names[] { pos, warp1, warp2, level, semi, pan, detune };
            return ForgeID::osc (osc, names[(i - (int) ModDest::posA) % 7]);
        }

        switch (d)
        {
            case ModDest::subLevel:       return ForgeID::subLevel;
            case ModDest::noiseLevel:     return ForgeID::noise;
            case ModDest::noiseColour:    return ForgeID::noiseColour;
            case ModDest::cutoff:         return filter (0, cutoffName);
            case ModDest::resonance:      return filter (0, res);
            case ModDest::drive:          return filter (0, driveName);
            case ModDest::filterVar:      return filter (0, var);
            case ModDest::filterMix:      return filter (0, mix);
            case ModDest::cutoff2:        return filter (1, cutoffName);
            case ModDest::resonance2:     return filter (1, res);
            case ModDest::drive2:         return filter (1, driveName);
            case ModDest::filterVar2:     return filter (1, var);
            case ModDest::filterMix2:     return filter (1, mix);
            case ModDest::volume:         return "master_gain";
            case ModDest::masterPitch:    return masterTune;
            case ModDest::pitchEnvAmount: return pitchEnv;
            case ModDest::lfo1Rate:       return lfo (0, hz);
            case ModDest::lfo2Rate:       return lfo (1, hz);
            case ModDest::lfo3Rate:       return lfo (2, hz);
            case ModDest::lfo4Rate:       return lfo (3, hz);
            case ModDest::fxVowel:        return "vowel";
            case ModDest::distDrive:      return "dist_drive";
            case ModDest::distMix:        return "dist_mix";
            case ModDest::combMix:        return "comb_mix";
            case ModDest::smash:          return "squash";
            case ModDest::heat:           return "heat";
            case ModDest::fxFilterCutoff: return fxfCutoff;
            case ModDest::fxFilterRes:    return fxfRes;
            case ModDest::fxFilterMix:    return fxfMix;
            case ModDest::eqGain1:        return eq1Gain;
            case ModDest::eqGain2:        return eq2Gain;
            case ModDest::eqFreq1:        return eq1Freq;
            case ModDest::eqFreq2:        return eq2Freq;
            case ModDest::bodeShift:      return bodeShift;
            case ModDest::bodeMix:        return bodeMix;
            case ModDest::hyperMix:       return ForgeID::hyperMix;
            case ModDest::dimensionMix:   return dimMix;
            case ModDest::chorusMix:      return ForgeID::chorusMix;
            case ModDest::phaserMix:      return ForgeID::phaserMix;
            case ModDest::flangerMix:     return ForgeID::flangerMix;
            case ModDest::delayMix:       return ForgeID::delayMix;
            case ModDest::reverbMix:      return ForgeID::reverbMix;
            default:                      return {};
        }
    }
}

//==============================================================================
juce::Colour ForgeUI::sourceColour (ModSource s)
{
    const auto i = (int) s;
    if (i >= (int) ModSource::lfo1 && i <= (int) ModSource::lfo4)     return cyan;
    if (i >= (int) ModSource::env2 && i <= (int) ModSource::ampEnv)   return amber;
    if (i >= (int) ModSource::macro1 && i <= (int) ModSource::macro8) return lime;
    return violet;
}

juce::String ForgeUI::sourceShortName (ModSource s)
{
    return Forge::modSourceNames()[(int) s];
}

ModDest ForgeUI::destForParameter (const juce::String& paramID)
{
    static const auto table = []
    {
        std::map<juce::String, ModDest> map;
        for (int d = 1; d < (int) ModDest::count; ++d)
            if (const auto id = paramIdFor ((ModDest) d); id.isNotEmpty())
                map.emplace (id, (ModDest) d);

        // The LFO rate knob (synced) drives the same destination as the free rate.
        for (int l = 0; l < Forge::numLfos; ++l)
            map.emplace (ForgeID::lfo (l, ForgeID::rate), (ModDest) ((int) ModDest::lfo1Rate + l));
        return map;
    }();

    const auto it = table.find (paramID);
    return it != table.end() ? it->second : ModDest::none;
}

//==============================================================================
ModSource ModSlots::source (int slot) const { return (ModSource) juce::roundToInt (state.getRawParameterValue (ForgeID::slot (slot, ForgeID::source))->load()); }
ModDest ModSlots::dest (int slot) const     { return (ModDest) juce::roundToInt (state.getRawParameterValue (ForgeID::slot (slot, ForgeID::dest))->load()); }
float ModSlots::amount (int slot) const     { return state.getRawParameterValue (ForgeID::slot (slot, ForgeID::amount))->load(); }
bool ModSlots::bipolar (int slot) const     { return state.getRawParameterValue (ForgeID::slot (slot, ForgeID::bipolar))->load() > 0.5f; }

void ModSlots::set (const juce::String& id, float plainValue)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

int ModSlots::connect (ModSource s, ModDest d, float amountValue)
{
    int target = -1;
    for (int i = 0; i < Forge::numModSlots && target < 0; ++i)
        if (source (i) == s && dest (i) == d)
            target = i;

    for (int i = 0; i < Forge::numModSlots && target < 0; ++i)
        if (source (i) == ModSource::none || dest (i) == ModDest::none)
            target = i;

    if (target < 0)
        return -1;

    set (ForgeID::slot (target, ForgeID::source), (float) s);
    set (ForgeID::slot (target, ForgeID::aux), 0.0f);
    set (ForgeID::slot (target, ForgeID::dest), (float) d);
    set (ForgeID::slot (target, ForgeID::amount), amountValue);
    set (ForgeID::slot (target, ForgeID::bipolar), 0.0f);
    return target;
}

void ModSlots::setAmount (int slot, float a) { set (ForgeID::slot (slot, ForgeID::amount), a); }

void ModSlots::clear (int slot)
{
    set (ForgeID::slot (slot, ForgeID::source), 0.0f);
    set (ForgeID::slot (slot, ForgeID::aux), 0.0f);
    set (ForgeID::slot (slot, ForgeID::dest), 0.0f);
    set (ForgeID::slot (slot, ForgeID::amount), 0.0f);
    set (ForgeID::slot (slot, ForgeID::bipolar), 0.0f);
}

std::vector<int> ModSlots::slotsFor (ModDest d) const
{
    std::vector<int> found;
    for (int i = 0; i < Forge::numModSlots; ++i)
        if (dest (i) == d && source (i) != ModSource::none)
            found.push_back (i);
    return found;
}

float ForgeUI::liveSourceValue (ForgeProcessor& p, ModSource s)
{
    const auto i = (int) s;
    if (i >= (int) ModSource::lfo1 && i <= (int) ModSource::lfo4)
        return p.getLfoValue (i - (int) ModSource::lfo1);
    if (i >= (int) ModSource::macro1 && i <= (int) ModSource::macro8)
        return p.parameters.getRawParameterValue (ForgeID::macro (i - (int) ModSource::macro1))->load();
    return 1.0f;
}

//==============================================================================
void ModSlider::mouseDown (const juce::MouseEvent& e)
{
    rightClicked = e.mods.isPopupMenu();
    if (rightClicked)
    {
        if (onRightClick)
            onRightClick();
        return;
    }
    juce::Slider::mouseDown (e);
}

void ModSlider::mouseDrag (const juce::MouseEvent& e) { if (! rightClicked) juce::Slider::mouseDrag (e); }
void ModSlider::mouseUp (const juce::MouseEvent& e)   { if (! rightClicked) juce::Slider::mouseUp (e); rightClicked = false; }

//==============================================================================
ForgeUI::Knob::Knob (ForgeProcessor& p, const juce::String& paramID, const juce::String& text, juce::Colour colour)
    : processor (p), slots (p.parameters), dest (destForParameter (paramID)),
      slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      attachment (p.parameters, paramID, slider),
      parameter (p.parameters.getParameter (paramID))
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setDoubleClickReturnValue (true, (double) parameter->convertFrom0to1 (parameter->getDefaultValue()));
    slider.setTooltip (parameter->getName (64) + (dest != ModDest::none ? "  (drop a mod source here; right-click for routes)"
                                                                         : "  (double-click to reset)"));
    slider.onValueChange = [this] { updateReadout(); };
    slider.onRightClick = [this] { showModMenu(); };
    addAndMakeVisible (slider);

    name.setText (text.toUpperCase(), juce::dontSendNotification);
    value.setColour (juce::Label::textColourId, colour.interpolatedWith (juce::Colours::white, 0.25f));

    for (auto* label : { &name, &value })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setInterceptsMouseClicks (false, false);
        label->setMinimumHorizontalScale (0.6f);
        label->setBorderSize ({});
        addAndMakeVisible (*label);
    }

    name.setFont (mono (9.0f, true));
    name.setColour (juce::Label::textColourId, textDim);
    value.setFont (mono (9.0f));
    updateReadout();

    if (dest != ModDest::none)
        startTimerHz (30);
}

void ForgeUI::Knob::updateReadout()
{
    value.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void ForgeUI::Knob::resized()
{
    const auto size = juce::jlimit (16, 58, juce::jmin (getWidth(), getHeight() - 22));
    auto area = getLocalBounds().withSizeKeepingCentre (getWidth(), size + 23);
    slider.setBounds (area.removeFromTop (size).withSizeKeepingCentre (size, size));
    area.removeFromTop (1);
    name.setBounds (area.removeFromTop (11));
    value.setBounds (area.removeFromTop (11));
}

void ForgeUI::Knob::timerCallback()
{
    std::vector<Ring> now;
    for (const auto slot : slots.slotsFor (dest))
        now.push_back ({ slots.source (slot), slots.amount (slot), slots.bipolar (slot), liveSourceValue (processor, slots.source (slot)) });

    auto same = now.size() == rings.size();
    for (size_t i = 0; same && i < now.size(); ++i)
        same = now[i].source == rings[i].source && std::abs (now[i].amount - rings[i].amount) < 1.0e-4f
               && now[i].bipolar == rings[i].bipolar && std::abs (now[i].live - rings[i].live) < 0.004f;

    if (! same)
    {
        rings = std::move (now);
        repaint();
    }
}

void ForgeUI::Knob::paintOverChildren (juce::Graphics& g)
{
    const auto bounds = slider.getBounds().toFloat().reduced (5.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto rotary = slider.getRotaryParameters();
    const auto start = rotary.startAngleRadians, end = rotary.endAngleRadians;
    const auto base = (float) parameter->getValue();
    auto angleFor = [&] (float v) { return start + juce::jlimit (0.0f, 1.0f, v) * (end - start); };

    // Each route as a thin ring outside the dial, innermost first.
    auto ringRadius = radius + 3.5f;
    for (const auto& r : rings)
    {
        const auto colour = sourceColour (r.source);
        const auto from = r.bipolar ? base - r.amount : base;
        const auto to = base + r.amount;

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, angleFor (juce::jmin (from, to)), angleFor (juce::jmax (from, to)), true);
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto live = r.bipolar ? base + (r.live * 2.0f - 1.0f) * r.amount : base + r.live * r.amount;
        const auto dot = centre.getPointOnCircumference (ringRadius, angleFor (live));
        g.setColour (juce::Colours::white);
        g.fillEllipse (dot.x - 2.0f, dot.y - 2.0f, 4.0f, 4.0f);
        ringRadius += 3.0f;
    }

    if (dropHover)
    {
        g.setColour (lime.withAlpha (0.25f));
        g.fillEllipse (bounds.expanded (4.0f));
        g.setColour (lime);
        g.drawEllipse (bounds.expanded (4.0f), 1.5f);
    }
}

bool ForgeUI::Knob::isInterestedInDragSource (const SourceDetails& details)
{
    return dest != ModDest::none && details.description.toString().startsWith ("mod:");
}

void ForgeUI::Knob::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    const auto source = (ModSource) details.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    // Point the new route into the knob's free range, so it shows and does something.
    if (slots.connect (source, dest, parameter->getValue() > 0.5f ? -0.5f : 0.5f) < 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Mod matrix full",
                                                "All 16 mod slots are in use. Remove one on the MATRIX page first.");
    timerCallback();
}

void ForgeUI::Knob::showModMenu()
{
    juce::PopupMenu menu;
    const auto routes = slots.slotsFor (dest);

    if (routes.empty())
        menu.addItem (-1, "No modulation. Drag an LFO, envelope or macro handle here.", false);

    int itemId = 1;
    std::vector<std::function<void()>> actions;
    for (const auto slot : routes)
    {
        juce::PopupMenu sub;
        for (const auto amount : { 1.0f, 0.75f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -0.75f, -1.0f })
        {
            sub.addItem (itemId++, juce::String (juce::roundToInt (amount * 100.0f)) + "%", true, std::abs (slots.amount (slot) - amount) < 0.005f);
            actions.push_back ([this, slot, amount] { slots.setAmount (slot, amount); });
        }
        sub.addSeparator();
        sub.addItem (itemId++, "Bipolar", true, slots.bipolar (slot));
        actions.push_back ([this, slot] { if (auto* p = slots.state.getParameter (ForgeID::slot (slot, ForgeID::bipolar))) p->setValueNotifyingHost (slots.bipolar (slot) ? 0.0f : 1.0f); });
        sub.addItem (itemId++, "Remove");
        actions.push_back ([this, slot] { slots.clear (slot); });

        menu.addSubMenu (sourceShortName (slots.source (slot)) + "  " + juce::String (juce::roundToInt (slots.amount (slot) * 100.0f)) + "%", sub);
    }

    if (! routes.empty())
    {
        menu.addSeparator();
        menu.addItem (itemId++, "Remove all modulation");
        actions.push_back ([this, routes] { for (auto s : routes) slots.clear (s); });
    }

    menu.addSeparator();
    menu.addItem (itemId++, "Reset to default");
    actions.push_back ([this] { slider.setValue (slider.getDoubleClickReturnValue(), juce::sendNotificationSync); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider),
                        [safe = juce::Component::SafePointer<Knob> (this), actions] (int result)
    {
        if (safe != nullptr && result > 0 && result <= (int) actions.size())
        {
            actions[(size_t) result - 1]();
            safe->timerCallback();
        }
    });
}

//==============================================================================
SourceHandle::SourceHandle (ModSource s) : source (s)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag onto any knob to modulate it with " + sourceShortName (s));
}

void SourceHandle::paint (juce::Graphics& g)
{
    const auto colour = sourceColour (source);
    const auto area = getLocalBounds().toFloat().reduced (1.5f);
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);

    g.setColour (colour.withAlpha (isMouseOver() ? 0.35f : 0.15f));
    g.fillEllipse (box);
    g.setColour (colour);
    g.drawEllipse (box.reduced (0.5f), 1.2f);

    const auto c = box.getCentre();
    const auto r = size * 0.28f;
    g.drawLine (c.x - r, c.y, c.x + r, c.y, 1.4f);
    g.drawLine (c.x, c.y - r, c.x, c.y + r, 1.4f);
}

void SourceHandle::mouseDrag (const juce::MouseEvent&)
{
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
        {
            juce::Image image (juce::Image::ARGB, 22, 22, true);
            {
                juce::Graphics g (image);
                g.setColour (sourceColour (source));
                g.fillEllipse (2.0f, 2.0f, 18.0f, 18.0f);
                g.setColour (void_);
                g.setFont (mono (9.0f, true));
                g.drawText ("+", 2, 2, 18, 18, juce::Justification::centred);
            }
            container->startDragging ("mod:" + juce::String ((int) source), this, juce::ScaledImage (image));
        }
}

//==============================================================================
Card::Card (const juce::String& t, juce::Colour c) : title (t.toUpperCase()), accent (c) {}

juce::Rectangle<int> Card::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).reduced (8, 4);
}

void Card::resized()
{
    if (layout)
        layout (getContentBounds());
}

void Card::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xe0101c38), 0.0f, 0.0f,
                                             juce::Colour (0xe0070d1d), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    // Title bar.
    auto bar = bounds.withHeight ((float) headerHeight - 4.0f);
    g.setColour (juce::Colour (0x40000000));
    g.fillRoundedRectangle (bar.reduced (1.0f), 5.0f);
    g.setGradientFill (juce::ColourGradient (accent, bounds.getX() + 8.0f, 0.0f,
                                             accent.withAlpha (0.0f), bounds.getWidth() * 0.8f, 0.0f, false));
    g.fillRect (bounds.getX() + 8.0f, bar.getBottom(), bounds.getWidth() * 0.8f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle (bounds.getX() + 8.0f, bar.getCentreY() - 4.0f, 3.0f, 8.0f, 1.5f);
    g.setColour (text);
    g.setFont (mono (10.5f, true).withExtraKerningFactor (0.15f));
    g.drawText (title, 16, 0, getWidth() - 24, (int) bar.getHeight(), juce::Justification::centredLeft);
}

void ForgeUI::layoutRow (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int gap)
{
    const auto n = (int) items.size();
    if (n == 0)
        return;

    const auto width = (area.getWidth() - gap * (n - 1)) / n;
    for (auto* item : items)
    {
        if (item != nullptr)
            item->setBounds (area.removeFromLeft (width));
        else
            area.removeFromLeft (width);
        area.removeFromLeft (gap);
    }
}

//==============================================================================
WavetableView::WavetableView (ForgeProcessor& p, int osc, juce::Colour c) : processor (p), oscIndex (osc), colour (c)
{
    const auto w = ForgeID::oscLetter (osc);
    auto& state = p.parameters;
    table       = state.getRawParameterValue (ForgeID::osc (w, ForgeID::table));
    position    = state.getRawParameterValue (ForgeID::osc (w, ForgeID::pos));
    warpMode[0] = state.getRawParameterValue (ForgeID::osc (w, ForgeID::warp1Mode));
    warp[0]     = state.getRawParameterValue (ForgeID::osc (w, ForgeID::warp1));
    warpMode[1] = state.getRawParameterValue (ForgeID::osc (w, ForgeID::warp2Mode));
    warp[1]     = state.getRawParameterValue (ForgeID::osc (w, ForgeID::warp2));
    setInterceptsMouseClicks (false, false);
    startTimerHz (24);
}

void WavetableView::timerCallback()
{
    const std::array<float, 8> now { table->load(), position->load(), warpMode[0]->load(), warp[0]->load(),
                                     warpMode[1]->load(), warp[1]->load(), processor.getImportedTable (oscIndex) != nullptr ? 1.0f : 0.0f, 0.0f };
    if (now != last)
    {
        last = now;
        repaint();
    }
}

float WavetableView::sampleAt (int builtIn, const UserWavetable* user, float pos, double phase, bool warped) const
{
    auto gain = 1.0f;
    if (warped)
    {
        const auto sub = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
        for (int w = 0; w < 2; ++w)
        {
            const auto mode = (Forge::Warp) juce::roundToInt (warpMode[w]->load());
            if (mode != Forge::Warp::off)
            {
                const auto r = Forge::warpPhase (mode, warp[w]->load(), phase, sub, sub);
                phase = r.phase;
                gain *= r.gain;
            }
        }
    }

    phase -= std::floor (phase);
    return gain * (user != nullptr ? user->sample (0, pos, phase) : bank->sample (builtIn, 0, pos, phase));
}

void WavetableView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff050a16));
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (area, 5.0f, 1.0f);

    const auto tableIndex = juce::roundToInt (table->load());
    const auto* user = tableIndex == Forge::importedTable() ? processor.getImportedTable (oscIndex) : nullptr;
    const auto builtIn = juce::jlimit (0, bank->getNumTables() - 1, tableIndex);
    const auto pos = position->load();

    // Frames recede up and to the right; the front frame is position 0.
    constexpr int frames = 16, points = 120;
    const auto inner = area.reduced (8.0f, 10.0f);
    const auto depthX = inner.getWidth() * 0.22f, depthY = inner.getHeight() * 0.32f;
    const auto plotW = inner.getWidth() - depthX, amp = (inner.getHeight() - depthY) * 0.42f;

    auto frameOrigin = [&] (float t) { return juce::Point<float> (inner.getX() + depthX * t, inner.getBottom() - amp - depthY * t); };

    for (int f = frames - 1; f >= 0; --f)
    {
        const auto t = (float) f / (float) (frames - 1);
        const auto origin = frameOrigin (t);
        juce::Path line;
        for (int i = 0; i <= points; i += 2)
        {
            const auto s = juce::jlimit (-1.0f, 1.0f, sampleAt (builtIn, user, t, (double) i / points, false));
            const auto x = origin.x + plotW * (float) i / points, y = origin.y - s * amp;
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (0.10f + 0.12f * (1.0f - t)));
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    // The playing frame, warped, in front.
    const auto origin = frameOrigin (pos);
    juce::Path live;
    for (int i = 0; i <= points * 2; ++i)
    {
        const auto s = juce::jlimit (-1.2f, 1.2f, sampleAt (builtIn, user, pos, (double) i / (points * 2), true));
        const auto x = origin.x + plotW * (float) i / (float) (points * 2), y = origin.y - s * amp;
        if (i == 0) live.startNewSubPath (x, y); else live.lineTo (x, y);
    }
    fillUnder (g, live, area, origin.y, colour);
    glowStroke (g, live, colour, 1.5f);

    g.setColour (textDim);
    g.setFont (mono (9.0f));
    g.drawText (juce::String (juce::roundToInt (pos * 255.0f) + 1), area.reduced (6.0f, 4.0f), juce::Justification::topRight);
}

//==============================================================================
FilterView::FilterView (APVTS& s, std::function<juce::String (const char*)> ids, juce::Colour c)
    : state (s), idFor (std::move (ids)), colour (c)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (15);
}

void FilterView::setIds (std::function<juce::String (const char*)> ids)
{
    idFor = std::move (ids);
    last[0] = -1.0f;
    timerCallback();
}

void FilterView::timerCallback()
{
    auto v = [this] (const char* name) { return state.getRawParameterValue (idFor (name))->load(); };
    const std::array<float, 6> now { v (ForgeID::type), v (ForgeID::cutoffName), v (ForgeID::res), v (ForgeID::driveName), v (ForgeID::var), v (ForgeID::mix) };
    if (now != last)
    {
        last = now;
        measure();
        repaint();
    }
}

void FilterView::measure()
{
    constexpr int order = 12, size = 1 << order;
    constexpr double sampleRate = 48000.0;

    Forge::FilterChannel filter;
    filter.prepare (sampleRate);
    Forge::FilterParams params;
    params.type = (Forge::FilterType) juce::roundToInt (last[0]);
    params.cutoff = last[1];
    params.resonance = last[2];
    params.drive = 0.0f; // drive is a level change here, not a shape
    params.var = last[4];
    params.mix = last[5];
    filter.setup (params);

    std::vector<float> data ((size_t) size * 2, 0.0f);
    for (int i = 0; i < size; ++i)
        data[(size_t) i] = filter.process (i == 0 ? 1.0f : 0.0f);

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data(), true);

    const auto columns = juce::jmax (8, getWidth());
    response.assign ((size_t) columns, -60.0f);
    for (int c = 0; c < columns; ++c)
    {
        const auto hz = 20.0 * std::pow (1000.0, (double) c / (columns - 1));
        const auto bin = juce::jlimit (1, size / 2 - 1, (int) (hz / sampleRate * size));
        response[(size_t) c] = juce::Decibels::gainToDecibels (data[(size_t) bin], -60.0f);
    }
}

void FilterView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area, 10, 3);
    if (response.empty())
        return;

    const auto inner = area.reduced (3.0f, 6.0f);
    juce::Path line;
    for (size_t c = 0; c < response.size(); ++c)
    {
        const auto x = inner.getX() + inner.getWidth() * (float) c / (float) (response.size() - 1);
        const auto y = juce::jmap (juce::jlimit (-36.0f, 18.0f, response[c]), -36.0f, 18.0f, inner.getBottom(), inner.getY());
        if (c == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    fillUnder (g, line, area, inner.getBottom(), colour);
    glowStroke (g, line, colour, 1.4f);
}

//==============================================================================
EnvelopeView::EnvelopeView (APVTS& s, juce::Colour c) : state (s), colour (c)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (15);
}

void EnvelopeView::setIds (const juce::String& a, const juce::String& d, const juce::String& s, const juce::String& r)
{
    attack = state.getRawParameterValue (a);
    decay = state.getRawParameterValue (d);
    sustain = state.getRawParameterValue (s);
    release = state.getRawParameterValue (r);
    repaint();
}

void EnvelopeView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area);
    if (attack == nullptr)
        return;

    // Square-root time axis, so short riddim envelopes stay readable next to long ones.
    const auto a = std::sqrt (attack->load()), d = std::sqrt (decay->load()), r = std::sqrt (release->load());
    const auto hold = 0.25f * (a + d + r) + 1.0f;
    const auto total = a + d + hold + r;
    const auto inner = area.reduced (6.0f, 8.0f);
    auto xAt = [&] (float t) { return inner.getX() + inner.getWidth() * t / total; };
    const auto sustainY = inner.getBottom() - sustain->load() * inner.getHeight();

    juce::Path line;
    line.startNewSubPath (inner.getX(), inner.getBottom());
    line.lineTo (xAt (a), inner.getY());
    line.quadraticTo (xAt (a + d * 0.25f), sustainY, xAt (a + d), sustainY);
    line.lineTo (xAt (a + d + hold), sustainY);
    line.quadraticTo (xAt (a + d + hold + r * 0.25f), inner.getBottom(), xAt (total), inner.getBottom());

    fillUnder (g, line, area, inner.getBottom(), colour);
    glowStroke (g, line, colour, 1.5f);

    for (const auto x : { xAt (a), xAt (a + d), xAt (a + d + hold) })
    {
        const auto y = x == xAt (a) ? inner.getY() : sustainY;
        g.setColour (juce::Colours::white);
        g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }
}

//==============================================================================
LfoEditor::LfoEditor (ForgeProcessor& p, juce::Colour c) : processor (p), colour (c)
{
    setLfo (0);
    startTimerHz (40);
}

void LfoEditor::setLfo (int index)
{
    lfo = index;
    curve = processor.getLfoCurve (lfo);
    shownShape = -1;
    repaint();
}

void LfoEditor::timerCallback()
{
    const auto shape = juce::roundToInt (processor.parameters.getRawParameterValue (ForgeID::lfo (lfo, ForgeID::shape))->load());
    const auto phase = processor.getLfoPhase (lfo);
    const auto custom = shape == Forge::lfoShapeNames().size() - 1;

    if (shape != shownShape || (custom && dragPoint < 0 && bendSegment < 0))
    {
        if (custom)
            curve = processor.getLfoCurve (lfo);
        shownShape = shape;
        repaint();
    }
    else if (std::abs (phase - livePhase) > 0.001f)
    {
        livePhase = phase;
        repaint();
    }
}

juce::Rectangle<float> LfoEditor::plotArea() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f); }

juce::Point<float> LfoEditor::toScreen (float x, float y) const
{
    const auto a = plotArea();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

float LfoEditor::shownValue (float x) const
{
    const auto custom = shownShape == Forge::lfoShapeNames().size() - 1;
    return custom ? curve.evaluate (x) : Forge::builtInLfoShape (juce::jmax (0, shownShape), juce::jlimit (0.0, 0.99999, (double) x));
}

Forge::LfoCurve LfoEditor::editableCurve()
{
    const auto customIndex = Forge::lfoShapeNames().size() - 1;
    if (shownShape != customIndex)
    {
        curve = Forge::LfoCurve::fromShape (juce::jmax (0, shownShape));
        if (auto* p = processor.parameters.getParameter (ForgeID::lfo (lfo, ForgeID::shape)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) customIndex));
        shownShape = customIndex;
    }
    return curve;
}

void LfoEditor::commit()
{
    processor.setLfoCurve (lfo, curve);
    repaint();
}

int LfoEditor::hitPoint (juce::Point<float> pos) const
{
    for (size_t i = 0; i < curve.points.size(); ++i)
        if (toScreen (curve.points[i].x, curve.points[i].y).getDistanceFrom (pos) < 7.0f)
            return (int) i;
    return -1;
}

int LfoEditor::hitSegment (juce::Point<float> pos) const
{
    for (size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto x = (curve.points[i].x + curve.points[i + 1].x) * 0.5f;
        if (toScreen (x, curve.evaluate (x)).getDistanceFrom (pos) < 7.0f)
            return (int) i;
    }
    return -1;
}

void LfoEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto custom = shownShape == Forge::lfoShapeNames().size() - 1;
    const auto hit = custom ? hitPoint (e.position) : -1;
    if (hit != hoverPoint)
    {
        hoverPoint = hit;
        repaint();
    }
}

void LfoEditor::mouseDown (const juce::MouseEvent& e)
{
    curve = editableCurve();
    dragPoint = hitPoint (e.position);
    bendSegment = dragPoint < 0 ? hitSegment (e.position) : -1;
    if (bendSegment >= 0)
        bendStart = curve.points[(size_t) bendSegment].curve;
}

void LfoEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto a = plotArea();
    auto x = juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
    auto y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());

    if (e.mods.isShiftDown())
    {
        x = std::round (x * (float) grid) / (float) grid;
        y = std::round (y * 8.0f) / 8.0f;
    }

    if (dragPoint >= 0)
    {
        auto& points = curve.points;
        const auto i = (size_t) dragPoint;
        // The first and last points stay at the cycle's ends, and keep the same height.
        if (i == 0 || i == points.size() - 1)
        {
            points.front().y = points.back().y = y;
        }
        else
        {
            points[i].x = juce::jlimit (points[i - 1].x, points[i + 1].x, x);
            points[i].y = y;
        }
        commit();
    }
    else if (bendSegment >= 0)
    {
        const auto& p0 = curve.points[(size_t) bendSegment];
        const auto& p1 = curve.points[(size_t) bendSegment + 1];
        const auto direction = p1.y >= p0.y ? 1.0f : -1.0f;
        curve.points[(size_t) bendSegment].curve = juce::jlimit (-1.0f, 1.0f, bendStart - direction * (float) e.getDistanceFromDragStartY() / 80.0f);
        commit();
    }
}

void LfoEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint = bendSegment = -1;
}

void LfoEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    curve = editableCurve();
    const auto hit = hitPoint (e.position);
    auto& points = curve.points;

    if (hit > 0 && hit < (int) points.size() - 1)
    {
        points.erase (points.begin() + hit);
    }
    else if (hit < 0)
    {
        const auto a = plotArea();
        const auto x = juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
        const auto y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        auto it = std::upper_bound (points.begin(), points.end(), x, [] (float v, const Forge::LfoCurve::Point& p) { return v < p.x; });
        if (it != points.begin() && it != points.end())
            points.insert (it, { x, y, 0.0f });
    }
    commit();
}

void LfoEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    drawScopeBackground (g, area, grid, 4);

    const auto plot = plotArea();
    juce::Path line;
    constexpr int points = 300;
    for (int i = 0; i <= points; ++i)
    {
        const auto x = (float) i / points;
        const auto p = toScreen (x, shownValue (juce::jmin (x, 0.9999f)));
        if (i == 0) line.startNewSubPath (p); else line.lineTo (p);
    }
    fillUnder (g, line, area, plot.getBottom(), colour);
    glowStroke (g, line, colour, 1.6f);

    if (shownShape == Forge::lfoShapeNames().size() - 1)
    {
        for (size_t i = 0; i < curve.points.size(); ++i)
        {
            const auto p = toScreen (curve.points[i].x, curve.points[i].y);
            const auto r = (int) i == hoverPoint || (int) i == dragPoint ? 5.0f : 3.5f;
            g.setColour (void_);
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
            g.setColour (juce::Colours::white);
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.5f);

            if (i + 1 < curve.points.size())
            {
                const auto mx = (curve.points[i].x + curve.points[i + 1].x) * 0.5f;
                const auto m = toScreen (mx, curve.evaluate (mx));
                g.setColour (colour.withAlpha (0.8f));
                g.drawEllipse (m.x - 2.5f, m.y - 2.5f, 5.0f, 5.0f, 1.0f);
            }
        }
    }
    else
    {
        g.setColour (textDim);
        g.setFont (mono (9.0f));
        g.drawText ("CLICK AND DRAG TO DRAW YOUR OWN", area.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
    }

    // Playhead.
    const auto x = juce::jlimit (0.0f, 0.9999f, livePhase);
    const auto p = toScreen (x, shownValue (x));
    g.setColour (colour.withAlpha (0.3f));
    g.drawVerticalLine ((int) p.x, area.getY() + 2.0f, area.getBottom() - 2.0f);
    g.setColour (colour.withAlpha (0.4f));
    g.fillEllipse (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
}

//==============================================================================
HitSequencer::HitSequencer (ForgeProcessor& p)
    : processor (p), length (*p.parameters.getRawParameterValue (ForgeID::hitSeqLen))
{
    for (int i = 0; i < Forge::numHitSteps; ++i)
    {
        rates.push_back (std::make_unique<ParamMenu> (p.parameters, ForgeID::hitRate (i), Forge::lfoRateNames()));
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
    }
    repaint();
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
            const auto bars = juce::jlimit (1, 16, (int) (Forge::lfoRateCyclesPerBeat()[rate] * 2.0));
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
      aux (state, ForgeID::slot (index, ForgeID::aux), Forge::modSourceNames()),
      dest (state, ForgeID::slot (index, ForgeID::dest), Forge::modDestNames()),
      bipolar (state, ForgeID::slot (index, ForgeID::bipolar), "Bi"),
      amount (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
      attachment (state, ForgeID::slot (index, ForgeID::amount), amount)
{
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    amount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    amount.setColour (juce::Slider::textBoxTextColourId, text);
    amount.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    amount.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    amount.updateText();
    amount.setTooltip ("Amount (double-click to reset)");
    aux.box.setTooltip ("Aux source: scales this route (e.g. a macro that fades the LFO in)");
    bipolar.button.setTooltip ("Bipolar: the source swings both ways around the knob's value");
    clear.setTooltip ("Clear this slot");
}

ModMatrix::ModMatrix (APVTS& s) : state (s)
{
    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        rows.push_back (std::make_unique<Row> (state, i));
        auto& row = *rows.back();
        for (auto* c : std::initializer_list<juce::Component*> { &row.source.box, &row.aux.box, &row.dest.box, &row.bipolar.button, &row.amount, &row.clear })
            addAndMakeVisible (c);

        row.clear.onClick = [this, i] { ModSlots (state).clear (i); };
    }
    startTimerHz (4);
}

void ModMatrix::resized()
{
    const auto rowHeight = juce::jmin (26, (getHeight() - 18) / Forge::numModSlots);

    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        auto area = juce::Rectangle<int> (0, 18 + i * rowHeight, getWidth(), rowHeight).reduced (0, 2);
        area.removeFromLeft (28);
        auto& row = *rows[(size_t) i];
        const auto w = area.getWidth();
        row.source.box.setBounds (area.removeFromLeft ((int) (w * 0.17f)));
        area.removeFromLeft (6);
        row.amount.setBounds (area.removeFromLeft ((int) (w * 0.27f)));
        area.removeFromLeft (6);
        row.bipolar.button.setBounds (area.removeFromLeft (40));
        area.removeFromLeft (6);
        row.aux.box.setBounds (area.removeFromLeft ((int) (w * 0.15f)));
        area.removeFromLeft (6);
        row.clear.setBounds (area.removeFromRight (24));
        area.removeFromRight (6);
        row.dest.box.setBounds (area);
    }
}

void ModMatrix::paint (juce::Graphics& g)
{
    const auto rowHeight = juce::jmin (26, (getHeight() - 18) / Forge::numModSlots);
    const auto& first = *rows.front();

    g.setFont (mono (9.0f, true));
    g.setColour (textDim);
    g.drawText ("SOURCE", first.source.box.getX(), 0, 100, 14, juce::Justification::centredLeft);
    g.drawText ("AMOUNT", first.amount.getX(), 0, 100, 14, juce::Justification::centredLeft);
    g.drawText ("AUX", first.aux.box.getX(), 0, 100, 14, juce::Justification::centredLeft);
    g.drawText ("DESTINATION", first.dest.box.getX(), 0, 140, 14, juce::Justification::centredLeft);

    ModSlots slots (state);
    for (int i = 0; i < Forge::numModSlots; ++i)
    {
        const auto y = 18 + i * rowHeight;
        const auto active = slots.source (i) != ModSource::none && slots.dest (i) != ModDest::none;
        if (i % 2 == 0)
        {
            g.setColour (juce::Colour (0x0affffff));
            g.fillRect (0, y, getWidth(), rowHeight);
        }
        g.setColour (active ? sourceColour (slots.source (i)) : textDim.withAlpha (0.6f));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), 4, y, 22, rowHeight, juce::Justification::centredLeft);
    }
}

//==============================================================================
MacroKnob::MacroKnob (ForgeProcessor& p, int i, juce::Colour colour)
    : processor (p), index (i), knob (p, ForgeID::macro (i), {}, colour),
      handle ((ModSource) ((int) ModSource::macro1 + i))
{
    name.setJustificationType (juce::Justification::centred);
    name.setFont (mono (10.0f, true));
    name.setColour (juce::Label::textColourId, text);
    name.setEditable (false, true, false);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { processor.setMacroName (index, name.getText().trim().isEmpty() ? juce::String() : name.getText().trim()); };

    addAndMakeVisible (knob);
    addAndMakeVisible (name);
    addAndMakeVisible (handle);
    refreshName();
}

void MacroKnob::refreshName()
{
    name.setText (processor.getMacroName (index), juce::dontSendNotification);
}

void MacroKnob::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (16);
    handle.setBounds (top.removeFromRight (16));
    name.setBounds (top);
    knob.setBounds (area);
}
