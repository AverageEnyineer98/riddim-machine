#include "ForgeVisual.h"
#include "../Parameters.h"
#include "MilkScenes.h"

using namespace juce::gl;

static_assert (sizeof (VisualDNA) == sizeof (float) * VisualDNA::numFields, "VisualDNA must be plain floats");

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr int meshU = 192, meshV = 96, ringPoints = 128;
    constexpr float defaultYaw = 0.6f, defaultPitch = 0.35f, defaultDistance = 4.6f;

    // Column-major 4x4 matrix, laid out the way OpenGL expects.
    struct Mat4
    {
        float m[16] {};

        static Mat4 identity()             { Mat4 r; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
        static Mat4 scale (float s)        { Mat4 r; r.m[0] = r.m[5] = r.m[10] = s; r.m[15] = 1.0f; return r; }
        static Mat4 translation (float x, float y, float z) { auto r = identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }

        static Mat4 rotation (int axis, float radians)
        {
            auto r = identity();
            const auto c = std::cos (radians), s = std::sin (radians);
            const int a = (axis + 1) % 3, b = (axis + 2) % 3;
            r.m[a * 4 + a] = c;  r.m[a * 4 + b] = s;
            r.m[b * 4 + a] = -s; r.m[b * 4 + b] = c;
            return r;
        }

        static Mat4 perspective (float fovY, float aspect, float zNear, float zFar)
        {
            Mat4 r;
            const auto f = 1.0f / std::tan (fovY * 0.5f);
            r.m[0] = f / aspect;
            r.m[5] = f;
            r.m[10] = (zFar + zNear) / (zNear - zFar);
            r.m[11] = -1.0f;
            r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
            return r;
        }

        static Mat4 lookAt (juce::Vector3D<float> eye, juce::Vector3D<float> target, juce::Vector3D<float> up)
        {
            const auto f = (target - eye).normalised();
            const auto s = (f ^ up).normalised();
            const auto u = s ^ f;

            auto r = identity();
            r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
            r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
            r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
            r.m[12] = -(s * eye);
            r.m[13] = -(u * eye);
            r.m[14] = f * eye;
            return r;
        }

        Mat4 operator* (const Mat4& o) const
        {
            Mat4 r;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    for (int k = 0; k < 4; ++k)
                        r.m[col * 4 + row] += m[k * 4 + row] * o.m[col * 4 + k];
            return r;
        }
    };

    float superformula (float phi, float m, float n1, float n2, float n3)
    {
        const auto t1 = std::pow (std::abs (std::cos (m * phi * 0.25f)), n2);
        const auto t2 = std::pow (std::abs (std::sin (m * phi * 0.25f)), n3);
        return juce::jlimit (0.0f, 2.5f, std::pow (juce::jmax (t1 + t2, 1.0e-4f), -1.0f / n1));
    }

    const char* shapeVertexShader = R"(
        attribute vec2 uv;
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        uniform vec4 sf1;
        uniform vec4 sf2;
        uniform float twist;
        uniform float spikes;
        uniform float spikeFreq;
        uniform float time;
        uniform float breathe;
        uniform float norm;
        uniform float inflate;
        uniform float bands[32];
        varying vec3 vNormal;
        varying vec3 vWorld;
        varying vec2 vUv;
        varying float vBand;

        float sf (float phi, vec4 p)
        {
            float t1 = pow (abs (cos (p.x * phi * 0.25)), p.z);
            float t2 = pow (abs (sin (p.x * phi * 0.25)), p.w);
            return clamp (pow (max (t1 + t2, 0.0001), -1.0 / p.y), 0.0, 2.5);
        }

        float bandAt (float v)
        {
            float t = clamp (v / 3.14159 + 0.5, 0.0, 1.0) * 31.0;
            int i = int (t);
            int j = min (i + 1, 31);
            return mix (bands[i], bands[j], t - float (i));
        }

        vec3 surface (float u, float v)
        {
            float r1 = sf (u, sf1);
            float r2 = sf (v, sf2);
            vec3 p = vec3 (r1 * cos (u) * r2 * cos (v), r2 * sin (v), r1 * sin (u) * r2 * cos (v)) * norm;
            float d = 1.0 + breathe + bandAt (v) * 0.45
                    + spikes * sin (u * spikeFreq + time * 1.7) * sin (v * spikeFreq * 0.8 - time * 1.1);
            p *= d * inflate;
            float a = twist * p.y + time * 0.05 * twist;
            float c = cos (a), s = sin (a);
            return vec3 (c * p.x - s * p.z, p.y, s * p.x + c * p.z);
        }

        void main()
        {
            float e = 0.004;
            vec3 p = surface (uv.x, uv.y);
            vec3 du = surface (uv.x + e, uv.y) - p;
            vec3 dv = surface (uv.x, uv.y + e) - p;
            vec3 n = cross (dv, du);
            if (length (n) < 1.0e-7)
                n = p;

            vec4 world = model * vec4 (p, 1.0);
            vWorld = world.xyz;
            vNormal = normalize (mat3 (model) * normalize (n));
            vUv = uv;
            vBand = bandAt (uv.y);
            gl_Position = projection * view * world;
        }
    )";

    const char* shapeFragmentShader = R"(
        varying vec3 vNormal;
        varying vec3 vWorld;
        varying vec2 vUv;
        varying float vBand;
        uniform vec3 colourA;
        uniform vec3 colourB;
        uniform vec3 eye;
        uniform float flash;
        uniform float metal;
        uniform float alpha;
        uniform float wirePass;
        uniform float burnPass;

        void main()
        {
            vec3 n = normalize (vNormal);
            vec3 toEye = normalize (eye - vWorld);
            if (dot (n, toEye) < 0.0)
                n = -n;

            // Only the glowing rim goes into the MilkDrop feedback, so trails stay bright lines
            // instead of piling up into a solid blob.
            if (burnPass > 0.5)
            {
                float edge = pow (1.0 - max (dot (n, toEye), 0.0), 3.0);
                gl_FragColor = vec4 (mix (colourA, colourB, edge) * (0.6 + flash + vBand), edge * (0.35 + 0.4 * flash));
                return;
            }

            if (wirePass > 0.5)
            {
                float edge = pow (1.0 - max (dot (n, toEye), 0.0), 1.5);
                gl_FragColor = vec4 (mix (colourB, vec3 (1.0), 0.35) * (0.5 + flash + vBand), alpha * (0.35 + 0.65 * edge));
                return;
            }

            vec3 l = normalize (vec3 (-0.5, 0.9, 0.6));
            float diffuse = max (dot (n, l), 0.0);
            float spec = pow (max (dot (n, normalize (l + toEye)), 0.0), mix (10.0, 70.0, metal));
            float rim = pow (1.0 - max (dot (n, toEye), 0.0), 2.4);
            float bandMix = 0.5 + 0.5 * sin (vUv.y * 3.0 + vUv.x * 2.0);
            vec3 base = mix (colourA, colourB, bandMix);

            vec3 c = base * (0.10 + 0.75 * diffuse)
                   + vec3 (spec) * (0.25 + 0.75 * metal)
                   + colourB * rim * (0.9 + vBand * 2.5)
                   + base * flash * 0.7;
            gl_FragColor = vec4 (c, 1.0);
        }
    )";

    const char* lineVertexShader = R"(
        attribute vec4 position;
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        varying float vShade;

        void main()
        {
            vShade = position.w;
            gl_Position = projection * view * model * vec4 (position.xyz, 1.0);
        }
    )";

    const char* lineFragmentShader = R"(
        varying float vShade;
        uniform vec4 colour;

        void main()
        {
            gl_FragColor = vec4 (colour.rgb * (0.4 + vShade), colour.a * (0.3 + 0.7 * vShade));
        }
    )";

    std::unique_ptr<juce::OpenGLShaderProgram> buildShader (juce::OpenGLContext& ctx, const char* vertex, const char* fragment)
    {
        auto program = std::make_unique<juce::OpenGLShaderProgram> (ctx);

        if (program->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertex))
            && program->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragment))
            && program->link())
            return program;

        DBG ("Shader error: " << program->getLastError());
        jassertfalse;
        return nullptr;
    }

    void setColourUniform (juce::OpenGLShaderProgram& shader, const char* name, juce::Colour c)
    {
        shader.setUniform (name, c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
    }
}

