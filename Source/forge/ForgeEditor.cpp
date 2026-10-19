#include "ForgeEditor.h"
#include "../Parameters.h"

using namespace ForgeUI;
using namespace ForgePalette;

namespace
{
    constexpr int headerHeight = 60, statusHeight = 24, visualiserHeight = 190, keyboardHeight = 56;
    constexpr int pageHeight = 486, gap = 8, margin = 10;
    constexpr float numWhiteKeys = 43.0f; // C1 to C7

    const juce::Colour amber { 0xffffb84d };
    const juce::Colour orange { 0xffff7a3d };

    using Knobs = std::vector<std::unique_ptr<ForgeUI::Knob>>;

    Knobs makeKnobs (juce::Component& parent, APVTS& state, juce::Colour colour,
                     std::initializer_list<std::pair<juce::String, const char*>> items)
    {
        Knobs knobs;
        for (const auto& [id, name] : items)
        {
            knobs.push_back (std::make_unique<ForgeUI::Knob> (state, id, name, colour));
            parent.addAndMakeVisible (*knobs.back());
        }
        return knobs;
    }

    void layoutKnobs (juce::Rectangle<int> area, Knobs& knobs, int gapBetween = 2)
    {
        if (knobs.empty())
            return;

        const auto width = (area.getWidth() - gapBetween * ((int) knobs.size() - 1)) / (int) knobs.size();
        for (auto& k : knobs)
        {
            k->setBounds (area.removeFromLeft (width));
            area.removeFromLeft (gapBetween);
        }
    }

    // Splits a row into cards with the given relative widths.
    void layoutCards (juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Component*, float>> cards)
    {
        auto total = 0.0f;
        for (const auto& c : cards)
            total += c.second;

        const auto available = (float) (row.getWidth() - gap * ((int) cards.size() - 1));
        for (const auto& [card, weight] : cards)
        {
            card->setBounds (row.removeFromLeft (juce::roundToInt (available * weight / total)));
            row.removeFromLeft (gap);
        }
    }

    //==========================================================================
    // One oscillator: on / table / import, a scope of the warped frame, both warp slots, then the knobs.
    class OscCard : public Card
    {
    public:
        OscCard (ForgeProcessor& p, char osc, juce::Colour colour)
            : Card ("Oscillator " + juce::String::charToString (osc), colour),
              processor (p), index (osc == 'A' ? 0 : 1),
              on (p.parameters, ForgeID::osc (osc, ForgeID::on), "On"),
              table (p.parameters, ForgeID::osc (osc, ForgeID::table), forgeTableNames()),
              warpMenus { { p.parameters, ForgeID::osc (osc, ForgeID::warp1Mode), Forge::warpNames() },
                          { p.parameters, ForgeID::osc (osc, ForgeID::warp2Mode), Forge::warpNames() } },
              scope (p, osc, colour)
        {
            auto& s = p.parameters;
            warpKnobs.push_back (std::make_unique<ForgeUI::Knob> (s, ForgeID::osc (osc, ForgeID::warp1), "Warp 1", colour));
            warpKnobs.push_back (std::make_unique<ForgeUI::Knob> (s, ForgeID::osc (osc, ForgeID::warp2), "Warp 2", colour));
            knobs = makeKnobs (*this, s, colour, { { ForgeID::osc (osc, ForgeID::pos), "Position" }, { ForgeID::osc (osc, ForgeID::level), "Level" },
                                                   { ForgeID::osc (osc, ForgeID::octave), "Octave" }, { ForgeID::osc (osc, ForgeID::semi), "Semi" },
                                                   { ForgeID::osc (osc, ForgeID::fine), "Fine" }, { ForgeID::osc (osc, ForgeID::unison), "Unison" },
                                                   { ForgeID::osc (osc, ForgeID::detune), "Detune" }, { ForgeID::osc (osc, ForgeID::blend), "Blend" },
                                                   { ForgeID::osc (osc, ForgeID::width), "Width" } });

            importButton.setButtonText ("IMPORT");
            importButton.setTooltip ("Load a wavetable WAV (2048-sample frames, the common wavetable format)");
            importButton.onClick = [this] { chooseWavetable(); };
            on.button.setTooltip ("Oscillator on / off");

            for (auto* c : std::initializer_list<juce::Component*> { &on.button, &table.box, &importButton, &scope,
                                                                       &warpMenus[0].box, &warpMenus[1].box })
                addAndMakeVisible (c);
            for (auto& k : warpKnobs)
                addAndMakeVisible (*k);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto strip = area.removeFromTop (24);
                on.button.setBounds (strip.removeFromLeft (52));
                strip.removeFromLeft (6);
                importButton.setBounds (strip.removeFromRight (76));
                strip.removeFromRight (6);
                table.box.setBounds (strip);

                area.removeFromTop (6);
                auto middle = area.removeFromTop (100);
                scope.setBounds (middle.removeFromLeft ((int) (middle.getWidth() * 0.46f)));
                middle.removeFromLeft (10);

                const auto columnWidth = (middle.getWidth() - 8) / 2;
                for (int w = 0; w < 2; ++w)
                {
                    auto column = middle.removeFromLeft (columnWidth);
                    middle.removeFromLeft (8);
                    warpMenus[w].box.setBounds (column.removeFromTop (22));
                    warpKnobs[(size_t) w]->setBounds (column);
                }

                area.removeFromTop (4);
                layoutKnobs (area, knobs);
            };
        }

