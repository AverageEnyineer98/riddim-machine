#include "MilkScenes.h"
#include "ForgeVisual.h"

namespace
{
    juce::String num (float v) { return juce::String (v, 4); }

    // Every scene starts from these settings; the scene's own lines come after and win.
    const char* baseSettings = R"(MILKDROP_PRESET_VERSION=201
PSVERSION=2
PSVERSION_WARP=2
PSVERSION_COMP=2
[preset00]
fRating=5.000000
fGammaAdj=1.000000
fDecay=0.970000
fVideoEchoZoom=1.000000
fVideoEchoAlpha=0.000000
nVideoEchoOrientation=0
nWaveMode=0
bAdditiveWaves=1
bWaveDots=0
bWaveThick=1
bModWaveAlphaByVolume=1
bMaximizeWaveColor=0
bTexWrap=1
bDarkenCenter=0
bRedBlueStereo=0
bBrighten=0
bDarken=0
bSolarize=0
bInvert=0
fWaveAlpha=0.800000
fWaveScale=1.000000
fWaveSmoothing=0.600000
fWaveParam=0.000000
fModWaveAlphaStart=0.500000
fModWaveAlphaEnd=1.000000
fWarpAnimSpeed=1.000000
fWarpScale=1.000000
fZoomExponent=1.000000
fShader=0.000000
zoom=1.000000
rot=0.000000
cx=0.500000
cy=0.500000
dx=0.000000
dy=0.000000
warp=0.000000
sx=1.000000
sy=1.000000
wave_r=1.000000
wave_g=1.000000
wave_b=1.000000
wave_x=0.500000
wave_y=0.500000
ob_size=0.000000
ob_a=0.000000
ib_size=0.000000
ib_a=0.000000
nMotionVectorsX=0.000000
nMotionVectorsY=0.000000
mv_a=0.000000
b1n=0.000000
b2n=0.000000
b3n=0.000000
b1x=1.000000
b2x=1.000000
b3x=1.000000
b1ed=0.250000
)";

    struct SceneText
    {
        const char* settings;   // key=value lines
        const char* perFrame;   // equations, one per line, after the shared ones
        const char* perPixel;
        const char* warp;       // HLSL bodies (without shader_body)
        const char* comp;
    };

    // q1 fold count, q2-q4 colour A, q5-q7 colour B, q8 twist, q9 grit, q10 width (unison),
    // q11 pulse (LFO depth), q12 spin, q13 metal, q14 seed, q15 smoothness, q16 latitude folds.
    // q20 bass, q21 treble, q22 a clock that runs faster with the bass, q23 a kick envelope.
    const char* sharedPerFrame = R"(kick = max(kick*0.86, min(max(bass - bass_att*1.05, 0)*2, 1));
drift = drift + 0.004 + 0.012*min(bass_att, 3)*(0.5 + q11*2);
q20 = min(bass_att, 3)*0.5;
q21 = min(treb_att, 3)*0.5;
q22 = drift;
q23 = kick;
wave_r = q2 + (q5 - q2)*kick;
wave_g = q3 + (q6 - q3)*kick;
wave_b = q4 + (q7 - q4)*kick;)";

    const SceneText scenes[MilkScenes::numScenes] {
        // Tunnel: zooming feedback folded into a kaleidoscope with the sound's own fold count.
        { "nWaveMode=0\nfWaveScale=1.300000\nfDecay=1.000000\n",
          R"(zoom = 1.03 + 0.05*q20 + 0.02*kick;
rot = 0.012*q8 + 0.01*sin(time*0.3);
wave_mystery = -0.4 + 0.3*q20;)",
          "",
          R"(float2 d = uv - 0.5;
float r = length(d);
float a = atan2(d.y, d.x) + 0.03*q8*sin(r*10.0 - time*3.0);
float2 w = 0.5 + r*float2(cos(a), sin(a));
ret = tex2D(sampler_main, w).xyz;
ret += (tex2D(sampler_noise_lq, w*3.0 + float2(time*0.07, 0.0)).xyz - 0.5)*q9*0.25;
ret = ret*0.965 - 0.004;)",
          R"(float2 d = uv - 0.5;