//==============================================================================
VisualDNA VisualDNA::fromState (const juce::AudioProcessorValueTreeState& state, const juce::String& presetName)
{
    auto v = [&state] (const juce::String& id) { auto* p = state.getRawParameterValue (id); return p != nullptr ? p->load() : 0.0f; };
    auto norm = [&state] (const juce::String& id) { auto* p = state.getParameter (id); return p != nullptr ? p->getValue() : 0.0f; };
    auto osc = [] (int o, const char* name) { return ForgeID::osc (ForgeID::oscLetter (o), name); };

    // The name only nudges the form, so two presets with the same settings still differ a little.
    const auto hash = (juce::uint64) presetName.hashCode64();
    juce::Random random ((juce::int64) hash);
    const auto seedShift = (int) (hash % 3);

    const auto tableA = juce::roundToInt (v (osc (0, ForgeID::table)));
    const auto tableB = juce::roundToInt (v (osc (1, ForgeID::table)));
    const auto warpModeA = juce::roundToInt (v (osc (0, ForgeID::warp1Mode)));
    const auto unisonA = v (osc (0, ForgeID::unison));
    const auto dist = v (ParamID::distDrive) * v (ParamID::distMix);
    const auto distType = v (ParamID::distType);
    const auto filterType = v (ForgeID::filter (0, ForgeID::type));
    const auto cutoff = norm (ForgeID::filter (0, ForgeID::cutoffName));
    const auto res = v (ForgeID::filter (0, ForgeID::res));

    float macroSum = 0.0f;
    for (int m = 0; m < Forge::numMacros; ++m)
        macroSum += v (ForgeID::macro (m));

    float lfo1Depth = 0.0f;
    for (int s = 0; s < Forge::numModSlots; ++s)
        if (juce::roundToInt (v (ForgeID::slot (s, ForgeID::source))) == (int) Forge::ModSource::lfo1)
            lfo1Depth += std::abs (v (ForgeID::slot (s, ForgeID::amount)));

    static const float folds[] { 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 2.0f, 10.0f, 12.0f, 16.0f };
    static const float latitude[] { 0.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 1.0f };

    VisualDNA d;
    d.m1 = folds[(tableA + seedShift) % 10];
    d.n11 = juce::jmap (juce::jlimit (0.0f, 1.0f, dist + v (ParamID::squash) * 0.3f), 5.0f, 0.4f) * (0.9f + 0.2f * random.nextFloat());
    d.n12 = 0.5f + 3.0f * cutoff;
    d.n13 = d.n12 * (0.5f + 1.2f * res);

    d.m2 = latitude[(tableB * 3 + warpModeA + seedShift) % 7];
    d.n21 = juce::jmap (v (osc (0, ForgeID::warp1)), 4.0f, 0.6f);
    d.n22 = 1.0f + 2.0f * v (osc (0, ForgeID::blend));
    d.n23 = d.n22 * (0.6f + 0.8f * v (osc (0, ForgeID::detune)));

    const auto lfoRate = v (ForgeID::lfo (0, ForgeID::rate)) / (float) (Forge::lfoRateNames().size() - 1);
    d.twist = (random.nextBool() ? 1.0f : -1.0f) * (lfoRate * 2.2f + (v (ForgeID::hitSeqOn) > 0.5f ? 1.2f : 0.0f) + macroSum * 0.15f);
    d.spikes = juce::jlimit (0.0f, 0.3f, 0.08f * v (ForgeID::noise) + 0.15f * v (ParamID::combMix) + 0.1f * v (ParamID::vowelMix)
                                          + 0.06f * v (osc (0, ForgeID::warp2)) + 0.03f * v (ForgeID::macro (1)));
    d.spikeFreq = 3.0f + (float) ((tableA + (int) filterType) % 6) * 2.0f + (float) seedShift;

    d.hueA = std::fmod (0.48f + (float) tableA * 0.137f + filterType * 0.021f + random.nextFloat() * 0.12f, 1.0f);
    d.hueB = std::fmod (d.hueA + 0.2f + 0.35f * distType / (float) Choices::distTypes.size(), 1.0f);
    d.saturation = 0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, dist);
    d.wire = juce::jlimit (0.1f, 0.95f, 0.15f + (unisonA - 1.0f) / 15.0f * 0.8f + (v (ForgeID::hyperOn) > 0.5f ? 0.15f : 0.0f));
    d.pulse = juce::jlimit (0.04f, 0.5f, 0.05f + lfo1Depth * 0.35f);
    d.spin = 0.12f + lfoRate * 0.5f;
    d.orbiters = (float) juce::jlimit (0, 6, juce::roundToInt (unisonA / 2.0f) + (v (ForgeID::hyperOn) > 0.5f ? 1 : 0));
    d.metal = juce::jlimit (0.0f, 1.0f, 0.25f + 0.6f * v (ForgeID::filter (0, ForgeID::driveName)) + 0.2f * v (ParamID::heat));
    d.seed = (float) (hash % 1000) / 1000.0f;
    return d;
}

