#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Riddim Forge's HUD look: deep navy glass panels, cyan-to-violet glowing arcs, mono readouts.
namespace ForgePalette
{
    inline const juce::Colour void_     { 0xff04060c };
    inline const juce::Colour deep      { 0xff081022 };
    inline const juce::Colour glass     { 0xc00c1630 };
    inline const juce::Colour glassEdge { 0xff1a3358 };
    inline const juce::Colour cyan      { 0xff22e6ff };
    inline const juce::Colour violet    { 0xffa855ff };
    inline const juce::Colour magenta   { 0xffff3dcb };
    inline const juce::Colour lime      { 0xff7dffb0 };
    inline const juce::Colour text      { 0xffd8f6ff };
    inline const juce::Colour textDim   { 0xff6b86a6 };
    inline const juce::Colour track     { 0xff142540 };

    inline juce::Font mono (float size, bool bold = false)
    {
        return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain);
    }

    // A soft glow: the shape stroked a few times, wide and faint to narrow and bright.
    inline void glowStroke (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (colour.withAlpha (0.08f * (float) (4 - i)));
            g.strokePath (path, { width + (float) i * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }
        g.setColour (colour);
        g.strokePath (path, { width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }
}

class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel()
    {
        using namespace ForgePalette;
        setColour (juce::ComboBox::backgroundColourId, deep);
        setColour (juce::ComboBox::outlineColourId, glassEdge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, cyan);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf2070d1c));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, cyan);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::TextButton::buttonColourId, deep);
        setColour (juce::TextButton::buttonOnColourId, cyan);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, void_);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf2070d1c));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, cyan.withAlpha (0.5f));
        setColour (juce::AlertWindow::backgroundColourId, deep);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, cyan.withAlpha (0.5f));
        setColour (juce::TextEditor::backgroundColourId, void_);
        setColour (juce::TextEditor::outlineColourId, glassEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, cyan);
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf2070d1c));
        setColour (juce::BubbleComponent::outlineColourId, cyan.withAlpha (0.6f));
        setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffcfe9ff));
        setColour (juce::MidiKeyboardComponent::blackNoteColourId, deep);
        setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, glassEdge);
        setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, cyan.withAlpha (0.75f));
        setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, violet.withAlpha (0.3f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace ForgePalette;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (5.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto lineWidth = juce::jmax (2.0f, radius * 0.11f);
        const auto arcRadius = radius - lineWidth;
        const auto angle = startAngle + pos * (endAngle - startAngle);

        // Faint tick ring, like a HUD dial.
        g.setColour (glassEdge);
        for (int i = 0; i <= 20; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / 20.0f;
            g.drawLine ({ centre.getPointOnCircumference (radius + 1.0f, a), centre.getPointOnCircumference (radius + (i % 5 == 0 ? 4.0f : 2.5f), a) }, 1.0f);
        }

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (ForgePalette::track);
        g.strokePath (track, { lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Bipolar knobs fill from the centre.
        const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

        if (std::abs (angle - fromAngle) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, fromAngle, angle, true);
            glowStroke (g, value, colour, lineWidth);
        }

        // Inner glass disc with a lit tip.
        const auto discRadius = arcRadius - lineWidth * 1.6f;
        juce::ColourGradient disc (juce::Colour (0xff17284a), centre.x, centre.y - discRadius,
                                   juce::Colour (0xff060b18), centre.x, centre.y + discRadius, false);
        g.setGradientFill (disc);
        g.fillEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);
        g.setColour (colour.withAlpha (slider.isMouseOverOrDragging() ? 0.7f : 0.3f));
        g.drawEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f, 1.0f);

        const auto tip = centre.getPointOnCircumference (discRadius * 0.72f, angle);
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (tip.x - 4.0f, tip.y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tip.x - 1.8f, tip.y - 1.8f, 3.6f, 3.6f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        using namespace ForgePalette;
        const auto bar = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 3.0f, (float) width, 6.0f);
        g.setColour (ForgePalette::track);
        g.fillRoundedRectangle (bar, 3.0f);

        const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto from = bipolar ? bar.getCentreX() : bar.getX();
        const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, sliderPos), bar.getY(),
                                                                      juce::jmax (from, sliderPos), bar.getBottom());
        const auto colour = sliderPos >= from ? cyan : magenta;
        g.setColour (colour.withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (0.0f, 3.0f), 4.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 3.0f);

        if (bipolar)
        {
            g.setColour (textDim);
            g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
        }

        g.setColour (juce::Colours::white);
        g.fillEllipse (sliderPos - 4.0f, bar.getCentreY() - 4.0f, 8.0f, 8.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        using namespace ForgePalette;
        const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
        const auto on = button.getToggleState();
        const auto corner = bounds.getHeight() * 0.5f;

        if (on)
        {
            g.setColour (cyan.withAlpha (0.18f));
            g.fillRoundedRectangle (bounds.expanded (1.5f), corner + 1.5f);
            juce::ColourGradient fill (cyan, bounds.getX(), 0.0f, violet, bounds.getRight(), 0.0f, false);
            g.setGradientFill (fill);
            g.fillRoundedRectangle (bounds, corner);
        }
        else
        {
            g.setColour (deep);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (highlighted ? cyan.withAlpha (0.6f) : glassEdge);
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }

        g.setColour (on ? void_ : textDim);
        g.setFont (mono (11.0f, true));
        g.drawText (button.getButtonText().toUpperCase(), bounds, juce::Justification::centred);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override
    {
        using namespace ForgePalette;
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto on = button.getToggleState();

        if (on)
        {
            juce::ColourGradient fill (cyan.withAlpha (0.25f), bounds.getX(), bounds.getY(),
                                       violet.withAlpha (0.25f), bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill (fill);
        }
        else
        {
            g.setColour (down ? cyan.withAlpha (0.2f) : deep);
        }
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (on ? cyan : (highlighted ? cyan.withAlpha (0.6f) : glassEdge));
        g.drawRoundedRectangle (bounds, 5.0f, on ? 1.4f : 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        using namespace ForgePalette;
        g.setFont (mono (12.0f, true));
        g.setColour (button.getToggleState() ? cyan : text);
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return ForgePalette::mono (12.0f); }
    juce::Font getPopupMenuFont() override               { return juce::FontOptions (14.0f); }
    juce::Font getAlertWindowMessageFont() override      { return juce::FontOptions (14.0f); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace ForgePalette;
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (deep);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.isMouseOver (true) ? cyan.withAlpha (0.7f) : glassEdge);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        juce::Path chevron;
        const auto ax = (float) width - (width < 80 ? 8.0f : 11.0f), ay = (float) height * 0.5f;
        chevron.startNewSubPath (ax - 3.5f, ay - 1.5f);
        chevron.lineTo (ax, ay + 2.0f);
        chevron.lineTo (ax + 3.5f, ay - 1.5f);
        g.setColour (cyan);
        g.strokePath (chevron, juce::PathStrokeType (1.5f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        // Narrow boxes (the hit sequencer's rates) give the text more room and a smaller font.
        const auto narrow = box.getWidth() < 80;
        label.setBounds (narrow ? 2 : 6, 1, box.getWidth() - (narrow ? 14 : 22), box.getHeight() - 2);
        label.setFont (narrow ? ForgePalette::mono (10.5f) : getComboBoxFont (box));
        label.setMinimumHorizontalScale (0.8f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (ForgePalette::cyan.withAlpha (0.4f));
        g.drawRect (0, 0, width, height, 1);
    }

    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        g.setColour (findColour (juce::BubbleComponent::backgroundColourId));
        g.fillRoundedRectangle (body, 4.0f);
        g.setColour (findColour (juce::BubbleComponent::outlineColourId));
        g.drawRoundedRectangle (body, 4.0f, 1.0f);
    }

    juce::Font getSliderPopupFont (juce::Slider&) override { return ForgePalette::mono (12.0f); }
};