float r = length(d);
float a = atan2(d.y, d.x) + q22*0.4*q12;
float seg = 6.28318/q1;
a = abs(fmod(a + seg*64.0, seg) - seg*0.5);
float2 k = 0.5 + r*float2(cos(a), sin(a));
float3 c = tex2D(sampler_main, k).xyz;
float3 glow = GetBlur1(k);
c += glow*(0.7 + q20);
float l = saturate(lum(c)*1.4);
float3 tint = lerp(float3(q2, q3, q4), float3(q5, q6, q7), l);
c = tint*l*1.8 + c*0.35 + float3(q5, q6, q7)*q23*0.12*(1.0 - r);
ret = c/(1.0 + c*0.45);)" },

        // Grid: a neon floor racing towards a banded sun, the waveform smoking up off the horizon.
        { "nWaveMode=6\nfWaveScale=0.900000\nwave_y=0.620000\nfDecay=1.000000\n",
          R"(zoom = 1.0;
dy = -0.006 - 0.006*q20;
warp = 0.25 + 0.5*q11;
wave_y = 0.62;)",
          "",
          R"(float2 n = tex2D(sampler_noise_lq, uv*1.5 + float2(0.0, time*0.05)).xy - 0.5;
ret = tex2D(sampler_main, uv + n*0.004*(1.0 + q9*8.0)).xyz;
ret = ret*0.955 - 0.003;)",
          R"(float3 trail = tex2D(sampler_main, uv).xyz + GetBlur1(uv)*0.9;
float3 colA = float3(q2, q3, q4);
float3 colB = float3(q5, q6, q7);
float y = uv.y - 0.62;
float3 c = colA*0.06;
if (y > 0.0)
{
    float z = 0.12/y;
    float x = (uv.x - 0.5)*z*14.0 + 0.6*sin(q22*0.7)*q8;
    float gx = abs(frac(x) - 0.5);
    float gz = abs(frac(z*5.0 + q22*3.0) - 0.5);
    float w = 0.03 + 0.05*saturate(z - 0.5);
    float grid = 1.0 - smoothstep(0.0, w, min(gx, gz));
    c += colB*grid*saturate(y*6.0)*(0.8 + q20 + q23*1.5);
    c += colA*0.18*saturate(1.0 - y*3.0);
}
else
{
    float2 s = float2((uv.x - 0.5)*1.7, y + 0.3);
    float sr = length(s) - 0.24 - 0.03*q20 - 0.02*q23;
    float sun = 1.0 - smoothstep(0.0, 0.01, sr);
    float bands = step(0.45, frac(y*28.0 - q22*1.5));
    sun *= lerp(1.0, bands, saturate((y + 0.3)*8.0));
    c += lerp(colB, colA, saturate(-y*5.0))*sun*1.3;
    c += colA*0.25*saturate(1.0 + y*4.0)*(1.0 - sun);
}
c += trail*lerp(float3(1.0, 1.0, 1.0), colA, 0.4)*1.1;
ret = c/(1.0 + c*0.3);)" },

        // Storm: plasma that churns with the LFO and twist, printed in the sound's palette with
        // a treble-driven colour split.
        { "nWaveMode=5\nfWaveScale=1.100000\nfDecay=1.000000\nbAdditiveWaves=1\n",
          R"(zoom = 1.01 + 0.02*q20;
rot = 0.006*q8;)",
          R"(zoom = zoom + 0.05*q20*sin(rad*8.0 - time*4.0);
rot = rot + 0.03*q8*sin(ang*q1 + time*q12*4.0);
dx = dx + 0.004*sin(y*12.0 + time*2.0)*(0.5 + q11*3.0);
dy = dy + 0.004*cos(x*12.0 + time*1.7)*(0.5 + q11*3.0);)",
          R"(float2 n = tex2D(sampler_noise_lq, uv*2.0 + time*0.03).xy - 0.5;
ret = tex2D(sampler_main, uv + n*0.006*(1.0 + q9*10.0)).xyz;
ret += GetBlur1(uv)*0.04*q21;
ret = ret*0.97 - 0.004;)",
          R"(float2 d = uv - 0.5;
float ca = 0.004 + 0.012*q21;
float3 c;
c.x = tex2D(sampler_main, uv + d*ca).x;
c.y = tex2D(sampler_main, uv).y;
c.z = tex2D(sampler_main, uv - d*ca).z;
c += GetBlur2(uv)*0.8;
float l = saturate(lum(c));
float3 pal = 0.5 + 0.5*cos(6.28318*(l*1.3 + float3(0.0, 0.33, 0.67) + q14 + q22*0.05));
float3 tint = lerp(float3(q2, q3, q4), float3(q5, q6, q7), l);
c = lerp(c, pal*tint*l*2.2, 0.55 + 0.35*q13);
ret = c/(1.0 + c*0.3);)" },

        // Burst: rays out of the core, one pair per fold, flaring on every kick.
        { "nWaveMode=3\nfWaveScale=1.400000\nfDecay=1.000000\n",
          R"(zoom = 0.985 - 0.02*q20;
rot = 0.015*q8;)",
          "",
          R"(float2 d = uv - 0.5;
