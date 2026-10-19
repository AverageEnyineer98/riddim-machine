#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../ui/ControlPanel.h" // ParamMenu, ParamToggle
#include "ForgeLookAndFeel.h"
#include "ForgeProcessor.h"

// Riddim Forge's widgets. Knobs take modulation the way Serum's do: drag a source handle
// (an LFO, envelope or macro) onto a knob to route it, and the knob draws a ring for each
// route with a dot following the live value. Right-click a knob to change or remove routes.
namespace ForgeUI
{
    using APVTS = juce::AudioProcessorValueTreeState;

    juce::Colour sourceColour (Forge::ModSource);
    juce::String sourceShortName (Forge::ModSource);

    // The mod destination a parameter feeds, or ModDest::none when it can't be modulated.
    Forge::ModDest destForParameter (const juce::String& paramID);

    // Reads and writes the 16 mod slots through the parameters, so the host sees every change.
    struct ModSlots
    {
        explicit ModSlots (APVTS& s) : state (s) {}

        Forge::ModSource source (int slot) const;
        Forge::ModDest dest (int slot) const;
        float amount (int slot) const;
        bool bipolar (int slot) const;

        // Routes a source to a destination: reuses a slot with the same pair, else the first free one.
        // Returns the slot, or -1 when all 16 are taken.
        int connect (Forge::ModSource, Forge::ModDest, float amount = 0.5f);
        void setAmount (int slot, float amount);
        void clear (int slot);
        std::vector<int> slotsFor (Forge::ModDest) const;

        APVTS& state;

    private:
        void set (const juce::String& id, float plainValue);
    };

    // The live value (about -1..1) a source has right now, for the moving dots on mod rings.
    float liveSourceValue (ForgeProcessor&, Forge::ModSource);

    //==========================================================================
    // A slider that hands right-clicks to its owner instead of dragging.
    class ModSlider : public juce::Slider
    {
    public:
        using juce::Slider::Slider;
        std::function<void()> onRightClick;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool rightClicked = false;
    };

    // A HUD dial with its name and a live readout, a drop target for mod sources.
    class Knob : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
    {
    public:
        Knob (ForgeProcessor&, const juce::String& paramID, const juce::String& name, juce::Colour);
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails&) override;

    private:
        void timerCallback() override;
        void updateReadout();
        void showModMenu();

        ForgeProcessor& processor;
        ModSlots slots;
        Forge::ModDest dest;
        ModSlider slider;
        juce::Label name, value;
        APVTS::SliderAttachment attachment;
        juce::RangedAudioParameter* parameter;

        struct Ring { Forge::ModSource source; float amount; bool bipolar; float live; };
        std::vector<Ring> rings;
        bool dropHover = false;
    };

    // The little crosshair you drag from a mod source onto a knob.
    class SourceHandle : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SourceHandle (Forge::ModSource);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

        Forge::ModSource source;
    };

    // A glass panel with a lit top edge and a mono title. Its owner lays out the children
    // inside getContentBounds(), usually through `layout`.
    class Card : public juce::Component
    {
    public:
        Card (const juce::String& title, juce::Colour accent);

        juce::Rectangle<int> getContentBounds() const;
        juce::Colour getAccent() const { return accent; }
        void setTitle (const juce::String& t) { title = t.toUpperCase(); repaint(); }

        std::function<void (juce::Rectangle<int>)> layout;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::String title;
        juce::Colour accent;
    };

    // Lays components side by side in equal columns.
    void layoutRow (juce::Rectangle<int> area, const std::vector<juce::Component*>&, int gap = 4);

    //==========================================================================
    // The oscillator's wavetable as a stack of frames in perspective, the current frame lit
    // and drawn through both warps, like Serum's table view.
    class WavetableView : public juce::Component, private juce::Timer
    {
    public:
        WavetableView (ForgeProcessor&, int oscillator, juce::Colour);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;
        float sampleAt (int builtIn, const UserWavetable*, float position, double phase, bool warped) const;

        ForgeProcessor& processor;
        int oscIndex;
        juce::SharedResourcePointer<WavetableBank> bank;
        std::atomic<float> *table, *position, *warpMode[2], *warp[2];
        juce::Colour colour;
        std::array<float, 8> last {};
    };

    // The filter's magnitude response, measured by running an impulse through the real filter.
    class FilterView : public juce::Component, private juce::Timer
    {
    public:
        FilterView (APVTS&, std::function<juce::String (const char*)> idFor, juce::Colour);
        void paint (juce::Graphics&) override;
        void setIds (std::function<juce::String (const char*)>);

    private:
        void timerCallback() override;
        void measure();

        APVTS& state;
        std::function<juce::String (const char*)> idFor;
        juce::Colour colour;
        std::array<float, 6> last { -1.0f };
        std::vector<float> response; // dB per display column
    };

    // An ADSR curve drawn from four parameters.
    class EnvelopeView : public juce::Component, private juce::Timer
    {
    public:
        EnvelopeView (APVTS&, juce::Colour);
        void setIds (const juce::String& attack, const juce::String& decay, const juce::String& sustain, const juce::String& release);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override { repaint(); }

        APVTS& state;
        std::atomic<float> *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr;
        juce::Colour colour;
    };

    // Draw an LFO: drag points, double-click to add or remove one, drag a segment's middle
    // to bend it. Hold shift to snap to the grid. Editing a built-in shape turns it Custom.
    class LfoEditor : public juce::Component, private juce::Timer
    {
    public:
        LfoEditor (ForgeProcessor&, juce::Colour);
        void setLfo (int index);
        int getLfo() const { return lfo; }
        void setGrid (int divisions) { grid = divisions; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        juce::Rectangle<float> plotArea() const;
        juce::Point<float> toScreen (float x, float y) const;
        Forge::LfoCurve editableCurve();
        void commit();
        int hitPoint (juce::Point<float>) const;
        int hitSegment (juce::Point<float>) const;
        float shownValue (float x) const;

        ForgeProcessor& processor;
        juce::Colour colour;
        int lfo = 0, grid = 8;
        Forge::LfoCurve curve;
        int dragPoint = -1, bendSegment = -1, hoverPoint = -1;
        float bendStart = 0.0f;
        int shownShape = -1;
        float livePhase = 0.0f;
    };

    // Eight steps, one LFO 1 rate per hit; the step that will play next is lit.
    class HitSequencer : public juce::Component, private juce::Timer
    {
    public:
        explicit HitSequencer (ForgeProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        juce::Rectangle<int> cellBounds (int step) const;

        ForgeProcessor& processor;
        std::atomic<float>& length;
        std::vector<std::unique_ptr<ParamMenu>> rates;
        int shownStep = -2, shownLength = 0;
    };

    // The 16 routes: source, aux, amount, bipolar, destination.
    class ModMatrix : public juce::Component, private juce::Timer
    {
    public:
        explicit ModMatrix (APVTS&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override { repaint(); }

        struct Row
        {
            Row (APVTS&, int index);
            ParamMenu source, aux, dest;
            ParamToggle bipolar;
            juce::Slider amount;
            APVTS::SliderAttachment attachment;
            juce::TextButton clear { "x" };
        };

        APVTS& state;
        std::vector<std::unique_ptr<Row>> rows;
    };

    // A macro: knob, a name you can double-click to rename, and its drag handle.
    class MacroKnob : public juce::Component
    {
    public:
        MacroKnob (ForgeProcessor&, int index, juce::Colour);
        void resized() override;
        void refreshName();

    private:
        ForgeProcessor& processor;
        int index;
        Knob knob;
        juce::Label name;
        SourceHandle handle;
    };
}