juce::String VisualDNA::describe() const
{
    juce::String s = juce::String (juce::roundToInt (m1)) + "-fold";
    s << (n11 < 1.0f ? " spiked" : (n11 > 3.0f ? " smooth" : " faceted"));
    s << (m2 < 0.5f ? " orb" : (m2 > 4.5f ? " crystal" : " shell"));
    if (std::abs (twist) > 1.0f)
        s << ", twisted";
    if (spikes > 0.08f)
        s << ", rippled";
    if (orbiters >= 1.0f)
        s << ", " << juce::roundToInt (orbiters) << (orbiters >= 2.0f ? " rings" : " ring");
    return s;
}

//==============================================================================
ForgeVisual::ForgeVisual (ForgeProcessor& p) : processor (p)
{
    setOpaque (true);
    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    context.setMultisamplingEnabled (true);
    juce::OpenGLPixelFormat format;
    format.multisamplingLevel = 4;
    context.setPixelFormat (format);
    context.setRenderer (this);
    context.setContinuousRepainting (true);
    context.setComponentPaintingEnabled (false);
    context.attachTo (*this);

    timerCallback();
    startTimerHz (12);
}

ForgeVisual::~ForgeVisual()
{
    stopTimer();
    context.detach();
}

void ForgeVisual::timerCallback()
{
    const auto name = processor.presets.getCurrentName();
    const auto dna = VisualDNA::fromState (processor.parameters, name);
    const auto changed = std::memcmp (&dna, &target, sizeof (VisualDNA)) != 0;
    target = dna;

    for (int i = 0; i < VisualDNA::numFields; ++i)
        shared[(size_t) i].store (dna.data()[i]);

    if (changed && onShapeChanged)
        onShapeChanged (dna);

    // A new preset or scene rebuilds the MilkDrop scene at once; knob moves wait until the
    // sound has settled for a moment, so the scene blends over once rather than every tick.
    stableTicks = changed ? 0 : stableTicks + 1;
    const auto scene = sceneChoice >= 0 ? sceneChoice : MilkScenes::pickFor (dna);
    const auto settled = stableTicks == 4 && std::memcmp (&dna, &builtDNA, sizeof (VisualDNA)) != 0;

    if (scene != builtScene || name != builtName || settled)
    {
        auto text = MilkScenes::build (scene, dna);
        {
            const juce::SpinLock::ScopedLockType lock (sceneLock);
            pendingScene.swapWith (text);
        }

        const auto sceneChanged = scene != builtScene;
        builtScene = scene;
        builtDNA = dna;
        builtName = name;

        if (sceneChanged && onSceneChanged)
            onSceneChanged();
    }
}

