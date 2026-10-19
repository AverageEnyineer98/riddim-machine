#include "ForgeEditor.h"
#include "ForgeMagic.h"
#include "../Parameters.h"

using namespace ForgeUI;
using namespace ForgePalette;

namespace
{
    constexpr int margin = 8, gap = 6;
    constexpr int headerHeight = 54, tabHeight = 28, stripHeight = 236, keyboardHeight = 52;
    constexpr int visualWidth = 372;
    constexpr float numWhiteKeys = 43.0f; // C1 to C7

    const juce::Colour amber { 0xffffb84d };
    const juce::Colour orange { 0xffff7a3d };
    const juce::Colour oscColours[] { cyan, violet, magenta };

    using KnobList = std::vector<std::unique_ptr<ForgeUI::Knob>>;

    struct KnobSpec { juce::String id; juce::String name; };

    KnobList makeKnobs (juce::Component& parent, ForgeProcessor& p, juce::Colour colour, const std::vector<KnobSpec>& specs)
    {
        KnobList knobs;
        for (const auto& s : specs)
        {
            knobs.push_back (std::make_unique<ForgeUI::Knob> (p, s.id, s.name, colour));
            parent.addAndMakeVisible (*knobs.back());
        }
        return knobs;
    }

    std::vector<juce::Component*> raw (KnobList& knobs, size_t from = 0, size_t to = 1000)
    {
        std::vector<juce::Component*> list;
        for (size_t i = from; i < juce::jmin (to, knobs.size()); ++i)
            list.push_back (knobs[i].get());
        return list;
    }

    // A small mono caption over a control.
    class Caption : public juce::Label
    {
    public:
        explicit Caption (const juce::String& t)
        {
            setText (t.toUpperCase(), juce::dontSendNotification);
            setFont (mono (8.5f, true));
            setColour (juce::Label::textColourId, textDim);
            setJustificationType (juce::Justification::centredLeft);
            setBorderSize ({});
            setInterceptsMouseClicks (false, false);
        }
    };

    //==========================================================================
    class OscPanel : public Card
    {
    public:
        OscPanel (ForgeProcessor& p, int index)
            : Card ("Osc " + juce::String::charToString (ForgeID::oscLetter (index)), oscColours[index]),
              processor (p), osc (index),
              on (p.parameters, id (ForgeID::on), "On"),
              table (p.parameters, id (ForgeID::table), forgeTableNames()),
              warpMenus { { p.parameters, id (ForgeID::warp1Mode), Forge::warpNames() },
                          { p.parameters, id (ForgeID::warp2Mode), Forge::warpNames() } },
              source (p.parameters, id (ForgeID::warpSrc), Forge::warpSourceNames()),
              route (p.parameters, id (ForgeID::route), Forge::routeNames()),
              view (p, index, oscColours[index])
        {
            const auto colour = oscColours[index];
            warpKnobs = makeKnobs (*this, p, colour, { { id (ForgeID::warp1), "Warp 1" }, { id (ForgeID::warp2), "Warp 2" } });
            knobs = makeKnobs (*this, p, colour, { { id (ForgeID::pos), "WT Pos" }, { id (ForgeID::level), "Level" }, { id (ForgeID::pan), "Pan" },
                                                   { id (ForgeID::unison), "Unison" }, { id (ForgeID::detune), "Detune" },
                                                   { id (ForgeID::octave), "Oct" }, { id (ForgeID::semi), "Semi" }, { id (ForgeID::fine), "Fine" },
                                                   { id (ForgeID::blend), "Blend" }, { id (ForgeID::width), "Width" } });

            importButton.setButtonText ("+WAV");
            importButton.setTooltip ("Load a wavetable WAV, e.g. one from your own Serum tables folder");
            importButton.onClick = [this] { chooseWavetable(); };
            on.button.setTooltip ("Oscillator on / off");
            source.box.setTooltip ("What FM / AM / RM / PD warps listen to");
            route.box.setTooltip ("Which filter this oscillator feeds");

            for (auto* c : std::initializer_list<juce::Component*> { &on.button, &table.box, &importButton, &view, &warpMenus[0].box,
                                                                       &warpMenus[1].box, &source.box, &route.box, &srcCaption, &routeCaption })
                addAndMakeVisible (c);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto strip = area.removeFromTop (22);
                on.button.setBounds (strip.removeFromLeft (40));
                strip.removeFromLeft (4);
                importButton.setBounds (strip.removeFromRight (44));
                strip.removeFromRight (4);
                table.box.setBounds (strip);

                area.removeFromTop (5);
                view.setBounds (area.removeFromTop (area.getHeight() - 22 - 58 - 2 * 62 - 10));
                area.removeFromTop (5);

                auto menus = area.removeFromTop (22);
                layoutRow (menus, { &warpMenus[0].box, &warpMenus[1].box }, 6);
                auto warpRow = area.removeFromTop (58);
                auto right = warpRow.removeFromRight (warpRow.getWidth() / 3);
                layoutRow (warpRow, raw (warpKnobs), 2);
                right.removeFromTop (2);
                srcCaption.setBounds (right.removeFromTop (10));
                source.box.setBounds (right.removeFromTop (20));
                right.removeFromTop (2);
                routeCaption.setBounds (right.removeFromTop (10));
                route.box.setBounds (right.removeFromTop (20));

                area.removeFromTop (3);
                layoutRow (area.removeFromTop (62), raw (knobs, 0, 5), 0);
                layoutRow (area.removeFromTop (62), raw (knobs, 5, 10), 0);
            };
        }

    private:
        juce::String id (const char* name) const { return ForgeID::osc (ForgeID::oscLetter (osc), name); }

