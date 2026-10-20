#pragma once

#include <juce_opengl/juce_opengl.h>

// MilkDrop-style feedback visuals drawn by projectM (https://github.com/projectM-visualizer/projectm,
// LGPL-2.1). libprojectM is a separate shared library loaded at run time from next to the plugin
// (or the standalone app), so it stays replaceable as the LGPL asks. Without it the view simply
// draws the 3D form on its own.
//
// Everything except getLibraryStatus() runs on the GL thread with the context current.
class MilkLayer
{
public:
    MilkLayer() = default;
    ~MilkLayer();

    bool create (int width, int height);
    void destroy();
    bool isReady() const noexcept { return handle != nullptr; }

    void addSamples (const float* mono, int numSamples);

    // Switches to a preset given as .milk text, blending from the last one when smooth.
    void setScene (const juce::String& milkText, bool smooth);

    // Copies a texture into the feedback image, so whatever it holds gets swirled into the trails.
    void burnIn (GLuint texture, int width, int height);

    // Draws a frame into the given framebuffer at the given size in pixels.
    void render (GLuint framebuffer, int width, int height);

    // "projectM 4.2.0", or why it isn't available.
    static juce::String getLibraryStatus();
    static bool isLibraryLoaded();

private:
    void* handle = nullptr;
    int lastWidth = 0, lastHeight = 0;
    bool hasFrame = false;

    JUCE_DECLARE_NON_COPYABLE (MilkLayer)
};