void ForgeVisual::setSceneChoice (int scene)
{
    sceneChoice = scene < 0 ? -1 : juce::jlimit (0, MilkScenes::numScenes - 1, scene);
    timerCallback();
}

void ForgeVisual::resized()
{
    viewWidth = juce::jmax (1, getWidth());
    viewHeight = juce::jmax (1, getHeight());
}

void ForgeVisual::mouseDown (const juce::MouseEvent&)
{
    dragYaw = yaw;
    dragPitch = pitch;
}

void ForgeVisual::mouseDrag (const juce::MouseEvent& e)
{
    yaw = dragYaw - (float) e.getDistanceFromDragStartX() * 0.008f;
    pitch = juce::jlimit (-1.2f, 1.2f, dragPitch + (float) e.getDistanceFromDragStartY() * 0.008f);
}

void ForgeVisual::mouseDoubleClick (const juce::MouseEvent&)
{
    yaw = defaultYaw;
    pitch = defaultPitch;
    distance = defaultDistance;
}

void ForgeVisual::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    distance = juce::jlimit (2.5f, 10.0f, distance * (1.0f - wheel.deltaY * 0.4f));
}

//==============================================================================
void ForgeVisual::newOpenGLContextCreated()
{
    shapeShader = buildShader (context, shapeVertexShader, shapeFragmentShader);
    lineShader = buildShader (context, lineVertexShader, lineFragmentShader);

    // The mesh is only (u, v) pairs; the shader turns them into the supershape.
    std::vector<float> uv;
    uv.reserve ((size_t) ((meshU + 1) * (meshV + 1) * 2));
    for (int j = 0; j <= meshV; ++j)
        for (int i = 0; i <= meshU; ++i)
        {
            uv.push_back (-pi + 2.0f * pi * (float) i / (float) meshU);
            uv.push_back (-pi * 0.5f + pi * (float) j / (float) meshV);
        }

    std::vector<GLuint> indices;
    for (int j = 0; j < meshV; ++j)
        for (int i = 0; i < meshU; ++i)
        {
            const auto a = (GLuint) (j * (meshU + 1) + i), b = a + 1, c = a + (GLuint) (meshU + 1), d = c + 1;
            indices.insert (indices.end(), { a, c, b, b, c, d });
        }
    meshIndexCount = (int) indices.size();

    glGenVertexArrays (1, &meshVao);
    glGenBuffers (1, &meshVbo);
    glGenBuffers (1, &meshIbo);
    glBindVertexArray (meshVao);
    glBindBuffer (GL_ARRAY_BUFFER, meshVbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (uv.size() * sizeof (float)), uv.data(), GL_STATIC_DRAW);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, meshIbo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (indices.size() * sizeof (GLuint)), indices.data(), GL_STATIC_DRAW);

    if (shapeShader != nullptr)
    {
        const auto attribute = (GLuint) glGetAttribLocation (shapeShader->getProgramID(), "uv");
        glEnableVertexAttribArray (attribute);
        glVertexAttribPointer (attribute, 2, GL_FLOAT, GL_FALSE, 2 * sizeof (float), nullptr);
    }

    glGenVertexArrays (1, &lineVao);
    glGenBuffers (1, &lineVbo);
    glBindVertexArray (lineVao);
    glBindBuffer (GL_ARRAY_BUFFER, lineVbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (4096 * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);

    if (lineShader != nullptr)
    {
        const auto attribute = (GLuint) glGetAttribLocation (lineShader->getProgramID(), "position");
        glEnableVertexAttribArray (attribute);
        glVertexAttribPointer (attribute, 4, GL_FLOAT, GL_FALSE, 4 * sizeof (float), nullptr);
    }

    glBindVertexArray (0);
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    firstFrame = true;
    milkFailed = milkHasScene = false;
}