        void chooseWavetable()
        {
            auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Xfer").getChildFile ("Serum Presets").getChildFile ("Tables");
            if (! start.isDirectory())
                start = juce::File();

            chooser = std::make_unique<juce::FileChooser> ("Import a wavetable", start, "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;

                juce::String error;
                if (processor.importWavetable (osc, file, error))
                    view.repaint();
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", error);
            });
        }

        ForgeProcessor& processor;
        int osc;
        ParamToggle on;
        ParamMenu table;
        ParamMenu warpMenus[2];
        ParamMenu source, route;
        Caption srcCaption { "Warp src" }, routeCaption { "Route" };
        juce::TextButton importButton;
        WavetableView view;
        KnobList warpKnobs, knobs;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==========================================================================
    class SubNoisePanel : public Card
    {
    public:
        explicit SubNoisePanel (ForgeProcessor& p)
            : Card ("Sub / Noise", lime),
              subOn (p.parameters, ForgeID::subOn, "Sub"),
              subShape (p.parameters, ForgeID::subShape, Forge::subShapeNames()),
              subOctave (p.parameters, ForgeID::subOctave, { "0", "-1", "-2" }),
              subRoute (p.parameters, ForgeID::subRoute, Forge::routeNames()),
              noiseType (p.parameters, ForgeID::noiseType, Forge::noiseTypeNames()),
              noiseRoute (p.parameters, ForgeID::noiseRoute, Forge::routeNames())
        {
            knobs = makeKnobs (*this, p, lime, { { ForgeID::subLevel, "Sub" }, { ForgeID::noise, "Noise" }, { ForgeID::noiseColour, "Colour" } });
            subOctave.box.setTooltip ("Sub octave");
            subRoute.box.setTooltip ("Sub route");
            noiseRoute.box.setTooltip ("Noise route");

            for (auto* c : std::initializer_list<juce::Component*> { &subOn.button, &subShape.box, &subOctave.box, &subRoute.box, &noiseType.box, &noiseRoute.box })
                addAndMakeVisible (c);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto row = area.removeFromTop (22);
                subOn.button.setBounds (row.removeFromLeft (46));
                row.removeFromLeft (4);
                layoutRow (row, { &subShape.box, &subOctave.box, &subRoute.box }, 4);
                area.removeFromTop (4);
                auto bottom = area.removeFromBottom (22);
                layoutRow (bottom, { &noiseType.box, &noiseRoute.box }, 4);
                layoutRow (area, raw (knobs), 0);
            };
        }

    private:
        ParamToggle subOn;
        ParamMenu subShape, subOctave, subRoute, noiseType, noiseRoute;
        KnobList knobs;
    };

    //==========================================================================
    class FilterControls : public juce::Component
    {
    public:
        FilterControls (ForgeProcessor& p, int i)
            : index (i),
              on (p.parameters, id (ForgeID::on), "On"),
              type (p.parameters, id (ForgeID::type), Forge::filterTypeNames()),
              view (p.parameters, [i] (const char* n) { return ForgeID::filter (i, n); }, orange)
        {
            knobs = makeKnobs (*this, p, orange, { { id (ForgeID::cutoffName), "Cutoff" }, { id (ForgeID::res), "Res" }, { id (ForgeID::driveName), "Drive" },
                                                   { id (ForgeID::var), "Var" }, { id (ForgeID::mix), "Mix" }, { id (ForgeID::keytrack), "Key" } });
            for (auto* c : std::initializer_list<juce::Component*> { &on.button, &type.box, &view })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto row = area.removeFromTop (22);
            on.button.setBounds (row.removeFromLeft (40));
            row.removeFromLeft (4);
            type.box.setBounds (row);
            area.removeFromTop (5);
            view.setBounds (area.removeFromTop (area.getHeight() - 2 * 60 - 4));
            area.removeFromTop (4);
            layoutRow (area.removeFromTop (60), raw (knobs, 0, 3), 0);
            layoutRow (area.removeFromTop (60), raw (knobs, 3, 6), 0);
        }

    private:
        juce::String id (const char* name) const { return ForgeID::filter (index, name); }

        int index;
        ParamToggle on;
        ParamMenu type;
        FilterView view;
        KnobList knobs;
    };

    class FilterPanel : public Card
    {
    public:
        explicit FilterPanel (ForgeProcessor& p)
            : Card ("Filter", orange), parallel (p.parameters, ForgeID::filtersParallel, "Parallel")
        {
            for (int i = 0; i < 2; ++i)
            {
                controls[i] = std::make_unique<FilterControls> (p, i);
                addChildComponent (*controls[i]);
                tabs[i].setButtonText (juce::String (i + 1));
                tabs[i].setClickingTogglesState (false);
                tabs[i].onClick = [this, i] { select (i); };
                addAndMakeVisible (tabs[i]);
            }
            parallel.button.setTooltip ("Run filters 1 and 2 side by side instead of one after the other");
            addAndMakeVisible (parallel.button);
            select (0);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto top = getLocalBounds().removeFromTop (24).reduced (4, 3);
                parallel.button.setBounds (top.removeFromRight (74));
                top.removeFromRight (4);
                tabs[1].setBounds (top.removeFromRight (26));
                top.removeFromRight (2);
                tabs[0].setBounds (top.removeFromRight (26));
                for (auto& c : controls)
                    c->setBounds (area);
            };
        }

    private:
        void select (int i)
        {
            for (int t = 0; t < 2; ++t)
            {
                controls[t]->setVisible (t == i);
                tabs[t].setToggleState (t == i, juce::dontSendNotification);
            }
        }

        std::unique_ptr<FilterControls> controls[2];
        juce::TextButton tabs[2];
        ParamToggle parallel;
    };

    class OscPage : public juce::Component
    {
    public:
        explicit OscPage (ForgeProcessor& p) : sub (p), filter (p)
        {
            for (int o = 0; o < Forge::numOscs; ++o)
            {
                oscs[o] = std::make_unique<OscPanel> (p, o);
                addAndMakeVisible (*oscs[o]);
            }
            addAndMakeVisible (sub);
            addAndMakeVisible (filter);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto right = area.removeFromRight (250);
            area.removeFromRight (gap);
            sub.setBounds (right.removeFromTop (138));
            right.removeFromTop (gap);
            filter.setBounds (right);

            const auto width = (area.getWidth() - gap * 2) / 3;
            for (auto& o : oscs)
            {
                o->setBounds (area.removeFromLeft (width));
                area.removeFromLeft (gap);
            }
        }

    private:
        std::unique_ptr<OscPanel> oscs[Forge::numOscs];
        SubNoisePanel sub;
        FilterPanel filter;
    };

    //==========================================================================
    // One effect in the rack: its power switch (or a lit dot when it has none), menus,
    // toggles and knobs.
    struct ModuleSpec
    {
        juce::String name;
        juce::String powerId;                                   // empty: always in, lit while it does something
        std::function<bool (APVTS&)> isActive;
        std::vector<std::pair<juce::String, juce::StringArray>> menus;
        std::vector<std::pair<juce::String, juce::String>> toggles;
        std::vector<KnobSpec> knobs;
        juce::String blurb;
    };

    std::vector<ModuleSpec> rackModules()
    {
        using namespace ForgeID;
        auto above = [] (std::initializer_list<const char*> ids) {
            std::vector<juce::String> list (ids.begin(), ids.end());
            return [list] (APVTS& s) { for (auto& id : list) if (s.getRawParameterValue (id)->load() <= 0.001f) return false; return true; };
        };

        return {
            { "Hyper / Dimension", hyperOn, {}, {}, {},
              { { hyperMix, "Mix" }, { hyperDetune, "Detune" }, { hyperRate, "Rate" }, { hyperUnison, "Voices" }, { dimSize, "Dim Size" }, { dimMix, "Dim Mix" } },
              "Thickens and widens before the riddim chain." },
            { "Distortion", {}, above ({ ParamID::distDrive, ParamID::distMix }), { { ParamID::distType, Choices::distTypes } }, {},
              { { ParamID::distDrive, "Drive" }, { ParamID::distMix, "Mix" }, { ParamID::heat, "Heat" } },
              "Nine shapers from soft clip to tube. Heat adds pre-emphasis bite." },
            { "Formant", {}, above ({ ParamID::vowelMix }), {}, {}, { { ParamID::vowelMix, "Mix" }, { ParamID::vowel, "Vowel" } },
              "The yoi / talking filter. Modulate Vowel from an LFO." },
            { "Comb", {}, above ({ ParamID::combMix }), {}, { { ParamID::combTrack, "Key Track" } },
              { { ParamID::combMix, "Mix" }, { ParamID::combFreq, "Freq" }, { ParamID::combFeedback, "Feedback" } },
              "Metallic resonance; key track follows the played note." },
            { "Smash / Dynamics", {}, above ({ ParamID::squash }), {}, { { ParamID::gate, "Gate" }, { ParamID::resample, "Resample" } },
              { { ParamID::squash, "Smash" }, { ParamID::lowKeep, "Low Keep" }, { ParamID::tilt, "Tilt" }, { ParamID::clip, "Clip" }, { ParamID::fxMix, "Chain Mix" } },
              "Multiband smash (OTT style), tilt EQ and a final clipper." },
            { "Filter", fxfOn, {}, { { fxfType, Forge::filterTypeNames() } }, {},
              { { fxfCutoff, "Cutoff" }, { fxfRes, "Res" }, { fxfDrive, "Drive" }, { fxfVar, "Var" }, { fxfMix, "Mix" } },
              "Any of the 18 synth filter types, after the chain." },
            { "EQ", eqOn, {}, { { eq1Shape, forgeEqShapeNames() }, { eq2Shape, forgeEqShapeNames() } }, {},
              { { eq1Freq, "Freq 1" }, { eq1Gain, "Gain 1" }, { eq1Q, "Q 1" }, { eq2Freq, "Freq 2" }, { eq2Gain, "Gain 2" }, { eq2Q, "Q 2" } },
              "Two bands: shelves or peaks." },
            { "Frequency Shift", bodeOn, {}, {}, {}, { { bodeShift, "Shift" }, { bodeFeedback, "Feedback" }, { bodeMix, "Mix" } },
              "Bode-style shifter for laser and screech movement." },
            { "Chorus", chorusOn, {}, {}, {},
              { { chorusRate, "Rate" }, { chorusDepth, "Depth" }, { chorusDelay, "Delay" }, { chorusFeedback, "Feedback" }, { chorusMix, "Mix" } }, {} },
            { "Phaser", phaserOn, {}, {}, {},
              { { phaserRate, "Rate" }, { phaserDepth, "Depth" }, { phaserFreq, "Freq" }, { phaserFeedback, "Feedback" }, { phaserMix, "Mix" } }, {} },
            { "Flanger", flangerOn, {}, {}, {},
              { { flangerRate, "Rate" }, { flangerDepth, "Depth" }, { flangerFeedback, "Feedback" }, { flangerMix, "Mix" } }, {} },
            { "Delay", delayOn, {}, { { delayTime, forgeDelayTimeNames() } }, { { delayPingPong, "Ping Pong" } },
              { { delayFeedback, "Feedback" }, { delayMix, "Mix" }, { delayTone, "Tone" } }, "Tempo-synced." },
            { "Reverb", reverbOn, {}, {}, {},
              { { reverbSize, "Size" }, { reverbDamping, "Damping" }, { reverbWidth, "Width" }, { reverbMix, "Mix" } }, {} },
        };
    }

    class ModuleView : public Card
    {
    public:
        ModuleView (ForgeProcessor& p, const ModuleSpec& spec) : Card (spec.name, violet)
        {
            if (spec.powerId.isNotEmpty())
            {
                power = std::make_unique<ParamToggle> (p.parameters, spec.powerId, "On");
                addAndMakeVisible (power->button);
            }
            for (const auto& [id, items] : spec.menus)
            {
                menus.push_back (std::make_unique<ParamMenu> (p.parameters, id, items));
                addAndMakeVisible (menus.back()->box);
            }
            for (const auto& [id, name] : spec.toggles)
            {
                toggles.push_back (std::make_unique<ParamToggle> (p.parameters, id, name));
                addAndMakeVisible (toggles.back()->button);
            }
            knobs = makeKnobs (*this, p, violet, spec.knobs);

            blurb.setText (spec.blurb, juce::dontSendNotification);
            blurb.setFont (juce::FontOptions (13.0f));
            blurb.setColour (juce::Label::textColourId, textDim);
            blurb.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (blurb);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto row = area.removeFromTop (24);
                if (power != nullptr)
                {
                    power->button.setBounds (row.removeFromLeft (52));
                    row.removeFromLeft (8);
                }
                for (auto& m : menus)
                {
                    m->box.setBounds (row.removeFromLeft (170));
                    row.removeFromLeft (8);
                }
                for (auto& t : toggles)
                {
                    t->button.setBounds (row.removeFromLeft (100));
                    row.removeFromLeft (8);
                }

                area.removeFromTop (12);
                auto knobRow = area.removeFromTop (96);
                const auto knobWidth = juce::jmin (100, knobRow.getWidth() / juce::jmax (1, (int) knobs.size()));
                for (auto& k : knobs)
                    k->setBounds (knobRow.removeFromLeft (knobWidth));

                area.removeFromTop (12);
                blurb.setBounds (area.removeFromTop (40));
            };
        }

    private:
        std::unique_ptr<ParamToggle> power;
        std::vector<std::unique_ptr<ParamMenu>> menus;
        std::vector<std::unique_ptr<ParamToggle>> toggles;
        KnobList knobs;
        juce::Label blurb;
    };

    // The rack as Serum lays it out: the chain as a list on the left (power switches and
    // activity lights), the selected effect's controls on the right.
    class FxPage : public juce::Component, private juce::Timer
    {
    public:
        explicit FxPage (ForgeProcessor& p) : processor (p), specs (rackModules())
        {
            for (size_t i = 0; i < specs.size(); ++i)
            {
                auto& b = buttons.emplace_back (std::make_unique<juce::TextButton> (specs[i].name.toUpperCase()));
                b->onClick = [this, i] { select ((int) i); };
                addAndMakeVisible (*b);

                views.push_back (std::make_unique<ModuleView> (p, specs[i]));
                addChildComponent (*views.back());
            }
            select (1);
            startTimerHz (6);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto list = area.removeFromLeft (220);
            area.removeFromLeft (gap);
            const auto rowHeight = juce::jmin (32, list.getHeight() / (int) buttons.size());
            for (auto& b : buttons)
                b->setBounds (list.removeFromTop (rowHeight).reduced (0, 2).withTrimmedLeft (18));
            for (auto& v : views)
                v->setBounds (area);
        }

        void paint (juce::Graphics& g) override
        {
            for (size_t i = 0; i < buttons.size(); ++i)
            {
                const auto& spec = specs[i];
                auto& state = processor.parameters;
                const auto lit = spec.powerId.isNotEmpty() ? state.getRawParameterValue (spec.powerId)->load() > 0.5f
                                                           : spec.isActive (state);
                const auto b = buttons[i]->getBounds();
                g.setColour (lit ? lime : glassEdge);
                g.fillEllipse ((float) b.getX() - 13.0f, (float) b.getCentreY() - 4.0f, 8.0f, 8.0f);
                if (i + 1 < buttons.size())
                {
                    g.setColour (glassEdge);
                    g.drawVerticalLine (b.getX() - 9, (float) b.getCentreY() + 5.0f, (float) b.getBottom() + 6.0f);
                }
            }
        }

    private:
        void timerCallback() override { repaint(); }

        void select (int index)
        {
            for (size_t i = 0; i < views.size(); ++i)
            {
                views[i]->setVisible ((int) i == index);
                buttons[i]->setToggleState ((int) i == index, juce::dontSendNotification);
            }
        }

        ForgeProcessor& processor;
        std::vector<ModuleSpec> specs;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        std::vector<std::unique_ptr<ModuleView>> views;
    };

    //==========================================================================
    class MatrixPage : public Card
    {
    public:
        explicit MatrixPage (ForgeProcessor& p) : Card ("Mod Matrix", cyan), matrix (p.parameters)
        {
            addAndMakeVisible (matrix);
            hint.setText ("Tip: drag the + handle on any LFO, envelope or macro onto a knob to add a route here.", juce::dontSendNotification);
            hint.setFont (juce::FontOptions (12.0f));
            hint.setColour (juce::Label::textColourId, textDim);
            addAndMakeVisible (hint);
            layout = [this] (juce::Rectangle<int> area)
            {
                hint.setBounds (area.removeFromBottom (18));
                matrix.setBounds (area);
            };
        }

    private:
        ModMatrix matrix;
        juce::Label hint;
    };

    //==========================================================================
    class GlobalPage : public juce::Component
    {
    public:
        explicit GlobalPage (ForgeProcessor& p)
            : voice ("Voicing", cyan), pitch ("Pitch Envelope", amber), hits ("Hit Sequencer (LFO 1 rate per note)", lime),
              phase ("Oscillator Phase", violet), output ("Output", magenta),
              mono (p.parameters, ForgeID::mono, "Mono"), legato (p.parameters, ForgeID::legato, "Legato"),
              hitOn (p.parameters, ForgeID::hitSeqOn, "On"), sequencer (p)
        {
            voiceKnobs = makeKnobs (voice, p, cyan, { { ForgeID::polyphony, "Voices" }, { ForgeID::glide, "Glide" }, { ForgeID::bendRange, "Bend" }, { ForgeID::masterTune, "Tune" } });
            pitchKnobs = makeKnobs (pitch, p, amber, { { ForgeID::pitchEnv, "Amount" }, { ForgeID::pitchDecay, "Decay" } });
            hitKnobs = makeKnobs (hits, p, lime, { { ForgeID::hitSeqLen, "Length" } });
            phaseKnobs = makeKnobs (phase, p, violet, { { ForgeID::osc ('A', ForgeID::randPhase), "Rand A" }, { ForgeID::osc ('B', ForgeID::randPhase), "Rand B" },
                                                        { ForgeID::osc ('C', ForgeID::randPhase), "Rand C" } });
            outputKnobs = makeKnobs (output, p, magenta, { { ParamID::master, "Volume" }, { ParamID::fxMix, "FX Mix" } });

            voice.addAndMakeVisible (mono.button);
            voice.addAndMakeVisible (legato.button);
            hits.addAndMakeVisible (hitOn.button);
            hits.addAndMakeVisible (sequencer);

            voice.layout = [this] (juce::Rectangle<int> area)
            {
                auto row = area.removeFromTop (24);
                layoutRow (row.removeFromLeft (200), { &mono.button, &legato.button }, 6);
                area.removeFromTop (6);
                layoutRow (area.removeFromTop (80), raw (voiceKnobs), 0);
            };
            pitch.layout = [this] (juce::Rectangle<int> area) { layoutRow (area.removeFromTop (80), raw (pitchKnobs), 0); };
            phase.layout = [this] (juce::Rectangle<int> area) { layoutRow (area.removeFromTop (80), raw (phaseKnobs), 0); };
            output.layout = [this] (juce::Rectangle<int> area) { layoutRow (area.removeFromTop (80), raw (outputKnobs), 0); };
            hits.layout = [this] (juce::Rectangle<int> area)
            {
                auto left = area.removeFromLeft (90);
                hitOn.button.setBounds (left.removeFromTop (24));
                left.removeFromTop (6);
                hitKnobs[0]->setBounds (left.removeFromTop (80));
                area.removeFromLeft (8);
                sequencer.setBounds (area);
            };

            for (auto* c : std::initializer_list<juce::Component*> { &voice, &pitch, &hits, &phase, &output })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto top = area.removeFromTop (150);
            voice.setBounds (top.removeFromLeft (top.getWidth() * 40 / 100));
            top.removeFromLeft (gap);
            pitch.setBounds (top.removeFromLeft (top.getWidth() / 2));
            top.removeFromLeft (gap);
            output.setBounds (top);
            area.removeFromTop (gap);
            auto bottom = area;
            phase.setBounds (bottom.removeFromRight (260));
            bottom.removeFromRight (gap);
            hits.setBounds (bottom);
        }

    private:
        Card voice, pitch, hits, phase, output;
        ParamToggle mono, legato, hitOn;
        HitSequencer sequencer;
        KnobList voiceKnobs, pitchKnobs, hitKnobs, phaseKnobs, outputKnobs;
    };

    //==========================================================================
    // Tabs along the top of a mod panel, each with its drag handle.
    struct SourceTab
    {
        juce::TextButton button;
        std::unique_ptr<SourceHandle> handle;
    };

    class EnvelopePanel : public Card
    {
    public:
        explicit EnvelopePanel (ForgeProcessor& p) : Card ("Envelopes", amber), view (p.parameters, amber)
        {
            const juce::StringArray names { "ENV 1", "ENV 2", "ENV 3" };
            const Forge::ModSource sources[] { Forge::ModSource::ampEnv, Forge::ModSource::env2, Forge::ModSource::env3 };

            for (int e = 0; e < 3; ++e)
            {
                tabs[e].button.setButtonText (names[e]);
                tabs[e].button.onClick = [this, e] { select (e); };
                tabs[e].handle = std::make_unique<SourceHandle> (sources[e]);
                addAndMakeVisible (tabs[e].button);
                addAndMakeVisible (*tabs[e].handle);

                const auto ids = idsFor (e);
                knobs[e] = makeKnobs (*this, p, amber, { { ids[0], "Attack" }, { ids[1], "Decay" }, { ids[2], "Sustain" }, { ids[3], "Release" } });
            }
            tabs[0].button.setTooltip ("ENV 1 is the amp envelope");
            addAndMakeVisible (view);
            select (0);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto row = area.removeFromTop (22);
                for (auto& t : tabs)
                {
                    auto cell = row.removeFromLeft (row.getWidth() / (int) (&tabs[3] - &t));
                    t.handle->setBounds (cell.removeFromRight (20));
                    t.button.setBounds (cell.withTrimmedRight (2));
                    row.removeFromLeft (4);
                }
                area.removeFromTop (5);
                auto knobRow = area.removeFromBottom (62);
                view.setBounds (area);
                for (auto& set : knobs)
                    layoutRow (knobRow, raw (set), 0);
            };
        }

    private:
        static juce::StringArray idsFor (int e)
        {
            if (e == 0)
                return { ForgeID::attack, ForgeID::decay, ForgeID::sustain, ForgeID::release };
            return { ForgeID::env (e - 1, ForgeID::envAttack), ForgeID::env (e - 1, ForgeID::envDecay),
                     ForgeID::env (e - 1, ForgeID::envSustain), ForgeID::env (e - 1, ForgeID::envRelease) };
        }

        void select (int e)
        {
            const auto ids = idsFor (e);
            view.setIds (ids[0], ids[1], ids[2], ids[3]);
            for (int i = 0; i < 3; ++i)
            {
                tabs[i].button.setToggleState (i == e, juce::dontSendNotification);
                for (auto& k : knobs[i])
                    k->setVisible (i == e);
            }
        }

        SourceTab tabs[3];
        EnvelopeView view;
        KnobList knobs[3];
    };

    class LfoControls : public juce::Component
    {
    public:
        LfoControls (ForgeProcessor& p, int l)
            : shape (p.parameters, ForgeID::lfo (l, ForgeID::shape), Forge::lfoShapeNames()),
              rate (p.parameters, ForgeID::lfo (l, ForgeID::rate), Forge::lfoRateNames()),
              mode (p.parameters, ForgeID::lfo (l, ForgeID::mode), Forge::lfoModeNames()),
              sync (p.parameters, ForgeID::lfo (l, ForgeID::sync), "Sync")
        {
            knobs = makeKnobs (*this, p, cyan, { { ForgeID::lfo (l, ForgeID::hz), "Hz" }, { ForgeID::lfo (l, ForgeID::smooth), "Smooth" } });
            shape.box.setTooltip ("Built-in shape, or Custom for the one you draw");
            mode.box.setTooltip ("Trigger: restarts every note. Envelope: plays once. Free: runs with the song.");
            for (auto* c : std::initializer_list<juce::Component*> { &shape.box, &rate.box, &mode.box, &sync.button, &shapeCaption, &rateCaption, &modeCaption })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            for (auto [caption, box] : { std::pair<juce::Label*, juce::ComboBox*> { &shapeCaption, &shape.box }, { &modeCaption, &mode.box } })
            {
                caption->setBounds (area.removeFromTop (11));
                box->setBounds (area.removeFromTop (21));
                area.removeFromTop (3);
            }
            rateCaption.setBounds (area.removeFromTop (11));
            auto row = area.removeFromTop (21);
            sync.button.setBounds (row.removeFromLeft (52));
            row.removeFromLeft (4);
            rate.box.setBounds (row);
            area.removeFromTop (3);
            layoutRow (area, raw (knobs), 0);
        }

    private:
        ParamMenu shape, rate, mode;
        ParamToggle sync;
        Caption shapeCaption { "Shape" }, rateCaption { "Rate" }, modeCaption { "Mode" };
        KnobList knobs;
    };

    class LfoPanel : public Card
    {
    public:
        explicit LfoPanel (ForgeProcessor& p) : Card ("LFOs", cyan), editor (p, cyan)
        {
            for (int l = 0; l < Forge::numLfos; ++l)
            {
                tabs[l].button.setButtonText ("LFO " + juce::String (l + 1));
                tabs[l].button.onClick = [this, l] { select (l); };
                tabs[l].handle = std::make_unique<SourceHandle> ((Forge::ModSource) ((int) Forge::ModSource::lfo1 + l));
                addAndMakeVisible (tabs[l].button);
                addAndMakeVisible (*tabs[l].handle);
                controls[l] = std::make_unique<LfoControls> (p, l);
                addChildComponent (*controls[l]);
            }

            for (int g = 0; g < 5; ++g)
                grid.addItem ("Grid " + juce::String (1 << (g + 1)), g + 1);
            grid.setSelectedId (3, juce::dontSendNotification);
            grid.setTooltip ("Grid for drawing (hold shift to snap)");
            grid.onChange = [this] { editor.setGrid (1 << grid.getSelectedId()); };
            addAndMakeVisible (grid);
            addAndMakeVisible (editor);
            select (0);

            layout = [this] (juce::Rectangle<int> area)
            {
                auto row = area.removeFromTop (22);
                grid.setBounds (row.removeFromRight (84));
                row.removeFromRight (8);
                for (int l = 0; l < Forge::numLfos; ++l)
                {
                    auto cell = row.removeFromLeft (92);
                    tabs[l].handle->setBounds (cell.removeFromRight (20));
                    tabs[l].button.setBounds (cell.withTrimmedRight (2));
                    row.removeFromLeft (4);
                }
                area.removeFromTop (5);
                auto side = area.removeFromRight (176);
                area.removeFromRight (8);
                editor.setBounds (area);
                for (auto& c : controls)
                    c->setBounds (side);
            };
        }

    private:
        void select (int l)
        {
            editor.setLfo (l);
            for (int i = 0; i < Forge::numLfos; ++i)
            {
                tabs[i].button.setToggleState (i == l, juce::dontSendNotification);
                controls[i]->setVisible (i == l);
            }
        }

        SourceTab tabs[Forge::numLfos];
        std::unique_ptr<LfoControls> controls[Forge::numLfos];
        juce::ComboBox grid;
        LfoEditor editor;
    };

    class MacroPanel : public Card
    {
    public:
        explicit MacroPanel (ForgeProcessor& p) : Card ("Macros", lime)
        {
            for (int m = 0; m < Forge::numMacros; ++m)
            {
                macros[m] = std::make_unique<MacroKnob> (p, m, lime);
                addAndMakeVisible (*macros[m]);
            }
            layout = [this] (juce::Rectangle<int> area)
            {
                const auto rowHeight = area.getHeight() / 2;
                for (int r = 0; r < 2; ++r)
                {
                    auto row = area.removeFromTop (rowHeight);
                    layoutRow (row, { macros[r * 4].get(), macros[r * 4 + 1].get(), macros[r * 4 + 2].get(), macros[r * 4 + 3].get() }, 4);
                }
            };
        }

        void refreshNames()
        {
            for (auto& m : macros)
                m->refreshName();
        }

    private:
        std::unique_ptr<MacroKnob> macros[Forge::numMacros];
    };

    //==========================================================================
    class VisualCard : public Card
    {
    public:
        explicit VisualCard (ForgeProcessor& p) : Card ("Visual", magenta), view (p)
        {
            caption.setFont (mono (10.0f));
            caption.setColour (juce::Label::textColourId, textDim);
            caption.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (view);
            addAndMakeVisible (caption);
            view.onShapeChanged = [this] (const VisualDNA& dna) { caption.setText (dna.describe().toUpperCase(), juce::dontSendNotification); };
            caption.setText (view.getTargetDNA().describe().toUpperCase(), juce::dontSendNotification);
            view.setTooltip ("This preset's own 3D form, built from its settings. Drag to orbit, scroll to zoom.");

            layout = [this] (juce::Rectangle<int> area)
            {
                caption.setBounds (area.removeFromBottom (16));
                view.setBounds (area.withTrimmedBottom (2));
            };
        }

        ForgeVisual view;

    private:
        juce::Label caption;
    };

    //==========================================================================
    // Logo, preset browser (by category), Magic Sound and the voice / CPU readout.
    class Header : public juce::Component, private juce::Timer
    {
    public:
        explicit Header (ForgeProcessor& p) : processor (p)
        {
            previous.onClick = [this] { processor.presets.loadPrevious(); refresh(); };
            next.onClick = [this] { processor.presets.loadNext(); refresh(); };
            name.onClick = [this] { showPresetMenu(); };
            save.onClick = [this] { showSaveDialog(); };
            save.setTooltip ("Save the current sound to " + processor.presets.getUserPresetFolder().getFullPathName());

            magic.setButtonText ("MAGIC SOUND");
            magic.setTooltip ("Make a brand-new riddim patch. Every click is different.");
            magic.onClick = [this]
            {
                const auto style = magicStyle.getSelectedItemIndex() <= 0 ? juce::String() : magicStyle.getText();
                processor.makeMagicSound (style);
                refresh();
            };

            magicStyle.addItemList (ForgeMagic::styles(), 1);
            magicStyle.setSelectedItemIndex (0, juce::dontSendNotification);
            magicStyle.setTooltip ("What kind of sound Magic Sound makes");

            for (auto* c : std::initializer_list<juce::Component*> { &previous, &name, &next, &save, &magic, &magicStyle })
                addAndMakeVisible (c);

            refresh();
            startTimerHz (4);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (0, 8);
            area.removeFromLeft (250);
            auto right = area.removeFromRight (300).withSizeKeepingCentre (300, 28);
            auto browser = area.withSizeKeepingCentre (juce::jmin (520, area.getWidth()), area.getHeight()).removeFromTop (24);
            previous.setBounds (browser.removeFromLeft (30));
            save.setBounds (browser.removeFromRight (58));
            browser.removeFromRight (4);
            next.setBounds (browser.removeFromRight (30));
            name.setBounds (browser.reduced (4, 0));

            right.removeFromRight (90);
            magicStyle.setBounds (right.removeFromRight (100));
            right.removeFromRight (4);
            magic.setBounds (right);
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds();
            g.setGradientFill (juce::ColourGradient (cyan, 18.0f, 0.0f, violet, 220.0f, 0.0f, false));
            g.setFont (juce::FontOptions (25.0f, juce::Font::bold).withKerningFactor (0.08f));
            g.drawText ("RIDDIM FORGE", area.withX (16).withWidth (240), juce::Justification::centredLeft);

            g.setColour (textDim);
            g.setFont (mono (10.0f));
            g.drawText (category.toUpperCase(), name.getBounds().withY (name.getBottom() + 1).withHeight (12), juce::Justification::centred);

            g.drawText ("VOICES " + juce::String (processor.getNumActiveVoices()), area.removeFromRight (86), juce::Justification::centred);
        }

    private:
        void timerCallback() override
        {
            if (processor.presets.getCurrentName() != shownName)
                refresh();
            repaint (getLocalBounds().removeFromRight (90));
        }

        void refresh()
        {
            shownName = processor.presets.getCurrentName();
            name.setButtonText (shownName);
            category = processor.presets.getCategory (processor.presets.getCurrentIndex());
            if (processor.presets.getCurrentIndex() < 0)
                category = "Magic Sound (unsaved)";
            repaint();
        }

        void showPresetMenu()
        {
            auto& presets = processor.presets;
            presets.refreshUserPresets();
            const auto names = presets.getPresetNames();

            std::map<juce::String, juce::PopupMenu> byCategory;
            juce::StringArray order;
            for (int i = 0; i < names.size(); ++i)
            {
                auto cat = presets.getCategory (i);
                if (cat.isEmpty())
                    cat = "Basics";
                if (! order.contains (cat))
                    order.add (cat);
                byCategory[cat].addItem (i + 1, names[i], true, i == presets.getCurrentIndex());
            }

            juce::PopupMenu menu;
            for (const auto& cat : order)
                menu.addSubMenu (cat + "  (" + juce::String (byCategory[cat].getNumItems()) + ")", byCategory[cat]);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name).withMaximumNumColumns (1),
                                [safe = juce::Component::SafePointer<Header> (this)] (int result)
            {
                if (safe != nullptr && result > 0)
                {
                    safe->processor.presets.loadPreset (result - 1);
                    safe->refresh();
                }
            });
        }

        void showSaveDialog()
        {
            auto* window = new juce::AlertWindow ("Save preset", "Name your sound:", juce::MessageBoxIconType::NoIcon, this);
            window->addTextEditor ("name", processor.presets.getCurrentName());
            window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
            {
                if (result == 1)
                {
                    processor.presets.saveUserPreset (window->getTextEditorContents ("name"));
                    refresh();
                }
            }), true);
        }

        ForgeProcessor& processor;
        juce::TextButton previous { "<" }, next { ">" }, name, save { "SAVE" }, magic;
        juce::ComboBox magicStyle;
        juce::String shownName, category;
    };
}

