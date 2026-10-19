#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_opengl/juce_opengl.h>
#include "ForgeProcessor.h"

// The shape a preset gets in the 3D view. Every field comes from the sound's own settings
// (tables, warps, filter, distortion, LFOs, unison, macros) plus a seed from the preset's
// name, so each preset has its own form, and turning a knob morphs it.
struct VisualDNA
{
    float m1 = 4.0f, n11 = 1.0f, n12 = 1.0f, n13 = 1.0f;   // superformula across the equator
    float m2 = 4.0f, n21 = 1.0f, n22 = 1.0f, n23 = 1.0f;   // and from pole to pole
    float twist = 0.0f, spikes = 0.0f, spikeFreq = 6.0f;
    float hueA = 0.52f, hueB = 0.78f, saturation = 0.8f;
    float wire = 0.3f, pulse = 0.2f, spin = 0.2f, orbiters = 1.0f, metal = 0.5f, seed = 0.0f;

    static constexpr int numFields = 20;
    float* data() { return &m1; }
    const float* data() const { return &m1; }

    static VisualDNA fromState (const juce::AudioProcessorValueTreeState&, const juce::String& presetName);
    juce::String describe() const; // a short caption, e.g. "7-fold spiked shell, twisted"
};

// OpenGL view: the preset's supershape, lit and rimmed in its colours, breathing with LFO 1,
// rippling with the output spectrum and flashing on every note, with orbit rings for unison
// and a spectrum halo underneath. Drag to orbit, scroll to zoom, double-click to reset.
class ForgeVisual : public juce::Component, public juce::SettableTooltipClient, private juce::OpenGLRenderer, private juce::Timer
{
public:
    explicit ForgeVisual (ForgeProcessor&);
    ~ForgeVisual() override;

    const VisualDNA& getTargetDNA() const { return target; }

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void (const VisualDNA&)> onShapeChanged;

private:
    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void updateSpectrum();

    ForgeProcessor& processor;
    juce::OpenGLContext context;

    VisualDNA target;                  // message thread
    std::array<std::atomic<float>, VisualDNA::numFields> shared {};
    std::array<float, VisualDNA::numFields> current {};
    bool firstFrame = true;

    std::atomic<float> yaw { 0.6f }, pitch { 0.35f }, distance { 4.6f };
    std::atomic<int> viewWidth { 1 }, viewHeight { 1 };
    float dragYaw = 0.0f, dragPitch = 0.0f;

    // GL thread.
    std::unique_ptr<juce::OpenGLShaderProgram> shapeShader, lineShader;
    GLuint meshVao = 0, meshVbo = 0, meshIbo = 0, lineVao = 0, lineVbo = 0;
    int meshIndexCount = 0;
    double lastFrameMs = 0.0;
    float clock = 0.0f, spinAngle = 0.0f, flash = 0.0f, pulseSmoothed = 0.0f, level = 0.0f;
    std::array<float, 128> lastVelocity {};

    static constexpr int numBands = 32;
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> history = std::vector<float> ((size_t) fftSize, 0.0f);
    std::vector<float> incoming = std::vector<float> ((size_t) VisualFeed::fifoSize, 0.0f);
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::array<float, numBands> bands {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeVisual)
};