void ForgeVisual::openGLContextClosing()
{
    glDeleteBuffers (1, &meshVbo);
    glDeleteBuffers (1, &meshIbo);
    glDeleteBuffers (1, &lineVbo);
    glDeleteVertexArrays (1, &meshVao);
    glDeleteVertexArrays (1, &lineVao);
    meshVbo = meshIbo = lineVbo = meshVao = lineVao = 0;
    milk.destroy();
    releaseBurnTarget();
    shapeShader.reset();
    lineShader.reset();
}

void ForgeVisual::updateSpectrum()
{
    const auto got = processor.visualFeed.pullSamples (incoming.data(), (int) incoming.size());
    if (got <= 0)
    {
        for (auto& b : bands)
            b *= 0.9f;
        return;
    }

    milk.addSamples (incoming.data(), got);

    const auto keep = juce::jmin (got, fftSize);
    std::move (history.begin() + keep, history.end(), history.begin());
    std::copy (incoming.begin() + (got - keep), incoming.begin() + got, history.end() - keep);

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (history.begin(), history.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const auto sampleRate = processor.visualFeed.getSampleRate();
    for (int b = 0; b < numBands; ++b)
    {
        const auto lo = 30.0 * std::pow (12000.0 / 30.0, (double) b / numBands);
        const auto hi = 30.0 * std::pow (12000.0 / 30.0, (double) (b + 1) / numBands);
        const auto binLo = juce::jlimit (1, fftSize / 2 - 1, (int) (lo / sampleRate * fftSize));
        const auto binHi = juce::jlimit (binLo + 1, fftSize / 2, (int) (hi / sampleRate * fftSize));

        auto peak = 0.0f;
        for (int k = binLo; k < binHi; ++k)
            peak = juce::jmax (peak, fftData[(size_t) k]);

        const auto db = juce::Decibels::gainToDecibels (peak / (float) fftSize * 4.0f, -80.0f);
        const auto value = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 60.0f);
        auto& band = bands[(size_t) b];
        band = value > band ? band + (value - band) * 0.6f : band * 0.86f;
    }
}

