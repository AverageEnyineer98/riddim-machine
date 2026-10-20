#include "ForgeMagic.h"
#include "ForgeBinaryData.h"

namespace
{
    constexpr int candidatesPerClick = 5;
    constexpr float temperature = 0.95f, minNovelty = 2.5f;

    const char* adjectives[] { "Rusty", "Toxic", "Heavy", "Wonky", "Broken", "Filthy", "Hollow", "Grim", "Molten", "Feral",
                               "Crooked", "Static", "Gnarly", "Vile", "Sunken", "Rabid", "Murky", "Twisted", "Sour", "Brutal",
                               "Cold", "Acid", "Bent", "Dusty", "Electric", "Greasy", "Hungry", "Jagged", "Mutant", "Nasty",
                               "Oily", "Rotten", "Thick", "Ugly", "Zombie", "Neon", "Chrome", "Plasma", "Gutter", "Venom" };

    juce::StringArray wordsFor (const juce::String& category)
    {
        static const std::map<juce::String, juce::StringArray> words {
            { "Wobble", { "Wob", "Wub", "Lurch", "Sway", "Bounce" } }, { "Riddim", { "Chop", "Stomp", "Stutter", "Step", "Grid" } },
            { "Growl", { "Growl", "Snarl", "Throat", "Gurgle", "Beast" } }, { "Tearout", { "Tear", "Rip", "Shred", "Grind", "Wreck" } },
            { "Yoi", { "Yoi", "Talk", "Mouth", "Vowel", "Yap" } }, { "Screech", { "Screech", "Siren", "Squeal", "Laser", "Shriek" } },
            { "Metal", { "Clang", "Pipe", "Gear", "Anvil", "Rattle" } }, { "Robot", { "Droid", "Circuit", "Glitch", "Servo", "Modem" } },
            { "Reese", { "Reese", "Drift", "Smear", "Rumble", "Haze" } }, { "Sub", { "Sub", "Weight", "Floor", "Thump", "Belly" } },
            { "Lead", { "Lead", "Hook", "Blade", "Signal", "Flare" } }, { "Pad", { "Mist", "Cloud", "Aura", "Veil", "Glow" } },
            { "Pluck", { "Pluck", "Ping", "Tick", "Flick", "Snap" } }, { "FX", { "Rise", "Sweep", "Blast", "Impact", "Whoosh" } } };
        const auto it = words.find (category);
        return it != words.end() ? it->second : juce::StringArray { "Sound" };
    }

    juce::String macroNameFor (const juce::String& dest)
    {
        static const std::map<juce::String, juce::String> names {
            { "Dist Drive", "Grit" }, { "A Warp 1", "Warp" }, { "A Warp 2", "Grit" }, { "A Position", "Movement" },
            { "B Position", "Movement" }, { "FX Vowel", "Vowel" }, { "Comb Mix", "Comb" }, { "Smash", "Smash" },
            { "Hyper Mix", "Width" }, { "A Detune", "Width" }, { "Sub Level", "Sub" }, { "Reverb Mix", "Space" },
            { "Delay Mix", "Space" }, { "LFO 1 Rate", "Wob Speed" }, { "Shift Amount", "Shift" }, { "F1 Cutoff", "Tone" },
            { "Pitch Env", "Punch" }, { "F1 Var", "Mouth" } };
        const auto it = names.find (dest);
        return it != names.end() ? it->second : dest;
    }

    // Drawn LFO shapes, matching the factory recipes in Tools/make_forge_presets.py.
    using Points = std::vector<std::array<float, 3>>;

    Points gateCurve (juce::Random& r, int steps, bool inverted)
    {
        Points points;
        for (int i = 0; i < steps; ++i)
        {
            const auto on = i == 0 || r.nextFloat() < 0.7f;
            const auto y = inverted ? (on ? 0.0f : 1.0f) : (on ? 1.0f : 0.0f);
            const auto x0 = (float) i / (float) steps, x1 = (float) (i + 1) / (float) steps;
            points.push_back ({ i == 0 ? 0.0f : x0 + 0.002f, y, 0.0f });
            points.push_back ({ x1 - 0.004f, y, 0.0f });
        }
        points.push_back ({ 1.0f, points.front()[1], 0.0f });
        return points;
    }

