#include "Visualiser3D.h"

using namespace juce::gl;

namespace
{
    // Column-major 4x4 matrix, laid out the way OpenGL expects.
    struct Mat4
    {
        float m[16] {};

        static Mat4 identity()
        {
            Mat4 r;
            r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
            return r;
        }

        static Mat4 translation (float x, float y, float z)
        {
            auto r = identity();
            r.m[12] = x; r.m[13] = y; r.m[14] = z;
            return r;
        }

        static Mat4 scale (float x, float y, float z)
        {
            Mat4 r;
            r.m[0] = x; r.m[5] = y; r.m[10] = z; r.m[15] = 1.0f;
            return r;
        }

        static Mat4 rotationX (float radians)
        {
            auto r = identity();
            const auto c = std::cos (radians), s = std::sin (radians);
            r.m[5] = c;  r.m[6] = s;
            r.m[9] = -s; r.m[10] = c;
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

    constexpr float defaultYaw = 0.0f, defaultPitch = 0.38f, defaultDistance = 36.0f;

    // Keyboard layout, in world units.
    constexpr float keySpacing = 1.1f;
    constexpr float whiteWidth = 1.0f, whiteHeight = 0.7f, whiteLength = 5.5f;
    constexpr float blackWidth = 0.62f, blackHeight = 0.6f, blackLength = 3.4f;
    constexpr float maxKeyTilt = 0.07f; // radians when fully pressed

    // Spectrum terrain.
    constexpr float terrainWidth = 58.0f, terrainRowSpacing = 0.75f, terrainFront = -4.0f;
    constexpr float terrainFloor = -1.2f, terrainPeak = 8.0f;
    constexpr float minFreq = 30.0f, maxFreq = 14000.0f;

    const juce::Colour backgroundColour { 0xff0b0b14 };

    bool isBlackKey (int note)
    {
        const auto pc = note % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    int whiteIndexBelow (int note, int lowest)
    {
        int count = 0;
        for (int n = lowest; n < note; ++n)
            if (! isBlackKey (n))
                ++count;
        return count;
    }

    // Purple for low notes through to cyan for high ones.
    juce::Colour colourForNote (int note)
    {
        const auto t = (float) (note - Visualiser3D::lowestNote) / (float) (Visualiser3D::highestNote - Visualiser3D::lowestNote);
        return juce::Colour::fromHSV (0.80f - 0.30f * t, 0.85f, 1.0f, 1.0f);
    }

    // 36 vertices, each position (xyz) + normal (xyz), for a unit cube centred on the origin.
    std::vector<float> makeCube()
    {
        // Each face: its normal and two tangent axes spanning it.
        const float faces[6][3][3] = {
            { {  1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
            { { 0,  1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } }, { { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
            { { 0, 0,  1 }, { 1, 0, 0 }, { 0, 1, 0 } }, { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } } };
        const float corners[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

        std::vector<float> v;

        for (const auto& face : faces)
        {
            const auto* n = face[0];
            const auto* a = face[1];
            const auto* b = face[2];

            for (const auto& c : corners)
            {
                for (int i = 0; i < 3; ++i)
                    v.push_back (0.5f * (n[i] + c[0] * a[i] + c[1] * b[i]));
                for (int i = 0; i < 3; ++i)
                    v.push_back (n[i]);
            }
        }

        return v;
    }

    const char* keyVertexShader = R"(
        attribute vec3 position;
        attribute vec3 normal;
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        varying vec3 vNormal;
        varying vec3 vWorld;

        void main()
        {
            vec4 world = model * vec4 (position, 1.0);
            vWorld = world.xyz;
            vNormal = normalize (mat3 (model) * normal);
            gl_Position = projection * view * world;
        }
    )";

    const char* keyFragmentShader = R"(
        varying vec3 vNormal;
        varying vec3 vWorld;
        uniform vec3 baseColour;
        uniform vec3 glowColour;
        uniform float glow;
        uniform vec3 eye;

        void main()
        {
            vec3 n = normalize (vNormal);
            vec3 l = normalize (vec3 (-0.4, 1.0, 0.7));
            float diffuse = max (dot (n, l), 0.0);
            vec3 h = normalize (l + normalize (eye - vWorld));
            float spec = pow (max (dot (n, h), 0.0), 32.0);

            vec3 lit = baseColour * (0.22 + 0.78 * diffuse) + vec3 (spec * 0.3);
            vec3 c = mix (lit, glowColour * (0.55 + 0.6 * diffuse), glow) + glowColour * glow * 0.35;
            gl_FragColor = vec4 (c, 1.0);
        }
    )";

    const char* terrainVertexShader = R"(
        attribute vec3 position;
        uniform mat4 projection;
        uniform mat4 view;
        varying float vHeight;
        varying float vDepth;

        void main()
        {
            vHeight = position.y;
            vDepth = position.z;
            gl_Position = projection * view * vec4 (position, 1.0);
        }
    )";

    const char* terrainFragmentShader = R"(
        varying float vHeight;
        varying float vDepth;
        uniform float floorY;
        uniform float peakY;
        uniform float level;

        void main()
        {
            float t = clamp ((vHeight - floorY) / peakY, 0.0, 1.0);
            vec3 c = mix (vec3 (0.45, 0.15, 0.85), vec3 (0.2, 0.95, 1.0), t);
            float fade = clamp (1.0 + (vDepth + 4.0) / 46.0, 0.0, 1.0);
            gl_FragColor = vec4 (c * fade * (0.35 + 0.9 * t + level), 1.0);
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
}

//==============================================================================
Visualiser3D::Visualiser3D (VisualFeed& f, juce::MidiKeyboardState& state)
    : feed (f), keyboardState (state)
{
    setOpaque (true);
    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    context.setRenderer (this);
    context.setContinuousRepainting (true);
    context.setComponentPaintingEnabled (false);
    context.attachTo (*this);
}

Visualiser3D::~Visualiser3D()
{
    context.detach();
}

void Visualiser3D::resized()
{
    viewWidth = juce::jmax (1, getWidth());
    viewHeight = juce::jmax (1, getHeight());
}

void Visualiser3D::mouseDown (const juce::MouseEvent&)
{
    dragStartYaw = yaw;
    dragStartPitch = pitch;
}

void Visualiser3D::mouseDrag (const juce::MouseEvent& e)
{
    yaw = juce::jlimit (-1.4f, 1.4f, dragStartYaw - (float) e.getDistanceFromDragStartX() * 0.006f);
    pitch = juce::jlimit (0.05f, 1.3f, dragStartPitch + (float) e.getDistanceFromDragStartY() * 0.006f);
}

void Visualiser3D::mouseDoubleClick (const juce::MouseEvent&)
{
    yaw = defaultYaw;
    pitch = defaultPitch;
    distance = defaultDistance;
}

void Visualiser3D::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    distance = juce::jlimit (20.0f, 90.0f, distance * (1.0f - wheel.deltaY * 0.5f));
}

//==============================================================================
void Visualiser3D::newOpenGLContextCreated()
{
    keyShader = buildShader (context, keyVertexShader, keyFragmentShader);
    terrainShader = buildShader (context, terrainVertexShader, terrainFragmentShader);

    const auto cube = makeCube();

    glGenVertexArrays (1, &cubeVao);
    glGenBuffers (1, &cubeVbo);
    glBindVertexArray (cubeVao);
    glBindBuffer (GL_ARRAY_BUFFER, cubeVbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (cube.size() * sizeof (float)), cube.data(), GL_STATIC_DRAW);

    if (keyShader != nullptr)
    {
        const auto pos = (GLuint) glGetAttribLocation (keyShader->getProgramID(), "position");
        const auto nrm = (GLuint) glGetAttribLocation (keyShader->getProgramID(), "normal");
        glEnableVertexAttribArray (pos);
        glVertexAttribPointer (pos, 3, GL_FLOAT, GL_FALSE, 6 * sizeof (float), nullptr);
        glEnableVertexAttribArray (nrm);
        glVertexAttribPointer (nrm, 3, GL_FLOAT, GL_FALSE, 6 * sizeof (float), (const void*) (3 * sizeof (float)));
    }

    glGenVertexArrays (1, &terrainVao);
    glGenBuffers (1, &terrainVbo);
    glBindVertexArray (terrainVao);
    glBindBuffer (GL_ARRAY_BUFFER, terrainVbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (terrainVertices.size() * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);

    if (terrainShader != nullptr)
    {
        const auto pos = (GLuint) glGetAttribLocation (terrainShader->getProgramID(), "position");
        glEnableVertexAttribArray (pos);
        glVertexAttribPointer (pos, 3, GL_FLOAT, GL_FALSE, 3 * sizeof (float), nullptr);
    }

    glBindVertexArray (0);
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
}

void Visualiser3D::openGLContextClosing()
{
    glDeleteBuffers (1, &cubeVbo);
    glDeleteBuffers (1, &terrainVbo);
    glDeleteVertexArrays (1, &cubeVao);
    glDeleteVertexArrays (1, &terrainVao);
    cubeVbo = terrainVbo = cubeVao = terrainVao = 0;
    keyShader.reset();
    terrainShader.reset();
}

void Visualiser3D::renderOpenGL()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.0, 0.1, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;
    clock += dt;

    updateNotes (dt);
    updateSpectrum();

    const auto level = juce::jmin (1.0f, feed.getLevel() * 3.0f);
    smoothedLevel += (level - smoothedLevel) * juce::jmin (1.0f, dt * (level > smoothedLevel ? 25.0f : 4.0f));

    const auto scale = (float) context.getRenderingScale();
    const auto w = juce::roundToInt (scale * (float) viewWidth.load());
    const auto h = juce::roundToInt (scale * (float) viewHeight.load());
    glViewport (0, 0, w, h);

    const auto bg = backgroundColour.interpolatedWith (juce::Colour (0xff2a0f4a), smoothedLevel * 0.6f);
    glClearColor (bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);

    // Orbit camera with a slow idle sway, punched forward by the level.
    const auto camYaw = yaw.load() + 0.04f * std::sin (clock * 0.3f);
    const auto camPitch = pitch.load();
    const auto camDist = distance.load() - smoothedLevel * 3.0f;
    const juce::Vector3D<float> target { 0.0f, 2.0f, -5.0f };
    const juce::Vector3D<float> eye { target.x + camDist * std::sin (camYaw) * std::cos (camPitch),
                                      target.y + camDist * std::sin (camPitch),
                                      target.z + camDist * std::cos (camYaw) * std::cos (camPitch) };

    const auto view = Mat4::lookAt (eye, target, { 0.0f, 1.0f, 0.0f });
    const auto projection = Mat4::perspective (juce::degreesToRadians (42.0f), (float) w / (float) juce::jmax (1, h), 0.5f, 250.0f);
    const float eyeArray[3] = { eye.x, eye.y, eye.z };

    drawTerrain (view.m, projection.m);
    if (showKeyboard)
        drawKeys (view.m, projection.m, eyeArray);

    glBindVertexArray (0);
}

void Visualiser3D::updateNotes (float dt)
{
    for (int note = lowestNote; note <= highestNote; ++note)
    {
        auto target = feed.getVelocity (note);

        // On-screen keys register here even when no audio is running.
        if (target <= 0.0f && keyboardState.isNoteOnForChannels (0xffff, note))
            target = 0.8f;

        auto& g = noteGlow[(size_t) note];
        g += (target - g) * juce::jmin (1.0f, dt * (target > g ? 30.0f : 6.0f));
    }
}

void Visualiser3D::updateSpectrum()
{
    const auto numNew = feed.pullSamples (incoming.data(), (int) incoming.size());

    auto* row = spectrumRows.data() + (size_t) (((newestRow + 1) % terrainRows) * terrainColumns);
    const auto* previous = spectrumRows.data() + (size_t) (newestRow * terrainColumns);
    newestRow = (newestRow + 1) % terrainRows;

    if (numNew == 0)
    {
        for (int c = 0; c < terrainColumns; ++c)
            row[c] = previous[c] * 0.9f;
        return;
    }

    // Slide the newest samples into the analysis window.
    if (numNew >= fftSize)
    {
        std::copy (incoming.begin() + (numNew - fftSize), incoming.begin() + numNew, sampleHistory.begin());
    }
    else
    {
        std::move (sampleHistory.begin() + numNew, sampleHistory.end(), sampleHistory.begin());
        std::copy (incoming.begin(), incoming.begin() + numNew, sampleHistory.end() - numNew);
    }

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (sampleHistory.begin(), sampleHistory.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const auto sampleRate = (float) feed.getSampleRate();
    const auto binHz = sampleRate / (float) fftSize;

    for (int c = 0; c < terrainColumns; ++c)
    {
        const auto t = (float) c / (float) (terrainColumns - 1);
        const auto freq = minFreq * std::pow (maxFreq / minFreq, t);
        const auto bin = juce::jlimit (1, fftSize / 2 - 1, (int) (freq / binHz));
        const auto magnitude = fftData[(size_t) bin] / (float) (fftSize / 4);
        const auto db = juce::Decibels::gainToDecibels (magnitude, -80.0f);
        const auto value = juce::jmap (db, -72.0f, -6.0f, 0.0f, 1.0f);

        // Smooth against the previous row so the terrain ripples rather than flickers.
        row[c] = juce::jlimit (0.0f, 1.0f, juce::jmax (value, previous[c] * 0.85f));
    }
}

void Visualiser3D::drawKeys (const float* view, const float* projection, const float* eye)
{
    if (keyShader == nullptr)
        return;

    keyShader->use();
    keyShader->setUniformMat4 ("projection", projection, 1, GL_FALSE);
    keyShader->setUniformMat4 ("view", view, 1, GL_FALSE);
    keyShader->setUniform ("eye", eye[0], eye[1], eye[2]);
    glBindVertexArray (cubeVao);

    const auto numWhite = whiteIndexBelow (highestNote + 1, lowestNote);
    const auto leftX = -0.5f * (float) (numWhite - 1) * keySpacing;

    auto drawKey = [&] (int note, bool black)
    {
        const auto glow = juce::jlimit (0.0f, 1.0f, noteGlow[(size_t) note]);
        const auto whiteIdx = whiteIndexBelow (note, lowestNote);
        const auto x = leftX + (float) whiteIdx * keySpacing - (black ? 0.5f * keySpacing : 0.0f);

        const auto width  = black ? blackWidth : whiteWidth;
        const auto height = black ? blackHeight : whiteHeight;
        const auto length = black ? blackLength : whiteLength;
        const auto y = black ? whiteHeight * 0.5f + height * 0.5f - 0.05f : 0.0f;

        // Hinge at the back edge so pressed keys dip at the front.
        const auto model = Mat4::translation (x, y, 0.0f)
                         * Mat4::rotationX (glow * maxKeyTilt)
                         * Mat4::translation (0.0f, -glow * 0.12f, length * 0.5f)
                         * Mat4::scale (width, height, length);

        const auto base = black ? juce::Colour (0xff15151f) : juce::Colour (0xffd8d8e6);
        const auto glowColour = colourForNote (note);

        keyShader->setUniformMat4 ("model", model.m, 1, GL_FALSE);
        keyShader->setUniform ("baseColour", base.getFloatRed(), base.getFloatGreen(), base.getFloatBlue());
        keyShader->setUniform ("glowColour", glowColour.getFloatRed(), glowColour.getFloatGreen(), glowColour.getFloatBlue());
        keyShader->setUniform ("glow", glow);
        glDrawArrays (GL_TRIANGLES, 0, 36);
    };

    for (int note = lowestNote; note <= highestNote; ++note)
        if (! isBlackKey (note))
            drawKey (note, false);

    for (int note = lowestNote; note <= highestNote; ++note)
        if (isBlackKey (note))
            drawKey (note, true);
}

void Visualiser3D::drawTerrain (const float* view, const float* projection)
{
    if (terrainShader == nullptr)
        return;

    // Row 0 is the newest spectrum, nearest the keyboard; older rows scroll away.
    for (int r = 0; r < terrainRows; ++r)
    {
        const auto source = (newestRow - r + terrainRows) % terrainRows;
        const auto* values = spectrumRows.data() + (size_t) (source * terrainColumns);
        auto* out = terrainVertices.data() + (size_t) (r * terrainColumns * 3);

        for (int c = 0; c < terrainColumns; ++c)
        {
            out[c * 3 + 0] = ((float) c / (float) (terrainColumns - 1) - 0.5f) * terrainWidth;
            out[c * 3 + 1] = terrainFloor + values[c] * terrainPeak;
            out[c * 3 + 2] = terrainFront - (float) r * terrainRowSpacing;
        }
    }

    terrainShader->use();
    terrainShader->setUniformMat4 ("projection", projection, 1, GL_FALSE);
    terrainShader->setUniformMat4 ("view", view, 1, GL_FALSE);
    terrainShader->setUniform ("floorY", terrainFloor);
    terrainShader->setUniform ("peakY", terrainPeak);
    terrainShader->setUniform ("level", smoothedLevel * 0.5f);

    glBindVertexArray (terrainVao);
    glBindBuffer (GL_ARRAY_BUFFER, terrainVbo);
    glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (terrainVertices.size() * sizeof (float)), terrainVertices.data());

    glEnable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE);
    glDepthMask (GL_FALSE);

    for (int r = terrainRows - 1; r >= 0; --r)
        glDrawArrays (GL_LINE_STRIP, r * terrainColumns, terrainColumns);

    glDepthMask (GL_TRUE);
    glDisable (GL_BLEND);
}
