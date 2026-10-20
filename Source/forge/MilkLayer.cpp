#include "MilkLayer.h"

namespace
{
    // The slice of the libprojectM 4 C API we use, declared here so the plugin builds without
    // projectM's headers and only needs the library at run time.
    using Handle = void*;
    using SwitchFailedCallback = void (*) (const char* file, const char* message, void* user);
    using LogCallback = void (*) (const char* message, int level, void* user);

    struct Api
    {
        Handle (*create)() = nullptr;
        void (*destroy) (Handle) = nullptr;
        void (*loadPresetData) (Handle, const char*, bool) = nullptr;
        void (*pcmAddFloat) (Handle, const float*, unsigned int, int) = nullptr;
        void (*renderFrameFbo) (Handle, uint32_t) = nullptr;
        void (*burnTexture) (Handle, uint32_t, int, int, int, int) = nullptr;
        void (*setWindowSize) (Handle, size_t, size_t) = nullptr;
        void (*setMeshSize) (Handle, size_t, size_t) = nullptr;
        void (*setFps) (Handle, int32_t) = nullptr;
        void (*setPresetLocked) (Handle, bool) = nullptr;
        void (*setHardCutEnabled) (Handle, bool) = nullptr;
        void (*setSoftCutDuration) (Handle, double) = nullptr;
        void (*setPresetDuration) (Handle, double) = nullptr;
        void (*setBeatSensitivity) (Handle, float) = nullptr;
        void (*setSwitchFailedCallback) (Handle, SwitchFailedCallback, void*) = nullptr;
        void (*setLogCallback) (LogCallback, bool, void*) = nullptr;
        char* (*getVersionString)() = nullptr;
        void (*freeString) (const char*) = nullptr;

        juce::DynamicLibrary library;
        juce::String status;
        bool loaded = false;

        Api()
        {
            if (! open())
                return;

            const auto allFound = find (create, "projectm_create")
                               && find (destroy, "projectm_destroy")
                               && find (loadPresetData, "projectm_load_preset_data")
                               && find (pcmAddFloat, "projectm_pcm_add_float")
                               && find (renderFrameFbo, "projectm_opengl_render_frame_fbo")
                               && find (burnTexture, "projectm_opengl_burn_texture")
                               && find (setWindowSize, "projectm_set_window_size")
                               && find (setMeshSize, "projectm_set_mesh_size")
                               && find (setFps, "projectm_set_fps")
                               && find (setPresetLocked, "projectm_set_preset_locked")
                               && find (setHardCutEnabled, "projectm_set_hard_cut_enabled")
                               && find (setSoftCutDuration, "projectm_set_soft_cut_duration")
                               && find (setPresetDuration, "projectm_set_preset_duration")
                               && find (setBeatSensitivity, "projectm_set_beat_sensitivity")
                               && find (setSwitchFailedCallback, "projectm_set_preset_switch_failed_event_callback");

            if (! allFound)
            {
                status = "libprojectM is too old (needs 4.2 or newer)";
                return;
            }

            find (setLogCallback, "projectm_set_log_callback");
            find (getVersionString, "projectm_get_version_string");
            find (freeString, "projectm_free_string");

            status = "projectM";
            if (getVersionString != nullptr)
            {
                if (auto* v = getVersionString())
                {
                    status << " " << v;
                    if (freeString != nullptr)
                        freeString (v);
                }
            }

            if (setLogCallback != nullptr && verbose())
                setLogCallback ([] (const char* message, int, void*) { std::fprintf (stderr, "[projectM] %s\n", message); }, false, nullptr);

            loaded = true;
        }

        static bool verbose() { return juce::SystemStats::getEnvironmentVariable ("RIDDIM_MILK_LOG", {}).isNotEmpty(); }

        bool open()
        {
           #if JUCE_WINDOWS
            const juce::StringArray names { "projectM-4.dll" };
           #elif JUCE_MAC
            const juce::StringArray names { "libprojectM-4.4.dylib", "libprojectM-4.dylib" };
           #else
            const juce::StringArray names { "libprojectM-4.so.4", "libprojectM-4.so" };
           #endif

            // Next to the plugin binary (inside the .vst3 bundle) or the standalone app.
            const auto binaryFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
            const juce::Array<juce::File> folders { binaryFolder, binaryFolder.getSiblingFile ("Frameworks") };

            for (const auto& folder : folders)
                for (const auto& name : names)
                {
                    const auto file = folder.getChildFile (name);
                    if (file.existsAsFile() && library.open (file.getFullPathName()))
                        return true;
                }

            status = "libprojectM not found next to the plugin";
            return false;
        }

        template <typename Fn>
        bool find (Fn& fn, const char* name)
        {
            fn = reinterpret_cast<Fn> (library.getFunction (name));
            return fn != nullptr;
        }
    };

    Api& api()
    {
        static Api instance; // stays loaded for the life of the process
        return instance;
    }
}

MilkLayer::~MilkLayer()
{
    jassert (handle == nullptr); // destroy() must run on the GL thread first
}

bool MilkLayer::create (int width, int height)
{
    auto& a = api();
    if (! a.loaded)
        return false;

    handle = a.create(); // fails (null) if the context is below OpenGL 3.3
    if (handle == nullptr)
        return false;

    // We pick every scene ourselves: no timed or beat-triggered switching.
    a.setPresetLocked (handle, true);
    a.setHardCutEnabled (handle, false);
    a.setPresetDuration (handle, 100000.0);
    a.setSoftCutDuration (handle, 1.6);
    a.setBeatSensitivity (handle, 1.2f);
    a.setFps (handle, 60);
    a.setMeshSize (handle, 64, 48);
    a.setSwitchFailedCallback (handle, [] (const char*, const char* message, void*)
    {
        DBG ("projectM scene failed: " << message);
        if (Api::verbose())
            std::fprintf (stderr, "[projectM] scene failed: %s\n", message);
    }, nullptr);

    lastWidth = juce::jmax (1, width);
    lastHeight = juce::jmax (1, height);
    a.setWindowSize (handle, (size_t) lastWidth, (size_t) lastHeight);
    hasFrame = false;
    return true;
}

void MilkLayer::destroy()
{
    if (handle != nullptr)
        api().destroy (handle);

    handle = nullptr;
}

void MilkLayer::addSamples (const float* mono, int numSamples)
{
    if (handle != nullptr && numSamples > 0)
        api().pcmAddFloat (handle, mono, (unsigned int) numSamples, 1);
}

void MilkLayer::setScene (const juce::String& milkText, bool smooth)
{
    if (handle != nullptr)
        api().loadPresetData (handle, milkText.toRawUTF8(), smooth);
}

void MilkLayer::burnIn (GLuint texture, int width, int height)
{
    if (handle != nullptr && hasFrame && width == lastWidth && height == lastHeight)
        api().burnTexture (handle, texture, 0, 0, width, height);
}

void MilkLayer::render (GLuint framebuffer, int width, int height)
{
    if (handle == nullptr)
        return;

    if (width != lastWidth || height != lastHeight)
    {
        api().setWindowSize (handle, (size_t) width, (size_t) height);
        lastWidth = width;
        lastHeight = height;
    }

    api().renderFrameFbo (handle, (uint32_t) framebuffer);
    hasFrame = true;
}

juce::String MilkLayer::getLibraryStatus()
{
    return api().status;
}

bool MilkLayer::isLibraryLoaded()
{
    return api().loaded;
}