struct ForgeVisual::FormFrame
{
    VisualDNA dna;
    Mat4 projection, view, model;
    juce::Vector3D<float> eye;
    juce::Colour colourA, colourB;
    float norm = 1.0f;
};

void ForgeVisual::renderOpenGL()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.0, 0.1, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;
    clock += dt;

    if (paused)
    {
        glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
        glClear (GL_COLOR_BUFFER_BIT);
        return;
    }

    // Morph towards the preset's shape.
    const auto morph = firstFrame ? 1.0f : 1.0f - std::exp (-dt * 4.0f);
    for (int i = 0; i < VisualDNA::numFields; ++i)
        current[(size_t) i] += (shared[(size_t) i].load() - current[(size_t) i]) * morph;
    firstFrame = false;

    VisualDNA dna;
    std::memcpy (dna.data(), current.data(), sizeof (VisualDNA));

    updateSpectrum();

    // Flash on new notes.
    for (int n = 0; n < 128; ++n)
    {
        const auto velocity = processor.visualFeed.getVelocity (n);
        if (velocity > 0.0f && lastVelocity[(size_t) n] == 0.0f)
            flash = juce::jmax (flash, 0.4f + 0.6f * velocity);
        lastVelocity[(size_t) n] = velocity;
    }
    flash *= std::exp (-dt * 5.0f);
    level += (processor.visualFeed.getLevel() - level) * 0.2f;

    // Breathe with LFO 1 while a voice plays.
    const auto lfo = processor.getNumActiveVoices() > 0 ? processor.getLfoValue (0) : 0.5f;
    pulseSmoothed += (lfo - pulseSmoothed) * 0.5f;
    spinAngle += dt * dna.spin * (1.0f + level * 3.0f);

    const auto scale = (float) context.getRenderingScale();
    const auto width = juce::roundToInt (scale * (float) viewWidth.load());
    const auto height = juce::roundToInt (scale * (float) viewHeight.load());

    GLint targetFbo = 0;
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &targetFbo);

    const auto y = yaw.load(), p = pitch.load(), dist = distance.load();
    FormFrame f;
    f.dna = dna;
    f.eye = { dist * std::cos (p) * std::sin (y), dist * std::sin (p), dist * std::cos (p) * std::cos (y) };
    f.projection = Mat4::perspective (0.75f, (float) width / (float) juce::jmax (1, height), 0.1f, 100.0f);
    f.view = Mat4::lookAt (f.eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    f.model = Mat4::rotation (1, spinAngle) * Mat4::rotation (0, 0.25f * std::sin (clock * 0.3f + dna.seed * 6.0f));

    // Size the shape so its widest point fits.
    auto max1 = 0.0f, max2 = 0.0f;
    for (int i = 0; i < 64; ++i)
    {
        const auto phi = -pi + 2.0f * pi * (float) i / 64.0f;
        max1 = juce::jmax (max1, superformula (phi, dna.m1, dna.n11, dna.n12, dna.n13));
        max2 = juce::jmax (max2, superformula (phi * 0.5f, dna.m2, dna.n21, dna.n22, dna.n23));
    }
    f.norm = 1.0f / juce::jmax (0.2f, max1 * max2);
    f.colourA = juce::Colour::fromHSV (dna.hueA, dna.saturation, 0.95f, 1.0f);
    f.colourB = juce::Colour::fromHSV (dna.hueB, dna.saturation * 0.9f, 1.0f, 1.0f);

    const auto viewMode = mode.load();
    const auto withMilk = viewMode != formOnly && ensureMilk (width, height);

    if (withMilk)
    {
        juce::String scene;
        {
            const juce::SpinLock::ScopedLockType lock (sceneLock);
            scene.swapWith (pendingScene);
        }

        if (scene.isNotEmpty())
        {
            milk.setScene (scene, milkHasScene);
            milkHasScene = true;
            sceneOnScreen = scene;
        }

        // Burn the form into the scene's feedback, so the scene drags trails off it.
        if (viewMode == formAndMilk && ensureBurnTarget (width, height))
        {
            glBindFramebuffer (GL_FRAMEBUFFER, burnFbo);
            glViewport (0, 0, burnWidth, burnHeight);
            glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
            glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawForm (f, true);
            glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) targetFbo);
            glDisable (GL_DEPTH_TEST);
            glEnable (GL_BLEND);
            glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            milk.burnIn (burnTexture, width, height);
        }

        milk.render ((GLuint) targetFbo, width, height);

        // projectM leaves its own GL state behind; put back what the form needs.
        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) targetFbo);
        glUseProgram (0);
        glBindVertexArray (0);
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, 0);
        glDisable (GL_SCISSOR_TEST);
        glDisable (GL_CULL_FACE);
        glDisable (GL_STENCIL_TEST);
        glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask (GL_TRUE);
        glViewport (0, 0, width, height);
        glClear (GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glViewport (0, 0, width, height);
        const auto background = juce::Colour::fromHSV (dna.hueA, 0.6f, 0.05f + flash * 0.04f, 1.0f);
        glClearColor (background.getFloatRed(), background.getFloatGreen(), background.getFloatBlue(), 1.0f);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    if (viewMode != milkOnly || ! withMilk)
        drawForm (f, false);

    glBindVertexArray (0);
}