//==============================================================================
ForgeEditor::ForgeEditor (ForgeProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    header = std::make_unique<Header> (p);
    pages[0] = std::make_unique<OscPage> (p);
    pages[1] = std::make_unique<FxPage> (p);
    pages[2] = std::make_unique<MatrixPage> (p);
    pages[3] = std::make_unique<GlobalPage> (p);
    auto visualPanel = std::make_unique<VisualCard> (p);
    visual = &visualPanel->view;
    visualCard = std::move (visualPanel);
    envelopes = std::make_unique<EnvelopePanel> (p);
    lfos = std::make_unique<LfoPanel> (p);
    macros = std::make_unique<MacroPanel> (p);

    const juce::StringArray tabNames { "OSC", "FX", "MATRIX", "GLOBAL" };
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].onClick = [this, i] { showPage (i); };
        content.addAndMakeVisible (tabs[i]);
        content.addChildComponent (*pages[i]);
    }

    for (auto* c : std::initializer_list<juce::Component*> { header.get(), visualCard.get(), envelopes.get(), lfos.get(), macros.get(), &keyboard })
        content.addAndMakeVisible (c);

    keyboard.setAvailableRange (24, 96);
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyPressBaseOctave (3);
    keyboard.setOctaveForMiddleC (4);

    p.onTextStateChanged = [this]
    {
        if (auto* m = dynamic_cast<MacroPanel*> (macros.get()))
            m->refreshNames();
    };

    showPage (0);

    // Fit the window to the screen it opens on.
    auto initialZoom = 1.0f;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        initialZoom = juce::jlimit (0.6f, 1.0f, juce::jmin ((float) display->userArea.getWidth() * 0.95f / baseWidth,
                                                           (float) display->userArea.getHeight() * 0.9f / baseHeight));

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setResizeLimits (juce::roundToInt (baseWidth * 0.5f), juce::roundToInt (baseHeight * 0.5f), baseWidth * 2, baseHeight * 2);
    setZoom (initialZoom);
    startTimerHz (2);
}