    Points wobbleCurve (juce::Random& r)
    {
        const auto peak = 0.15f + 0.3f * r.nextFloat();
        Points points { { 0.0f, 0.0f, r.nextFloat() - 0.5f }, { peak, 1.0f, -0.8f + 1.4f * r.nextFloat() } };
        if (r.nextBool())
        {
            const auto notch = juce::jmin (0.85f, peak + 0.1f + 0.4f * r.nextFloat());
            points.push_back ({ notch, 0.1f + 0.4f * r.nextFloat(), 0.0f });
            points.push_back ({ juce::jmin (0.97f, notch + 0.08f), 0.5f + 0.4f * r.nextFloat(), r.nextFloat() * 1.2f - 0.6f });
        }
        points.push_back ({ 1.0f, 0.0f, 0.0f });
        return points;
    }

    Points steppedCurve (juce::Random& r, int steps)
    {
        Points points;
        for (int i = 0; i < steps; ++i)
        {
            const auto y = (float) r.nextInt (5) / 4.0f;
            points.push_back ({ (float) i / (float) steps + (i > 0 ? 0.001f : 0.0f), y, 0.0f });
            points.push_back ({ (float) (i + 1) / (float) steps - 0.003f, y, 0.0f });
        }
        points.push_back ({ 1.0f, points.front()[1], 0.0f });
        return points;
    }

    juce::String curveText (const Points& points)
    {
        juce::StringArray parts;
        for (const auto& p : points)
            parts.add (juce::String (p[0], 4) + "," + juce::String (p[1], 4) + "," + juce::String (p[2], 3));
        return parts.joinIntoString (";");
    }

    float gaussian (juce::Random& r)
    {
        const auto u1 = juce::jmax (1.0e-7f, r.nextFloat()), u2 = r.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
    }
}

//==============================================================================
struct ForgeMagic::Generator::Model
{
    struct Layer { int out = 0, in = 0; const float* weights = nullptr; const float* bias = nullptr; };
    struct Choice { juce::String id; int n; };
    struct Profile { std::array<float, ForgeAudition::numBands> bands {}, spread {}; float movement = 0.5f; };

    int latent = 0;
    juce::StringArray categories;
    std::vector<Layer> layers;
    juce::StringArray continuous;
    std::vector<Choice> choices;
    std::map<juce::String, Profile> profiles;
    juce::HeapBlock<float> storage;

    bool load()
    {
        const auto spec = juce::JSON::parse (juce::String::fromUTF8 (ForgeBinaryData::magic_model_json, ForgeBinaryData::magic_model_jsonSize));
        if (! spec.isObject())
            return false;

        latent = (int) spec["latent"];
        for (const auto& c : *spec["categories"].getArray())
            categories.add (c.toString());
        for (const auto& c : *spec["continuous"].getArray())
            continuous.add (c.toString());
        for (const auto& c : *spec["choices"].getArray())
            choices.push_back ({ c["id"].toString(), (int) c["n"] });

        if (auto* p = spec["profiles"].getDynamicObject())
            for (const auto& entry : p->getProperties())
            {
                Profile profile;
                for (int b = 0; b < ForgeAudition::numBands; ++b)
                {
                    profile.bands[(size_t) b] = (float) (double) entry.value["bands"][b];
                    profile.spread[(size_t) b] = (float) (double) entry.value["spread"][b];
                }
                profile.movement = (float) (double) entry.value["movement"];
                profiles[entry.name.toString()] = profile;
            }

        // Weights: per layer, an (out x in) matrix then the bias, float32 little-endian.
        const auto count = (size_t) ForgeBinaryData::magic_model_binSize / sizeof (float);
        storage.allocate (count, false);
        std::memcpy (storage.get(), ForgeBinaryData::magic_model_bin, count * sizeof (float));

        size_t offset = 0;
        for (const auto& shape : *spec["layers"].getArray())
        {
            Layer layer;
            layer.out = (int) shape[0];
            layer.in = (int) shape[1];
            layer.weights = storage.get() + offset;
            offset += (size_t) (layer.out * layer.in);
            layer.bias = storage.get() + offset;
            offset += (size_t) layer.out;
            layers.push_back (layer);
        }

        const auto expectedOut = continuous.size() + std::accumulate (choices.begin(), choices.end(), 0, [] (int s, const Choice& c) { return s + c.n; });
        return offset == count && layers.size() == 3 && layers.back().out == expectedOut;
    }