bool ForgeVisual::ensureMilk (int width, int height)
{
    if (milk.isReady())
        return true;

    if (milkFailed)
        return false;

    milkFailed = ! milk.create (width, height);
    milkHasScene = false;

    // A recreated context (e.g. the window moved to another screen) picks up where it left off.
    if (! milkFailed && sceneOnScreen.isNotEmpty())
    {
        milk.setScene (sceneOnScreen, false);
        milkHasScene = true;
    }

    return ! milkFailed;
}

bool ForgeVisual::ensureBurnTarget (int width, int height)
{
    if (burnFbo != 0 && width == burnWidth && height == burnHeight)
        return true;

    releaseBurnTarget();
    glGenFramebuffers (1, &burnFbo);
    glGenTextures (1, &burnTexture);
    glGenRenderbuffers (1, &burnDepth);

    glBindTexture (GL_TEXTURE_2D, burnTexture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    glBindRenderbuffer (GL_RENDERBUFFER, burnDepth);
    glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer (GL_RENDERBUFFER, 0);

    GLint previous = 0;
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer (GL_FRAMEBUFFER, burnFbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, burnTexture, 0);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, burnDepth);
    const auto complete = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) previous);

    if (! complete)
    {
        releaseBurnTarget();
        return false;
    }

    burnWidth = width;
    burnHeight = height;
    return true;
}

void ForgeVisual::releaseBurnTarget()
{
    if (burnFbo != 0)      glDeleteFramebuffers (1, &burnFbo);
    if (burnTexture != 0)  glDeleteTextures (1, &burnTexture);
    if (burnDepth != 0)    glDeleteRenderbuffers (1, &burnDepth);
    burnFbo = burnTexture = burnDepth = 0;
    burnWidth = burnHeight = 0;
}