ForgeEditor::~ForgeEditor()
{
    processor.onTextStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void ForgeEditor::setZoom (float z)
{
    zoom = z;
    setSize (juce::roundToInt (baseWidth * zoom), juce::roundToInt (baseHeight * zoom));
}

void ForgeEditor::showPage (int index)
{
    currentPage = index;
    for (int i = 0; i < 4; ++i)
    {
        pages[i]->setVisible (i == index);
        tabs[i].setToggleState (i == index, juce::dontSendNotification);
    }
}

void ForgeEditor::timerCallback() {}

void ForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (void_);
    g.addTransform (content.getTransform());

    // Deep gradient with a faint hex grid, behind the panels.
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) baseWidth, (float) baseHeight);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a1430), bounds.getCentreX(), 0.0f,
                                             void_, bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (glassEdge.withAlpha (0.18f));
    for (int x = 0; x < baseWidth; x += 24)
        g.drawVerticalLine (x, 0.0f, (float) baseHeight);
    for (int y = 0; y < baseHeight; y += 24)
        g.drawHorizontalLine (y, 0.0f, (float) baseWidth);

    g.setColour (juce::Colour (0xc0060b18));
    g.fillRect (0, 0, baseWidth, headerHeight);
    g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.7f), 0.0f, 0.0f, violet.withAlpha (0.0f), (float) baseWidth, 0.0f, false));
    g.fillRect (0, headerHeight - 1, baseWidth, 1);
}

void ForgeEditor::resized()
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));

    auto area = juce::Rectangle<int> (0, 0, baseWidth, baseHeight);
    header->setBounds (area.removeFromTop (headerHeight).reduced (margin, 0));
    area.reduce (margin, 0);
    area.removeFromTop (gap);

    keyboard.setBounds (area.removeFromBottom (keyboardHeight));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / numWhiteKeys);
    area.removeFromBottom (gap);

    auto strip = area.removeFromBottom (stripHeight);
    area.removeFromBottom (gap);

    auto right = area.removeFromRight (visualWidth);
    area.removeFromRight (gap);
    visualCard->setBounds (right);

    auto tabRow = area.removeFromTop (tabHeight);
    for (auto& t : tabs)
    {
        t.setBounds (tabRow.removeFromLeft (110));
        tabRow.removeFromLeft (4);
    }
    area.removeFromTop (gap);
    for (auto& p : pages)
        p->setBounds (area);

    macros->setBounds (strip.removeFromRight (visualWidth));
    strip.removeFromRight (gap);
    envelopes->setBounds (strip.removeFromLeft (370));
    strip.removeFromLeft (gap);
    lfos->setBounds (strip);
}
