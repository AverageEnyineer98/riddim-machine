#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour background { 0xff0b0b14 };
    inline const juce::Colour panel      { 0xff14141f };
    inline const juce::Colour panelEdge  { 0xff26263a };
    inline const juce::Colour accent     { 0xffb84dff };
    inline const juce::Colour accentAlt  { 0xff33e0ff };
    inline const juce::Colour text       { 0xffe6e6f0 };
    inline const juce::Colour textDim    { 0xff8a8aa3 };
}

// Dark neon look: arc knobs, pill toggles, flat combo boxes.
class RiddimLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RiddimLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, Palette::panelEdge);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::ComboBox::arrowColourId, Palette::accent);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.35f));
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::TextButton::buttonColourId, Palette::panelEdge);
        setColour (juce::TextButton::textColourOffId, Palette::text);
        setColour (juce::Label::textColourId, Palette::text);
        setColour (juce::Slider::textBoxTextColourId, Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel);
        setColour (juce::TooltipWindow::textColourId, Palette::text);
        setColour (juce::AlertWindow::backgroundColourId, Palette::panel);
        setColour (juce::AlertWindow::textColourId, Palette::text);
        setColour (juce::TextEditor::backgroundColourId, Palette::background);
        setColour (juce::TextEditor::outlineColourId, Palette::panelEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto lineWidth = juce::jmax (2.5f, radius * 0.16f);
        const auto arcRadius = radius - lineWidth * 0.5f;
        const auto angle = startAngle + pos * (endAngle - startAngle);
        const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::panelEdge);
        g.strokePath (track, { lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Bipolar knobs fill from the centre.
        const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, fromAngle, angle, true);
        g.setColour (colour);
        g.strokePath (value, { lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        const auto knobRadius = arcRadius - lineWidth * 1.1f;
        g.setColour (Palette::panel.brighter (0.15f));
        g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

        const auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
        g.setColour (Palette::text);
        g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.3f, angle), tip }, 2.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
        const auto on = button.getToggleState();

        g.setColour (on ? Palette::accent : Palette::panelEdge.brighter (highlighted ? 0.2f : 0.0f));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

        g.setColour (on ? juce::Colours::white : Palette::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override                        { return juce::FontOptions (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.0f, juce::Font::bold); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 5.0f);

        juce::Path arrow;
        const auto ax = (float) width - 12.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }
};