void ForgeVisual::drawForm (const FormFrame& f, bool burn)
{
    const auto& dna = f.dna;
    const auto& projection = f.projection;
    const auto& view = f.view;
    const auto& model = f.model;
    const auto& eye = f.eye;
    const auto& colourA = f.colourA;
    const auto& colourB = f.colourB;
    const auto norm = f.norm;

    glEnable (GL_DEPTH_TEST);
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (shapeShader != nullptr)
    {
        shapeShader->use();
        shapeShader->setUniformMat4 ("projection", projection.m, 1, false);
        shapeShader->setUniformMat4 ("view", view.m, 1, false);
        shapeShader->setUniformMat4 ("model", model.m, 1, false);
        shapeShader->setUniform ("sf1", dna.m1, dna.n11, dna.n12, dna.n13);
        shapeShader->setUniform ("sf2", dna.m2, dna.n21, dna.n22, dna.n23);
        shapeShader->setUniform ("twist", dna.twist);
        shapeShader->setUniform ("spikes", dna.spikes + level * 0.15f);
        shapeShader->setUniform ("spikeFreq", dna.spikeFreq);
        shapeShader->setUniform ("time", clock);
        shapeShader->setUniform ("breathe", (pulseSmoothed - 0.5f) * dna.pulse + flash * 0.08f);
        shapeShader->setUniform ("norm", norm);
        shapeShader->setUniform ("bands", bands.data(), numBands);
        shapeShader->setUniform ("eye", eye.x, eye.y, eye.z);
        shapeShader->setUniform ("flash", flash);
        shapeShader->setUniform ("metal", dna.metal);
        setColourUniform (*shapeShader, "colourA", colourA);
        setColourUniform (*shapeShader, "colourB", colourB);

        glBindVertexArray (meshVao);

        shapeShader->setUniform ("burnPass", burn ? 1.0f : 0.0f);
        shapeShader->setUniform ("inflate", 1.0f);
        shapeShader->setUniform ("wirePass", 0.0f);
        shapeShader->setUniform ("alpha", 1.0f);
        glDrawElements (GL_TRIANGLES, meshIndexCount, GL_UNSIGNED_INT, nullptr);

        // A wire cage just outside the surface, denser with more unison.
        if (dna.wire > 0.05f && ! burn)
        {
            glDepthMask (GL_FALSE);
            glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
            shapeShader->setUniform ("inflate", 1.06f + 0.05f * dna.wire);
            shapeShader->setUniform ("wirePass", 1.0f);
            shapeShader->setUniform ("alpha", dna.wire * 0.6f);
            glDrawElements (GL_TRIANGLES, meshIndexCount, GL_UNSIGNED_INT, nullptr);
            glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
            glDepthMask (GL_TRUE);
        }
    }

    if (lineShader != nullptr)
    {
        lineShader->use();
        lineShader->setUniformMat4 ("projection", projection.m, 1, false);
        lineShader->setUniformMat4 ("view", view.m, 1, false);
        glBindVertexArray (lineVao);
        glBindBuffer (GL_ARRAY_BUFFER, lineVbo);

        // Orbit rings, one per pair of unison voices.
        std::vector<float> ring;
        for (int i = 0; i < ringPoints; ++i)
        {
            const auto a = 2.0f * pi * (float) i / ringPoints;
            ring.insert (ring.end(), { std::cos (a), 0.0f, std::sin (a), 0.5f + 0.5f * std::sin (a * 3.0f + clock * 2.0f) });
        }
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (ring.size() * sizeof (float)), ring.data());

        const auto rings = juce::roundToInt (dna.orbiters);
        for (int r = 0; r < rings; ++r)
        {
            const auto t = (float) r / (float) juce::jmax (1, rings);
            const auto ringModel = Mat4::rotation (1, clock * (0.3f + 0.2f * t) * (r % 2 == 0 ? 1.0f : -1.0f))
                                 * Mat4::rotation (0, 0.4f + t * 2.2f + dna.seed)
                                 * Mat4::scale (1.35f + 0.12f * (float) r + level * 0.3f);
            lineShader->setUniformMat4 ("model", ringModel.m, 1, false);
            const auto c = colourA.interpolatedWith (colourB, t);
            lineShader->setUniform ("colour", c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), 0.55f + flash * 0.4f);
            glDrawArrays (GL_LINE_LOOP, 0, ringPoints);
        }

        // Spectrum halo on the floor: bars radiating out, mirrored left and right.
        std::vector<float> halo;
        for (int side = 0; side < 2; ++side)
            for (int b = 0; b < numBands; ++b)
            {
                const auto a = (side == 0 ? 1.0f : -1.0f) * pi * ((float) b + 0.5f) / (float) numBands;
                const auto inner = 1.5f, outer = inner + 0.15f + bands[(size_t) b] * 1.1f;
                halo.insert (halo.end(), { std::sin (a) * inner, -1.25f, std::cos (a) * inner, 0.1f,
                                           std::sin (a) * outer, -1.25f, std::cos (a) * outer, 0.4f + bands[(size_t) b] });
            }
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (halo.size() * sizeof (float)), halo.data());
        const auto haloModel = Mat4::rotation (1, -spinAngle * 0.3f);
        lineShader->setUniformMat4 ("model", haloModel.m, 1, false);
        lineShader->setUniform ("colour", colourB.getFloatRed(), colourB.getFloatGreen(), colourB.getFloatBlue(), 0.9f);
        glDrawArrays (GL_LINES, 0, (GLsizei) (halo.size() / 4));
    }

    glBindVertexArray (0);
}