    std::vector<float> decode (const std::vector<float>& z, int category) const
    {
        std::vector<float> x (z);
        for (int c = 0; c < categories.size(); ++c)
            x.push_back (c == category ? 1.0f : 0.0f);

        for (size_t l = 0; l < layers.size(); ++l)
        {
            const auto& layer = layers[l];
            std::vector<float> y ((size_t) layer.out);
            for (int o = 0; o < layer.out; ++o)
            {
                const auto* row = layer.weights + (size_t) o * (size_t) layer.in;
                auto sum = layer.bias[o];
                for (int i = 0; i < layer.in; ++i)
                    sum += row[i] * x[(size_t) i];
                y[(size_t) o] = l + 1 < layers.size() ? juce::jmax (0.0f, sum) : sum;
            }
            x = std::move (y);
        }
        return x;
    }

    float score (const ForgeAudition::Features& f, const juce::String& category) const
    {
        const auto it = profiles.find (category);
        if (it == profiles.end())
            return 0.0f;

        float sum = 0.0f;
        for (int b = 0; b < ForgeAudition::numBands; ++b)
        {
            const auto d = (f.bands[(size_t) b] - it->second.bands[(size_t) b]) / it->second.spread[(size_t) b];
            sum += d * d;
        }
        const auto movement = (f.movement - it->second.movement) / 0.25f;
        return sum / ForgeAudition::numBands + 0.5f * movement * movement;
    }
};

//==============================================================================
const juce::StringArray& ForgeMagic::styles()
{
    static const auto names = []
    {
        juce::StringArray list { "Any" };
        const auto spec = juce::JSON::parse (juce::String::fromUTF8 (ForgeBinaryData::magic_model_json, ForgeBinaryData::magic_model_jsonSize));
        if (auto* categories = spec["categories"].getArray())
            for (const auto& c : *categories)
                list.add (c.toString());
        return list;
    }();
    return names;
}

ForgeMagic::Generator::Generator() : model (std::make_unique<Model>())
{
    if (! model->load())
        model.reset();
}

ForgeMagic::Generator::~Generator() = default;

bool ForgeMagic::Generator::isReady() const { return model != nullptr; }

