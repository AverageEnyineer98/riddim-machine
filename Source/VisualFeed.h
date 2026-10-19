#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// Lock-free bridge from the audio thread to the visualiser's GL thread.
// The audio thread writes note velocities, the output level and a mono copy
// of the output; the GL thread reads them once per frame.
class VisualFeed
{
public:
    static constexpr int fifoSize = 8192;

    VisualFeed()
    {
        for (auto& v : noteVelocity)
            v.store (0.0f);
    }

    void noteOn (int note, float velocity)  { if (isPositiveAndBelow (note, 128)) noteVelocity[(size_t) note].store (velocity); }
    void noteOff (int note)                 { if (isPositiveAndBelow (note, 128)) noteVelocity[(size_t) note].store (0.0f); }
    void allNotesOff()                      { for (auto& v : noteVelocity) v.store (0.0f); }
    float getVelocity (int note) const      { return noteVelocity[(size_t) note].load(); }

    void setLevel (float rms)               { level.store (rms); }
    float getLevel() const                  { return level.load(); }

    void setLfoPhase (float p)              { lfoPhase.store (p); }
    float getLfoPhase() const               { return lfoPhase.load(); }

    void setSampleRate (double sr)          { sampleRate.store (sr); }
    double getSampleRate() const            { return sampleRate.load(); }

    // Audio thread. Drops samples when the reader falls behind.
    void pushSamples (const float* data, int numSamples)
    {
        const auto scope = fifo.write (numSamples);

        if (scope.blockSize1 > 0)
            std::copy (data, data + scope.blockSize1, buffer.begin() + scope.startIndex1);

        if (scope.blockSize2 > 0)
            std::copy (data + scope.blockSize1, data + scope.blockSize1 + scope.blockSize2,
                       buffer.begin() + scope.startIndex2);
    }

    // GL thread. Returns the number of samples copied into dest.
    int pullSamples (float* dest, int maxSamples)
    {
        const auto scope = fifo.read (juce::jmin (maxSamples, fifo.getNumReady()));

        if (scope.blockSize1 > 0)
            std::copy (buffer.begin() + scope.startIndex1,
                       buffer.begin() + scope.startIndex1 + scope.blockSize1, dest);

        if (scope.blockSize2 > 0)
            std::copy (buffer.begin() + scope.startIndex2,
                       buffer.begin() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);

        return scope.blockSize1 + scope.blockSize2;
    }

private:
    template <typename T>
    static bool isPositiveAndBelow (T v, T limit) { return v >= 0 && v < limit; }

    std::array<std::atomic<float>, 128> noteVelocity;
    std::atomic<float> level { 0.0f };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<float> lfoPhase { 0.0f };

    juce::AbstractFifo fifo { fifoSize };
    std::array<float, fifoSize> buffer {};
};