    private:
        void chooseWavetable()
        {
            chooser = std::make_unique<juce::FileChooser> ("Import a wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;

                juce::String error;
                if (processor.importWavetable (index, file, error))
                    scope.repaint();
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", error);
            });
        }

        ForgeProcessor& processor;
        int index;
        ParamToggle on;
        ParamMenu table;
        ParamMenu warpMenus[2];
        juce::TextButton importButton;
        WarpScope scope;
        Knobs warpKnobs, knobs;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    // A card holding an optional strip of widgets over a row of knobs, with an optional scope between.
    class KnobCard : public Card
    {
    public:
        KnobCard (const juce::String& cardTitle, juce::Colour colour) : Card (cardTitle, colour)
        {
            layout = [this] (juce::Rectangle<int> area)
            {
                if (! strip.empty())
                {
                    auto row = area.removeFromTop (24);
                    const auto width = (row.getWidth() - 6 * ((int) strip.size() - 1)) / (int) strip.size();
                    for (auto* c : strip)
                    {
                        c->setBounds (row.removeFromLeft (width));
                        row.removeFromLeft (6);
                    }
                    area.removeFromTop (6);
                }

                if (scope != nullptr)
                {
                    scope->setBounds (area.removeFromTop (scopeHeight));
                    area.removeFromTop (6);
                }

                layoutKnobs (area.withSizeKeepingCentre (juce::jmin (area.getWidth(), (int) knobs.size() * 86), area.getHeight()), knobs);
            };
        }

        void addToStrip (juce::Component& c) { strip.push_back (&c); addAndMakeVisible (c); }
        void setScope (juce::Component& c, int height) { scope = &c; scopeHeight = height; addAndMakeVisible (c); }

        Knobs knobs;

    private:
        std::vector<juce::Component*> strip;
        juce::Component* scope = nullptr;
        int scopeHeight = 0;
    };

    //==========================================================================
    class SynthPage : public juce::Component
    {
    public:
        explicit SynthPage (ForgeProcessor& p)
            : oscA (p, 'A', cyan), oscB (p, 'B', violet),
              sub ("Sub / Noise", lime), filter ("Filter", cyan), amp ("Amp Envelope", lime), voice ("Voice", magenta),
              subOctave (p.parameters, ForgeID::subOctave, { "SUB OCT 0", "SUB OCT -1" }),
              filterType (p.parameters, ForgeID::filterType, Forge::filterTypeNames()),
              mono (p.parameters, ForgeID::mono, "Mono / Legato"),
              ampScope (p.parameters, ForgeID::attack, ForgeID::decay, ForgeID::sustain, ForgeID::release, lime)
        {
            auto& s = p.parameters;
            sub.addToStrip (subOctave.box);
            sub.knobs = makeKnobs (sub, s, lime, { { ForgeID::subLevel, "Sub" }, { ForgeID::noise, "Noise" } });

            filter.addToStrip (filterType.box);
            filter.knobs = makeKnobs (filter, s, cyan, { { ForgeID::cutoff, "Cutoff" }, { ForgeID::resonance, "Reso" },
                                                         { ForgeID::drive, "Drive" }, { ForgeID::keyTrack, "Key Track" } });

            amp.setScope (ampScope, 54);
            amp.knobs = makeKnobs (amp, s, lime, { { ForgeID::attack, "Attack" }, { ForgeID::decay, "Decay" },
                                                   { ForgeID::sustain, "Sustain" }, { ForgeID::release, "Release" } });

            voice.addToStrip (mono.button);
            voice.knobs = makeKnobs (voice, s, magenta, { { ForgeID::glide, "Glide" }, { ForgeID::bendRange, "Bend" },
                                                          { ForgeID::pitchEnv, "Pitch Env" }, { ForgeID::pitchDecay, "P. Decay" } });

            for (auto* c : std::initializer_list<juce::Component*> { &oscA, &oscB, &sub, &filter, &amp, &voice })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            layoutCards (area.removeFromTop (264), { { &oscA, 5.0f }, { &oscB, 5.0f }, { &sub, 1.9f } });
            area.removeFromTop (gap);
            layoutCards (area, { { &filter, 1.0f }, { &amp, 1.0f }, { &voice, 1.0f } });
        }

    private:
        OscCard oscA, oscB;
        KnobCard sub, filter, amp, voice;
        ParamMenu subOctave, filterType;
        ParamToggle mono;
        EnvelopeScope ampScope;
    };

    //==========================================================================
    class ModPage : public juce::Component
    {
    public:
        explicit ModPage (ForgeProcessor& p)
            : lfo1 ("LFO 1  //  Wobble", cyan), lfo2 ("LFO 2", violet), env2 ("Envelope 2", lime),
              hits ("Hit Sequencer  //  LFO 1 rate per hit", magenta), matrix ("Mod Matrix", cyan),
              lfoShape { { p.parameters, ForgeID::lfo (0, ForgeID::shape), Choices::lfoShapes }, { p.parameters, ForgeID::lfo (1, ForgeID::shape), Choices::lfoShapes } },
              lfoRate { { p.parameters, ForgeID::lfo (0, ForgeID::rate), Choices::lfoRates }, { p.parameters, ForgeID::lfo (1, ForgeID::rate), Choices::lfoRates } },
              lfoRetrig { { p.parameters, ForgeID::lfo (0, ForgeID::retrig), "Retrig" }, { p.parameters, ForgeID::lfo (1, ForgeID::retrig), "Retrig" } },
              lfoScopes { { p.parameters, 0, &p.visualFeed, cyan }, { p.parameters, 1, nullptr, violet } },
              env2Scope (p.parameters, ForgeID::env2Attack, ForgeID::env2Decay, ForgeID::env2Sustain, ForgeID::env2Release, lime),
              hitOn (p.parameters, ForgeID::hitSeqOn, "On"),
              hitLength (p.parameters, ForgeID::hitSeqLen, "Length", magenta),
              sequencer (p), routing (p.parameters)
        {
            KnobCard* lfoCards[] { &lfo1, &lfo2 };
            for (int l = 0; l < 2; ++l)
            {
                lfoCards[l]->addToStrip (lfoShape[l].box);
                lfoCards[l]->addToStrip (lfoRate[l].box);
                lfoCards[l]->addToStrip (lfoRetrig[l].button);
                lfoCards[l]->setScope (lfoScopes[l], 128);
            }
            lfoRate[0].box.setTooltip ("Used when the hit sequencer is off");

            env2.setScope (env2Scope, 54);
            env2.knobs = makeKnobs (env2, p.parameters, lime, { { ForgeID::env2Attack, "Attack" }, { ForgeID::env2Decay, "Decay" },
                                                                { ForgeID::env2Sustain, "Sustain" }, { ForgeID::env2Release, "Release" } });

            hitOn.button.setTooltip ("Every new note takes the next rate, so one MIDI clip plays a riddim pattern");
            for (auto* c : std::initializer_list<juce::Component*> { &hitOn.button, &hitLength, &sequencer })
                hits.addAndMakeVisible (c);
            hits.layout = [this] (juce::Rectangle<int> area)
            {
                auto side = area.removeFromLeft (76);
                hitOn.button.setBounds (side.removeFromTop (24));
                side.removeFromTop (8);
                hitLength.setBounds (side.removeFromTop (88));
                area.removeFromLeft (10);
                sequencer.setBounds (area);
            };

            matrix.addAndMakeVisible (routing);
            matrix.layout = [this] (juce::Rectangle<int> area) { routing.setBounds (area); };

            for (auto* c : std::initializer_list<juce::Component*> { &lfo1, &lfo2, &env2, &hits, &matrix })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            layoutCards (area.removeFromTop (220), { { &lfo1, 1.0f }, { &lfo2, 1.0f }, { &env2, 1.0f } });
            area.removeFromTop (gap);
            layoutCards (area, { { &hits, 1.0f }, { &matrix, 1.25f } });
        }

    private:
        KnobCard lfo1, lfo2, env2;
        Card hits, matrix;
        ParamMenu lfoShape[2], lfoRate[2];
        ParamToggle lfoRetrig[2];
        LfoScope lfoScopes[2];
        EnvelopeScope env2Scope;
        ParamToggle hitOn;
        ForgeUI::Knob hitLength;
        HitSequencer sequencer;
        ModMatrix routing;
    };

    //==========================================================================
    class FxPage : public juce::Component
    {
    public:
        explicit FxPage (ForgeProcessor& p)
            : distortion ("Distortion", orange), formant ("Formant  //  Yoi", magenta), comb ("Comb", violet),
              smash ("Smash / Clip", cyan), tone ("Tone / Low End", lime), output ("Heat / Output", amber),
              distType (p.parameters, ParamID::distType, Choices::distTypes),
              combTrack (p.parameters, ParamID::combTrack, "Key Track"),
              resample (p.parameters, ParamID::resample, "Resample"),
              gate (p.parameters, ParamID::gate, "Gate")
        {
            auto& s = p.parameters;
            distortion.addToStrip (distType.box);
            distortion.knobs = makeKnobs (distortion, s, orange, { { ParamID::distDrive, "Drive" }, { ParamID::distMix, "Mix" } });
            formant.knobs = makeKnobs (formant, s, magenta, { { ParamID::vowelMix, "Mix" }, { ParamID::vowel, "Vowel" } });
            comb.addToStrip (combTrack.button);
            comb.knobs = makeKnobs (comb, s, violet, { { ParamID::combMix, "Mix" }, { ParamID::combFreq, "Freq" }, { ParamID::combFeedback, "Feedback" } });
            smash.knobs = makeKnobs (smash, s, cyan, { { ParamID::squash, "Smash" }, { ParamID::clip, "Clip" } });
            tone.knobs = makeKnobs (tone, s, lime, { { ParamID::tilt, "Tilt" }, { ParamID::lowKeep, "Low Keep" } });
            output.addToStrip (resample.button);
            output.addToStrip (gate.button);
            output.knobs = makeKnobs (output, s, amber, { { ParamID::heat, "Heat" }, { ParamID::fxMix, "FX Mix" }, { ParamID::master, "Output" } });

            resample.button.setTooltip ("Run distortion and smash a second time, like bouncing and processing again");
            gate.button.setTooltip ("Keep the gaps between hits silent");

            for (auto* c : std::initializer_list<juce::Component*> { &distortion, &formant, &comb, &smash, &tone, &output })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            const auto rowHeight = (area.getHeight() - gap) / 2;
            layoutCards (area.removeFromTop (rowHeight), { { &distortion, 1.0f }, { &formant, 1.0f }, { &comb, 1.2f } });
            area.removeFromTop (gap);
            layoutCards (area, { { &smash, 1.0f }, { &tone, 1.0f }, { &output, 1.2f } });
        }

    private:
        KnobCard distortion, formant, comb, smash, tone, output;
        ParamMenu distType;
        ParamToggle combTrack, resample, gate;
    };
}

