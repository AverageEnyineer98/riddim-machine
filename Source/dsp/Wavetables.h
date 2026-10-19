#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

// The built-in wavetable library. Every table holds `numFrames` single-cycle
// frames that the position knob morphs across, and each frame is stored at
// several band-limited levels so high notes don't alias.
//
// Shared between all plugin instances through juce::SharedResourcePointer.
class WavetableBank
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int numFrames = 16;
    static constexpr int numLevels = 11; // level L keeps harmonics up to 1024 >> L

    WavetableBank();

    static const juce::StringArray& getTableNames();
    int getNumTables() const { return (int) tables.size(); }

    // The highest-quality frame data, for drawing previews.
    const float* getFrame (int table, int frame) const { return getFrame (table, frame, 0); }

    // Linear interpolation across phase and frame position.
    // `position` is 0..1 across the frames, `phase` 0..1 through the cycle.
    float sample (int table, int level, float position, double phase) const noexcept
    {
        const auto framePos = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
        const auto frameA = juce::jmin ((int) framePos, numFrames - 2);
        const auto frameMix = framePos - (float) frameA;

        const auto index = phase * (double) frameSize;
        const auto i0 = ((int) index) & (frameSize - 1);
        const auto i1 = (i0 + 1) & (frameSize - 1);
        const auto frac = (float) (index - std::floor (index));

        const auto* a = getFrame (table, frameA, level);
        const auto* b = getFrame (table, frameA + 1, level);
        const auto sa = a[i0] + frac * (a[i1] - a[i0]);
        const auto sb = b[i0] + frac * (b[i1] - b[i0]);
        return sa + frameMix * (sb - sa);
    }

    // Band-limits single-cycle frames (frameSize samples each, back to back) into `numLevels`
    // levels per frame, normalised to the loudest full-band frame, laid out like the built-in tables.
    static std::vector<float> buildLevels (const std::vector<float>& rawFrames, int frameCount, bool normalisePerFrame);

    // Picks the band-limited level whose top harmonic stays under Nyquist.
    static int levelForFrequency (double frequency, double sampleRate) noexcept
    {
        const auto maxHarmonics = sampleRate * 0.5 / juce::jmax (1.0, frequency);
        int level = 0;
        while (level < numLevels - 1 && (double) (1024 >> level) > maxHarmonics)
            ++level;
        return level;
    }

private:
    const float* getFrame (int table, int frame, int level) const
    {
        return tables[(size_t) table].data() + ((size_t) (frame * numLevels + level)) * frameSize;
    }

    std::vector<std::vector<float>> tables;
};

// A wavetable loaded from a WAV file, in the common format other wavetable synths use:
// single-cycle frames of 2048 samples back to back (a "clm " chunk can name another frame
// size). A file shorter than one frame is read as a single cycle.
class UserWavetable
{
public:
    static constexpr int maxFrames = 256;

    // Returns nullptr and sets `error` when the file can't be used.
    static std::unique_ptr<UserWavetable> load (const juce::File&, juce::String& error);

    juce::String getName() const { return name; }
    juce::String getPath() const { return path; }
    int getNumFrames() const { return frameCount; }
    const float* getFrame (int frame) const { return levels.data() + (size_t) (frame * WavetableBank::numLevels) * WavetableBank::frameSize; }

    float sample (int level, float position, double phase) const noexcept
    {
        constexpr auto size = WavetableBank::frameSize;
        const auto framePos = juce::jlimit (0.0f, 1.0f, position) * (float) (frameCount - 1);
        const auto frameA = juce::jmin ((int) framePos, juce::jmax (0, frameCount - 2));
        const auto frameB = juce::jmin (frameA + 1, frameCount - 1);
        const auto frameMix = framePos - (float) frameA;

        const auto index = phase * (double) size;
        const auto i0 = ((int) index) & (size - 1);
        const auto i1 = (i0 + 1) & (size - 1);
        const auto frac = (float) (index - std::floor (index));

        const auto* a = levels.data() + ((size_t) (frameA * WavetableBank::numLevels + level)) * size;
        const auto* b = levels.data() + ((size_t) (frameB * WavetableBank::numLevels + level)) * size;
        const auto sa = a[i0] + frac * (a[i1] - a[i0]);
        const auto sb = b[i0] + frac * (b[i1] - b[i0]);
        return sa + frameMix * (sb - sa);
    }

private:
    juce::String name, path;
    int frameCount = 0;
    std::vector<float> levels;
};
