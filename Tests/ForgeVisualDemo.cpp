// Plays a riddim bar through Riddim Forge presets in real time and shows the visual, for
// screenshots and screen-recorded clips. Writes what it played to a WAV so a clip can get sound.
//
//   ForgeVisualDemo [--presets 3,40,120] [--seconds-each 6] [--size 960x540] [--mode 0|1|2]
//                   [--scene -1|0..3] [--editor] [--wav out.wav]

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/forge/ForgeEditor.h"
#include "../Source/forge/MilkScenes.h"

namespace
{
    constexpr double sampleRate = 48000.0, bpm = 150.0;
    constexpr int blockSize = 256, stepsPerBar = 12;

    struct Hit { int step, length, note; };
    const std::vector<Hit> riddimBar { { 0, 2, 30 }, { 3, 1, 30 }, { 4, 1, 30 }, { 6, 2, 30 }, { 9, 1, 30 }, { 10, 2, 33 } };

    juce::String option (const juce::StringArray& args, const juce::String& name, const juce::String& fallback)
    {
        const auto i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    }

    // Feeds the processor in real time from a timer thread, as a host's audio thread would.
    class Player : private juce::HighResolutionTimer
    {
    public:
        explicit Player (ForgeProcessor& p) : processor (p)
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        ~Player() override { stopTimer(); }

        void start()
        {
            startMs = juce::Time::getMillisecondCounterHiRes();
            std::printf ("audio-start-epoch-ms %.0f\n", (double) juce::Time::currentTimeMillis());
            std::fflush (stdout);
            startTimer (4);
        }

        void stop() { stopTimer(); }

        void writeWav (const juce::File& file)
        {
            const juce::ScopedLock lock (recordLock);
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            juce::WavAudioFormat wav;
            const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
            if (auto writer = wav.createWriterFor (stream, options))
                writer->writeFromAudioSampleBuffer (recorded, 0, recordedSamples);
        }

    private:
        void hiResTimerCallback() override
        {
            const auto due = (juce::int64) ((juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001 * sampleRate);
            while (rendered + blockSize <= due)
                renderBlock();
        }

        void renderBlock()
        {
            const auto samplesPerStep = (juce::int64) (sampleRate * 60.0 / bpm / 3.0); // 1/8 triplets
            juce::MidiBuffer midi;

            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = rendered + i;
                if (t % samplesPerStep != 0)
                    continue;

                const auto step = (int) ((t / samplesPerStep) % stepsPerBar);
                for (const auto& hit : riddimBar)
                {
                    if (hit.step == step)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, hit.note, 0.9f), i);
                        offAt = t + samplesPerStep * hit.length - samplesPerStep / 6;
                        noteOn = hit.note;
                    }
                }
            }

            if (noteOn >= 0 && offAt >= rendered && offAt < rendered + blockSize)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, noteOn), (int) (offAt - rendered));
                noteOn = -1;
            }

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            processor.processBlock (buffer, midi);

            const juce::ScopedLock lock (recordLock);
            if (recordedSamples + blockSize > recorded.getNumSamples())
                recorded.setSize (2, recorded.getNumSamples() + (int) sampleRate * 10, true);
            for (int c = 0; c < 2; ++c)
                recorded.copyFrom (c, recordedSamples, buffer, c, 0, blockSize);
            recordedSamples += blockSize;
            rendered += blockSize;
        }

        ForgeProcessor& processor;
        double startMs = 0.0;
        juce::int64 rendered = 0, offAt = -1;
        int noteOn = -1;
        juce::CriticalSection recordLock;
        juce::AudioBuffer<float> recorded { 2, (int) sampleRate * 10 };
        int recordedSamples = 0;
    };
}

class ForgeVisualDemoApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "ForgeVisualDemo"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        for (const auto& index : juce::StringArray::fromTokens (option (args, "--presets", "0"), ",", {}))
            presetList.add (index.getIntValue());
        secondsEach = option (args, "--seconds-each", "6").getDoubleValue();
        wavFile = option (args, "--wav", {});
        const auto size = juce::StringArray::fromTokens (option (args, "--size", "960x540"), "x", {});

        std::printf ("milk: %s\n", MilkLayer::getLibraryStatus().toRawUTF8());

        processor = std::make_unique<ForgeProcessor>();

        if (args.contains ("--stats")) // which scene each factory preset gets
        {
            for (int i = 0; i < processor->presets.getNumFactoryPresets(); ++i)
            {
                processor->presets.loadPreset (i);
                const auto d = VisualDNA::fromState (processor->parameters, processor->presets.getCurrentName());
                std::printf ("%d\t%s\t%s\ttwist %.2f metal %.2f spikes %.3f wire %.2f pulse %.2f n11 %.2f m1 %.0f\n", i,
                             processor->presets.getCategory (i).toRawUTF8(), MilkScenes::getName (MilkScenes::pickFor (d)).toRawUTF8(),
                             d.twist, d.metal, d.spikes, d.wire, d.pulse, d.n11, d.m1);
            }
            quit();
            return;
        }
        player = std::make_unique<Player> (*processor);
        loadPreset (0);

        window = std::make_unique<juce::DocumentWindow> ("Forge Visual Demo", juce::Colours::black, 0);
        window->setUsingNativeTitleBar (false);
        window->setTitleBarHeight (0);

        if (args.contains ("--editor"))
        {
            window->setContentOwned (processor->createEditor(), true);
        }
        else
        {
            auto* visual = new ForgeVisual (*processor);
            visual->setMode (option (args, "--mode", "1").getIntValue());
            visual->setSceneChoice (option (args, "--scene", "-1").getIntValue());
            visual->setSize (size[0].getIntValue(), size[1].getIntValue());
            window->setContentOwned (visual, true);
        }

        window->setTopLeftPosition (0, 0);
        window->setVisible (true);

        player->start();
        startTimer ((int) (secondsEach * 1000.0));
    }

    void shutdown() override
    {
        if (player == nullptr)
            return;

        player->stop();
        if (wavFile.isNotEmpty())
            player->writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (wavFile));
        window.reset();
        player.reset();
        processor.reset();
    }

private:
    void loadPreset (int position)
    {
        const auto index = presetList[position];
        processor->presets.loadPreset (index);
        const auto dna = VisualDNA::fromState (processor->parameters, processor->presets.getCurrentName());
        std::printf ("preset %d \"%s\" scene %s, %s\n", index, processor->presets.getCurrentName().toRawUTF8(),
                     MilkScenes::getName (MilkScenes::pickFor (dna)).toRawUTF8(), dna.describe().toRawUTF8());
        std::fflush (stdout);
    }

    void timerCallback() override
    {
        if (++position >= presetList.size())
        {
            stopTimer();
            quit();
            return;
        }

        loadPreset (position);
    }

    std::unique_ptr<ForgeProcessor> processor;
    std::unique_ptr<Player> player;
    std::unique_ptr<juce::DocumentWindow> window;
    juce::Array<int> presetList;
    int position = 0;
    double secondsEach = 6.0;
    juce::String wavFile;
};

START_JUCE_APPLICATION (ForgeVisualDemoApp)