//==============================================================================
ForgeEditor::ForgeEditor (ForgeProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetBar (p.presets),
      visualiser (p.visualFeed, p.keyboardState),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);

    pages[0] = std::make_unique<SynthPage> (p);
    pages[1] = std::make_unique<ModPage> (p);
    pages[2] = std::make_unique<FxPage> (p);

    const char* names[] { "SYNTH", "MOD", "FX" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (1001);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
        addChildComponent (*pages[i]);
    }

    keyboard.setAvailableRange (24, 96);
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyPressBaseOctave (3);
    keyboard.setOctaveForMiddleC (4);

    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &visualiser, &keyboard })
        addAndMakeVisible (c);

    showPage (0);
    setResizable (true, true);
    setResizeLimits (1180, 800, 2400, 1600);
    setSize (1280, headerHeight + statusHeight + visualiserHeight + pageHeight + keyboardHeight + gap * 3);
    startTimerHz (20);
}

ForgeEditor::~ForgeEditor()
{
    setLookAndFeel (nullptr);
}

void ForgeEditor::showPage (int index)
{
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setToggleState (i == index, juce::dontSendNotification);
        pages[i]->setVisible (i == index);
    }
}

void ForgeEditor::timerCallback()
{
    pulse = std::fmod (pulse + 0.05f, 1.0f);
    shownHit = processor.getHitStep();
    repaint (statusArea);
}

void ForgeEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Deep space background with two soft glows and a faint dot grid.
    g.fillAll (void_);
    g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.10f), 0.0f, 0.0f,
                                             cyan.withAlpha (0.0f), bounds.getWidth() * 0.45f, bounds.getHeight() * 0.45f, true));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.10f), bounds.getRight(), bounds.getBottom(),
                                             violet.withAlpha (0.0f), bounds.getWidth() * 0.5f, bounds.getHeight() * 0.5f, true));
    g.fillRect (bounds);

    g.setColour (cyan.withAlpha (0.05f));
    for (int y = headerHeight; y < getHeight(); y += 16)
        for (int x = 8; x < getWidth(); x += 16)
            g.fillRect (x, y, 1, 1);

    // Header: logo, subtitle and a lit divider.
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (18, 0);
    g.setGradientFill (juce::ColourGradient (cyan, (float) header.getX(), 0.0f, violet, (float) header.getX() + 240.0f, 0.0f, false));
    g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    g.drawText ("RIDDIM FORGE", header.withHeight (40).withY (8), juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (mono (10.0f).withExtraKerningFactor (0.2f));
    g.drawText ("RIDDIM BASS ENGINE  //  v0.1", header.withHeight (14).withY (40), juce::Justification::centredLeft);

    g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.0f), 0.0f, 0.0f, cyan.withAlpha (0.6f), bounds.getCentreX(), 0.0f, false));
    g.fillRect (0.0f, (float) headerHeight - 1.0f, bounds.getCentreX(), 1.0f);
    g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.6f), bounds.getCentreX(), 0.0f, violet.withAlpha (0.0f), bounds.getRight(), 0.0f, false));
    g.fillRect (bounds.getCentreX(), (float) headerHeight - 1.0f, bounds.getCentreX(), 1.0f);

    // Status strip: a pulsing "online" light, engine facts and the hit sequencer step.
    const auto status = statusArea.toFloat();
    const auto glow = 0.5f + 0.5f * std::sin (pulse * juce::MathConstants<float>::twoPi);
    g.setColour (lime.withAlpha (0.2f + 0.3f * glow));
    g.fillEllipse (status.getX() + 2.0f, status.getCentreY() - 6.0f, 12.0f, 12.0f);
    g.setColour (lime);
    g.fillEllipse (status.getX() + 5.0f, status.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setFont (mono (10.0f, true).withExtraKerningFactor (0.15f));
    g.setColour (lime);
    g.drawText ("ENGINE ONLINE", status.withTrimmedLeft (22.0f), juce::Justification::centredLeft);

    const auto sampleRate = processor.getSampleRate();
    const auto mono = processor.parameters.getRawParameterValue (ForgeID::mono)->load() > 0.5f;
    g.setColour (textDim);
    g.drawText (juce::String (sampleRate > 0.0 ? sampleRate / 1000.0 : 48.0, 1) + " KHZ  //  " + (mono ? "MONO LEGATO" : "POLY 8")
                    + "  //  17 WARP MODES  //  WAVETABLE IMPORT",
                status, juce::Justification::centred);

    const auto seqOn = processor.parameters.getRawParameterValue (ForgeID::hitSeqOn)->load() > 0.5f;
    const auto length = juce::roundToInt (processor.parameters.getRawParameterValue (ForgeID::hitSeqLen)->load());
    g.setColour (seqOn ? magenta : textDim);
    g.drawText (seqOn ? "HIT " + juce::String (juce::jmax (0, shownHit) + 1).paddedLeft ('0', 2) + " / " + juce::String (length).paddedLeft ('0', 2)
                      : juce::String ("HIT SEQ OFF"),
                status, juce::Justification::centredRight);

    // HUD brackets around the 3D view.
    const auto view = visualiser.getBounds().toFloat().expanded (3.0f);
    g.setColour (cyan.withAlpha (0.7f));
    for (const auto corner : { view.getTopLeft(), view.getTopRight(), view.getBottomLeft(), view.getBottomRight() })
    {
        const auto dx = corner.x < view.getCentreX() ? 1.0f : -1.0f;
        const auto dy = corner.y < view.getCentreY() ? 1.0f : -1.0f;
        g.drawLine (corner.x, corner.y, corner.x + dx * 16.0f, corner.y, 1.5f);
        g.drawLine (corner.x, corner.y, corner.x, corner.y + dy * 16.0f, 1.5f);
    }
}

void ForgeEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (18, 14);
    presetBar.setBounds (header.removeFromRight (400));
    auto tabArea = header.withSizeKeepingCentre (3 * 96 + 2 * 6, header.getHeight());
    for (auto& tab : tabs)
    {
        tab.setBounds (tabArea.removeFromLeft (96));
        tabArea.removeFromLeft (6);
    }

    statusArea = area.removeFromTop (statusHeight).reduced (margin + 4, 3);

    keyboard.setBounds (area.removeFromBottom (keyboardHeight));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / numWhiteKeys);
    area.removeFromBottom (gap);

    const auto pageArea = area.removeFromBottom (juce::jmax (pageHeight, area.getHeight() - visualiserHeight - gap)).reduced (margin, 0);
    for (auto& page : pages)
        page->setBounds (pageArea);

    area.removeFromBottom (gap);
    visualiser.setBounds (area.reduced (margin + 4, 4));
}