ret = tex2D(sampler_main, uv).xyz*0.55 + tex2D(sampler_main, 0.5 + d*0.96).xyz*0.42;
ret += (tex2D(sampler_noise_lq, uv*4.0 + time*0.1).xyz - 0.5)*q9*0.12;
ret -= 0.004;)",
          R"(float2 d = uv - 0.5;
float r = length(d);
float a = atan2(d.y, d.x);
float rays = pow(abs(sin(a*q1*0.5 + q22*2.0*q12)), 6.0 - 3.0*saturate(q20));
float3 c = tex2D(sampler_main, uv).xyz + GetBlur1(uv)*1.1;
float3 rayCol = lerp(float3(q2, q3, q4), float3(q5, q6, q7), saturate(r*2.2));
float core = 0.02/(r + 0.02)*(0.25 + q23);
c += rayCol*rays*saturate(1.0 - r*1.2)*(0.35 + 0.8*q20 + 0.8*q23);
c += rayCol*core*0.8;
ret = c/(1.0 + c*0.3);)" }
    };

    void addNumbered (juce::String& out, const juce::String& prefix, const juce::String& body, int& counter, bool quoted)
    {
        for (const auto& line : juce::StringArray::fromLines (body))
            if (line.trim().isNotEmpty())
                out << prefix << counter++ << "=" << (quoted ? "`" : "") << line << "\n";
    }

    void addShader (juce::String& out, const char* prefix, const char* body)
    {
        int n = 1;
        addNumbered (out, prefix, "shader_body\n{", n, true);
        addNumbered (out, prefix, body, n, true);
        addNumbered (out, prefix, "}", n, true);
    }
}

juce::String MilkScenes::getName (int scene)
{
    static const char* names[] { "Tunnel", "Grid", "Storm", "Burst" };
    return names[juce::jlimit (0, numScenes - 1, scene)];
}

int MilkScenes::pickFor (const VisualDNA& dna)
{
    // Thresholds split the factory set roughly evenly between the four scenes.
    if (dna.metal >= 0.56f || dna.spikes >= 0.075f)
        return burst;
    if (std::abs (dna.twist) >= 2.1f)
        return storm;
    if (dna.n11 < 1.6f || dna.wire >= 0.35f)
        return tunnel;
    return grid;
}

juce::String MilkScenes::build (int scene, const VisualDNA& dna)
{
    const auto& s = scenes[juce::jlimit (0, numScenes - 1, scene)];
    const auto colourA = juce::Colour::fromHSV (dna.hueA, dna.saturation, 1.0f, 1.0f);
    const auto colourB = juce::Colour::fromHSV (dna.hueB, dna.saturation * 0.9f, 1.0f, 1.0f);

    juce::String out (baseSettings);
    out << s.settings;

    const juce::String constants =
        "q1 = " + num (juce::jlimit (2.0f, 16.0f, dna.m1)) + ";\n"
        "q2 = " + num (colourA.getFloatRed()) + ";\n"
        "q3 = " + num (colourA.getFloatGreen()) + ";\n"
        "q4 = " + num (colourA.getFloatBlue()) + ";\n"
        "q5 = " + num (colourB.getFloatRed()) + ";\n"
        "q6 = " + num (colourB.getFloatGreen()) + ";\n"
        "q7 = " + num (colourB.getFloatBlue()) + ";\n"
        "q8 = " + num (juce::jlimit (-3.0f, 3.0f, dna.twist)) + ";\n"
        "q9 = " + num (juce::jlimit (0.0f, 1.0f, dna.spikes * 3.0f + 0.3f / juce::jmax (0.4f, dna.n11))) + ";\n"
        "q10 = " + num (dna.wire) + ";\n"
        "q11 = " + num (dna.pulse) + ";\n"
        "q12 = " + num (dna.spin) + ";\n"
        "q13 = " + num (dna.metal) + ";\n"
        "q14 = " + num (dna.seed) + ";\n"
        "q15 = " + num (juce::jlimit (0.0f, 1.0f, dna.n11 / 5.0f)) + ";\n"
        "q16 = " + num (dna.m2) + ";";

    int n = 1;
    addNumbered (out, "per_frame_", constants, n, false);
    addNumbered (out, "per_frame_", sharedPerFrame, n, false);
    addNumbered (out, "per_frame_", s.perFrame, n, false);

    n = 1;
    addNumbered (out, "per_pixel_", s.perPixel, n, false);

    addShader (out, "warp_", s.warp);
    addShader (out, "comp_", s.comp);
    return out;
}