FactoryPreset ForgeMagic::Generator::make (juce::Random& random, const juce::String& style)
{
    const std::scoped_lock guard (lock);
    jassert (isReady());

    auto& state = host.parameters;
    FactoryPreset best;
    float bestScore = std::numeric_limits<float>::max();
    std::vector<float> bestZ;
    ForgeAudition::Features bestFeatures;
    int tried = 0;

    for (int attempt = 0; attempt < candidatesPerClick * 4 && tried < candidatesPerClick; ++attempt)
    {
        // A fresh point in latent space, away from the last few results.
        std::vector<float> z ((size_t) model->latent);
        for (auto& v : z)
            v = gaussian (random) * temperature;

        auto novel = true;
        for (const auto& old : recent)
        {
            float d = 0.0f;
            for (size_t i = 0; i < z.size(); ++i)
                d += (z[i] - old[i]) * (z[i] - old[i]);
            novel = novel && std::sqrt (d) > minNovelty;
        }
        if (! novel)
            continue;

        auto category = model->categories.indexOf (style);
        if (category < 0)
            category = random.nextInt (model->categories.size());
        const auto categoryName = model->categories[category];
        const auto out = model->decode (z, category);

        FactoryPreset preset;
        preset.category = categoryName;

        size_t k = 0;
        for (const auto& id : model->continuous)
            if (auto* p = state.getParameter (id))
                preset.values.emplace_back (id, p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, 1.0f / (1.0f + std::exp (-out[k++])))));

        std::map<juce::String, int> picked;
        for (const auto& choice : model->choices)
        {
            int bestIndex = 0;
            for (int i = 1; i < choice.n; ++i)
                if (out[k + (size_t) i] > out[k + (size_t) bestIndex])
                    bestIndex = i;
            k += (size_t) choice.n;
            picked[choice.id] = bestIndex;
            preset.values.emplace_back (choice.id, (float) bestIndex);
        }

        // Drawn LFOs: a chop where an LFO turns the volume down, otherwise a wobble or steps.
        const auto customShape = Forge::lfoShapeNames().size() - 1;
        for (int l = 0; l < Forge::numLfos; ++l)
        {
            if (picked[ForgeID::lfo (l, ForgeID::shape)] != customShape)
                continue;

            auto chops = false;
            for (int s = 0; s < Forge::numModSlots; ++s)
                chops = chops || (picked[ForgeID::slot (s, ForgeID::source)] == (int) Forge::ModSource::lfo1 + l
                                  && picked[ForgeID::slot (s, ForgeID::dest)] == (int) Forge::ModDest::volume);

            const auto points = chops ? gateCurve (random, 4 + 2 * random.nextInt (3), true)
                                      : (l == 0 || random.nextBool() ? wobbleCurve (random) : steppedCurve (random, 4 << random.nextInt (2)));
            preset.text.emplace_back (ForgeID::lfoCurveProperty (l).toString(), curveText (points));
        }

        // Macro names from what each macro drives.
        for (int m = 0; m < Forge::numMacros; ++m)
            for (int s = 0; s < Forge::numModSlots; ++s)
                if (picked[ForgeID::slot (s, ForgeID::source)] == (int) Forge::ModSource::macro1 + m)
                {
                    const auto dest = Forge::modDestNames()[picked[ForgeID::slot (s, ForgeID::dest)]];
                    if (dest != "-")
                    {
                        preset.text.emplace_back (ForgeID::macroNameProperty (m).toString(), macroNameFor (dest));
                        break;
                    }
                }

        // Play it and measure it.
        presets.loadUnlisted (preset);
        const auto features = ForgeAudition::analyse (ForgeAudition::render (state, ForgeAudition::isMelodic (categoryName), 1));
        ++tried;
        if (! features.finite || features.rmsDb < -45.0f)
            continue;

        const auto s = model->score (features, categoryName);
        if (s < bestScore)
        {
            bestScore = s;
            best = preset;
            bestZ = z;
            bestFeatures = features;
        }
    }

    if (best.values.empty())
        return best; // nothing passed; the caller keeps the current sound

    // Level-match like the factory set.
    const auto target = ForgeAudition::isMelodic (best.category) || best.category == "FX" ? -18.0f : -15.0f;
    auto* master = state.getParameter ("master_gain");
    const auto current = master != nullptr ? master->convertFrom0to1 (master->getDefaultValue()) : -6.0f;
    best.values.emplace_back ("master_gain", juce::jlimit (-24.0f, 6.0f, current + target - bestFeatures.rmsDb));

    const auto words = wordsFor (best.category);
    best.name = juce::String (adjectives[random.nextInt ((int) std::size (adjectives))]) + " " + words[random.nextInt (words.size())];

    recent.push_back (bestZ);
    if (recent.size() > 24)
        recent.erase (recent.begin());

    lastStats = { bestScore, target, tried };
    return best;
}
