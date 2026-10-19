#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_opengl/juce_opengl.h>
#include "VisualFeed.h"

// OpenGL 3D view of the synth: a keyboard whose keys press down and glow with
// the notes being played, in front of a scrolling spectrum terrain driven by the
// audio output. Drag to orbit, scroll to zoom, double-click to reset the camera.
class Visualiser3D : public juce::Component,
                     private juce::OpenGLRenderer
{
public:
    static constexpr int lowestNote = 24;   // C1
    static constexpr int highestNote = 96;  // C7
    static constexpr int terrainColumns = 128;
    static constexpr int terrainRows = 56;

    Visualiser3D (VisualFeed&, juce::MidiKeyboardState&);
    ~Visualiser3D() override;

    // The FX plugin has no notes, so it shows only the spectrum terrain.
    void setShowKeyboard (bool show) { showKeyboard = show; }

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void updateNotes (float dt);
    void updateSpectrum();
    void drawKeys (const float* view, const float* projection, const float* eye);
    void drawTerrain (const float* view, const float* projection);

    VisualFeed& feed;
    juce::MidiKeyboardState& keyboardState;
    juce::OpenGLContext context;

    // Camera, written on the message thread and read on the GL thread.
    std::atomic<float> yaw { 0.0f }, pitch { 0.38f }, distance { 36.0f };
    std::atomic<int> viewWidth { 1 }, viewHeight { 1 };
    std::atomic<bool> showKeyboard { true };
    float dragStartYaw = 0.0f, dragStartPitch = 0.0f;

    // GL-thread state.
    std::unique_ptr<juce::OpenGLShaderProgram> keyShader, terrainShader;
    GLuint cubeVao = 0, cubeVbo = 0, terrainVao = 0, terrainVbo = 0;

    std::array<float, 128> noteGlow {};
    float smoothedLevel = 0.0f;
    double lastFrameMs = 0.0;
    float clock = 0.0f;

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> sampleHistory = std::vector<float> ((size_t) fftSize, 0.0f);
    std::vector<float> incoming = std::vector<float> ((size_t) VisualFeed::fifoSize, 0.0f);
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> spectrumRows = std::vector<float> ((size_t) (terrainRows * terrainColumns), 0.0f);
    std::vector<float> terrainVertices = std::vector<float> ((size_t) (terrainRows * terrainColumns * 3), 0.0f);
    int newestRow = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualiser3D)
};
