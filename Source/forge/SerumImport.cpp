#include "SerumImport.h"
#include "ForgeEngine.h"
#include "ForgeParameters.h"
#include "../Parameters.h"
#include "ForgeAudition.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <zstd.h>

namespace
{
    //==========================================================================
    // Container: "XferJson\0", a 64-bit little-endian header length, the JSON header, a short
    // binary prefix, then one zstd frame holding the CBOR document.
    bool decompress (const juce::MemoryBlock& file, juce::MemoryBlock& out, juce::var& header, juce::String& error)
    {
        const auto* bytes = static_cast<const uint8_t*> (file.getData());
        const auto size = file.getSize();

        if (size < 17 || std::memcmp (bytes, "XferJson", 8) != 0)
        {
            error = "This isn't a Serum 2 preset (.SerumPreset).";
            return false;
        }

        uint64_t headerLength = 0;
        for (int i = 7; i >= 0; --i)
            headerLength = (headerLength << 8) | bytes[9 + i];

        if (headerLength > 0 && 17 + headerLength <= size)
            header = juce::JSON::parse (juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes + 17), (int) headerLength));

        static constexpr uint8_t magic[] { 0x28, 0xb5, 0x2f, 0xfd };
        size_t start = (size_t) juce::jmin<uint64_t> (17 + headerLength, size);
        while (start + 4 <= size && std::memcmp (bytes + start, magic, 4) != 0)
            ++start;
        if (start + 4 > size)
        {
            error = "The preset's data block is missing.";
            return false;
        }

        std::unique_ptr<ZSTD_DStream, decltype (&ZSTD_freeDStream)> stream (ZSTD_createDStream(), &ZSTD_freeDStream);
        ZSTD_initDStream (stream.get());
        ZSTD_inBuffer input { bytes + start, size - start, 0 };
        std::vector<char> chunk (ZSTD_DStreamOutSize());

        for (;;)
        {
            ZSTD_outBuffer output { chunk.data(), chunk.size(), 0 };
            const auto result = ZSTD_decompressStream (stream.get(), &output, &input);
            if (ZSTD_isError (result))
            {
                error = juce::String ("The preset's data couldn't be unpacked (") + ZSTD_getErrorName (result) + ").";
                return false;
            }
            out.append (chunk.data(), output.pos);
            if (result == 0 || (input.pos == input.size && output.pos < output.size))
                break;
            if (out.getSize() > 256 * 1024 * 1024)
            {
                error = "The preset's data is unexpectedly large.";
                return false;
            }
        }
        return true;
    }

    //==========================================================================
    // A small CBOR reader (RFC 8949): enough for the maps, arrays, strings and numbers
    // Serum 2 writes.
    class Cbor
    {
    public:
        Cbor (const uint8_t* d, size_t n) : data (d), size (n) {}

        juce::var read (int depth = 0)
        {
            if (depth > 64 || pos >= size)
                return fail();

            const auto initial = data[pos++];
            const auto major = initial >> 5, info = initial & 31;

            if (major == 7)
                return simple (info);

            if (info == 31)
                return indefinite (major, depth);

            const auto n = argument (info);
            if (failed)
                return {};

            switch (major)
            {
                case 0: return juce::var ((juce::int64) n);
                case 1: return juce::var (-1 - (juce::int64) n);
                case 2: return take (n, true);
                case 3: return take (n, false);
                case 4:
                {
                    juce::Array<juce::var> list;
                    if (n > size - pos)
                        return fail();
                    list.ensureStorageAllocated ((int) n);
                    for (uint64_t i = 0; i < n && ! failed; ++i)
                        list.add (read (depth + 1));
                    return list;
                }
                case 5:
                {
                    auto* object = new juce::DynamicObject();
                    juce::var result (object);
                    for (uint64_t i = 0; i < n && ! failed; ++i)
                    {
                        const auto key = read (depth + 1).toString();
                        object->setProperty (key, read (depth + 1));
                    }
                    return result;
                }
                case 6: return read (depth + 1); // tag: the value follows
                default: return fail();
            }
        }

        bool ok() const { return ! failed; }

    private:
        juce::var fail() { failed = true; pos = size; return {}; }

        uint64_t argument (int info)
        {
            if (info < 24)
                return (uint64_t) info;
            const int length = info == 24 ? 1 : info == 25 ? 2 : info == 26 ? 4 : info == 27 ? 8 : 0;
            if (length == 0 || pos + (size_t) length > size)
            {
                fail();
                return 0;
            }
            uint64_t value = 0;
            for (int i = 0; i < length; ++i)
                value = (value << 8) | data[pos++];
            return value;
        }

        juce::var take (uint64_t n, bool binary)
        {
            if (n > size - pos)
                return fail();
            const auto* start = data + pos;
            pos += (size_t) n;
            if (binary)
                return juce::var (juce::MemoryBlock (start, (size_t) n));
            return juce::String::fromUTF8 (reinterpret_cast<const char*> (start), (int) n);
        }

        juce::var simple (int info)
        {
            switch (info)
            {
                case 20: return false;
                case 21: return true;
                case 22: case 23: return {};
                case 25:
                {
                    const auto bits = (uint16_t) argument (25);
                    const int exponent = (bits >> 10) & 31, mantissa = bits & 1023;
                    double value = exponent == 0 ? std::ldexp ((double) mantissa, -24)
                                 : exponent == 31 ? (mantissa == 0 ? std::numeric_limits<double>::infinity() : 0.0)
                                                  : std::ldexp ((double) (mantissa + 1024), exponent - 25);
                    return (bits & 0x8000) != 0 ? -value : value;
                }
                case 26:
                {
                    const auto bits = (uint32_t) argument (26);
                    float value;
                    std::memcpy (&value, &bits, sizeof (value));
                    return (double) value;
                }
                case 27:
                {
                    const auto bits = argument (27);
                    double value;
                    std::memcpy (&value, &bits, sizeof (value));
                    return value;
                }
                default: return info < 24 ? juce::var() : fail();
            }
        }

        juce::var indefinite (int major, int depth)
        {
            auto atBreak = [this] { return pos < size && data[pos] == 0xff; };

            if (major == 2 || major == 3)
            {
                juce::MemoryBlock joined;
                while (! failed && ! atBreak())
                {
                    const auto part = read (depth + 1);
                    if (auto* block = part.getBinaryData())
                        joined.append (block->getData(), block->getSize());
                    else
                        joined.append (part.toString().toRawUTF8(), part.toString().getNumBytesAsUTF8());
                }
                ++pos;
                return major == 2 ? juce::var (joined) : juce::var (joined.toString());
            }
            if (major == 4)
            {
                juce::Array<juce::var> list;
                while (! failed && ! atBreak())
                    list.add (read (depth + 1));
                ++pos;
                return list;
            }
            if (major == 5)
            {
                auto* object = new juce::DynamicObject();
                juce::var result (object);
                while (! failed && ! atBreak())
                {
                    const auto key = read (depth + 1).toString();
                    object->setProperty (key, read (depth + 1));
                }
                ++pos;
                return result;
            }
            return fail();
        }

        const uint8_t* data;
        size_t size, pos = 0;
        bool failed = false;
    };

    //==========================================================================
    // Reading settings. Serum leaves a setting out (or writes "default") when it is at its
    // default, so every lookup takes the value to use when it is missing.
    const juce::var& child (const juce::var& v, const char* name)
    {
        static const juce::var none;
        if (auto* object = v.getDynamicObject())
            if (object->hasProperty (name))
                return object->getProperty (name);
        return none;
    }

    double param (const juce::var& module, const char* name, double fallback)
    {
        const auto& value = child (child (module, "plainParams"), name);
        if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
            return (double) value;
        return fallback;
    }

    juce::String textParam (const juce::var& module, const char* name, const juce::String& fallback = {})
    {
        const auto& value = child (child (module, "plainParams"), name);
        return value.isString() ? value.toString() : fallback;
    }

    bool exists (const juce::var& module) { return module.getDynamicObject() != nullptr; }

    //==========================================================================
    // Value mappings. Serum's internals aren't public, so these follow its documented
    // ranges; they are approximations.
    float serumCutoffHz (double normalised) // Serum's cutoff knob: about 8 Hz to 22 kHz, log
    {
        return (float) juce::jlimit (20.0, 20000.0, 8.18 * std::pow (22000.0 / 8.18, juce::jlimit (0.0, 1.0, normalised)));
    }

    int filterType (const juce::String& serum)
    {
        const auto t = serum.toUpperCase();
        auto sign = [&t] (int plus, int minus) { return t.endsWith ("N") || t.contains ("NEG") ? minus : plus; };

        if (t.startsWith ("COMB"))                         return sign (9, 10);
        if (t.startsWith ("PHASE"))                        return sign (11, 12);
        if (t.startsWith ("FLANGE"))                       return sign (13, 14);
        if (t.contains ("FORMANT") || t.startsWith ("WSP") || t.contains ("VOWEL")) return 15;
        if (t.startsWith ("RING"))                         return 16;
        if (t.startsWith ("SNH") || t.contains ("S&H"))   return 17;
        if (t.contains ("ACID") || t.contains ("LADDER") || t.startsWith ("MG")) return 8;
        if (t.startsWith ("PEAK"))                         return 7;
        if (t.startsWith ("N") || t.startsWith ("NOTCH"))  return 6;
        if (t.startsWith ("H"))                            return t.contains ("24") ? 5 : 4;
        if (t.startsWith ("B"))                            return t.contains ("24") ? 3 : 2;
        return t.contains ("24") || t.contains ("18") ? 1 : 0;
    }

    int warpMode (const juce::String& serum)
    {
        const auto w = serum.toUpperCase().fromFirstOccurrenceOf ("K", false, false);
        if (w.isEmpty() || w == "NONE" || w == "OFF") return 0;
        if (w.startsWith ("SYNCWIN") || w.contains ("WINDOW")) return 2;
        if (w.startsWith ("SYNC"))    return 1;
        if (w == "BENDPOSNEG")        return 5;
        if (w.startsWith ("BENDPOS")) return 3;
        if (w.startsWith ("BENDNEG")) return 4;
        if (w.startsWith ("PWM"))     return 6;
        if (w == "ASYMPOSNEG")        return 9;
        if (w.startsWith ("ASYMNEG")) return 8;
        if (w.startsWith ("ASYM"))    return 7;
        if (w.startsWith ("FLIP"))    return 10;
        if (w.startsWith ("MIRROR"))  return 11;
        if (w.startsWith ("QUANT"))   return 12;
        if (w.startsWith ("PD"))      return 17;
        if (w.startsWith ("FM"))      return w.contains ("SUB") ? 16 : 13;
        if (w.startsWith ("AM"))      return 14;
        if (w.startsWith ("RM"))      return 15;
        return 0;
    }

    int distortionType (const juce::String& serum)
    {
        const auto d = serum.toUpperCase();
        if (d.contains ("HARD"))                          return 1;
        if (d.contains ("FOLD"))                          return 2;
        if (d.contains ("DOWNSAMP") || d.contains ("RATE") || d.contains ("CRUSH")) return 3;
        if (d.contains ("DIODE"))                         return 4;
        if (d.contains ("ASYM"))                          return 5;
        if (d.contains ("SIN"))                           return 6;
        if (d.contains ("ZERO") || d.contains ("XSHAPE")) return 7;
        if (d.contains ("SOFT"))                          return 0;
        return 8; // Tube, Serum's default
    }

    int nearestIndex (const double* values, int count, double target) // compared on a log scale
    {
        int best = 0;
        for (int i = 1; i < count; ++i)
            if (std::abs (std::log (values[i] / target)) < std::abs (std::log (values[best] / target)))
                best = i;
        return best;
    }

    // Serum 2 stores a tempo-synced LFO rate as a continuous number; read here as cycles per
    // bar, with its triplet and dotted switches. An assumption: the scale isn't documented.
    int syncedRateIndex (double serumRate, bool triplet, bool dotted)
    {
        auto cyclesPerBeat = juce::jmax (0.01, serumRate) / 4.0;
        if (triplet) cyclesPerBeat *= 1.5;
        if (dotted)  cyclesPerBeat *= 2.0 / 3.0;
        return nearestIndex (Forge::lfoRateCyclesPerBeat(), Forge::lfoRateNames().size(), cyclesPerBeat);
    }

    // Serum's sources, by the number a mod slot stores: 2-5 the envelopes, 6-15 the LFOs,
    // 25-32 the macros (worked out from which modules each preset uses).
    Forge::ModSource modSource (int serum)
    {
        using S = Forge::ModSource;
        switch (serum)
        {
            case 2: return S::ampEnv;
            case 3: return S::env2;
            case 4: return S::env3;
            case 6: return S::lfo1;
            case 7: return S::lfo2;
            case 8: return S::lfo3;
            case 9: return S::lfo4;
            default: break;
        }
        if (serum >= 25 && serum <= 32)
            return (S) ((int) S::macro1 + serum - 25);
        return S::none;
    }

    Forge::ModDest modDest (const juce::String& module, int id, const juce::String& name)
    {
        using D = Forge::ModDest;
        auto osc = [id] (D a, D b, D c) { return id == 0 ? a : id == 1 ? b : id == 2 ? c : D::none; };
        auto flt = [id] (D one, D two) { return id == 0 ? one : id == 1 ? two : D::none; };

        if (module == "Oscillator")
        {
            if (name == "kParamVolume")
                return id == 3 ? D::noiseLevel : id == 4 ? D::subLevel : osc (D::levelA, D::levelB, D::levelC);
            if (name == "kParamPan")    return osc (D::panA, D::panB, D::panC);
            if (name == "kParamDetune") return osc (D::detuneA, D::detuneB, D::detuneC);
            if (name == "kParamFine" || name == "kParamPitch" || name == "kParamCoarsePit" || name == "kParamOctave")
                return osc (D::pitchA, D::pitchB, D::pitchC);
            if (name == "kParamColor" && id == 3) return D::noiseColour;
        }
        if (module == "NoiseOsc" && name == "kParamColor") return D::noiseColour;
        if (module == "WTOsc")
        {
            if (name == "kParamTablePos") return osc (D::posA, D::posB, D::posC);
            if (name == "kParamWarp")     return osc (D::warpA1, D::warpB1, D::warpC1);
            if (name == "kParamWarp2")    return osc (D::warpA2, D::warpB2, D::warpC2);
        }
        if (module == "VoiceFilter")
        {
            if (name == "kParamFreq")  return flt (D::cutoff, D::cutoff2);
            if (name == "kParamReso")  return flt (D::resonance, D::resonance2);
            if (name == "kParamDrive") return flt (D::drive, D::drive2);
            if (name == "kParamVar")   return flt (D::filterVar, D::filterVar2);
            if (name == "kParamWet")   return flt (D::filterMix, D::filterMix2);
        }
        if (module == "LFO" && name == "kParamRate" && id >= 0 && id < 4)
            return (D) ((int) D::lfo1Rate + id);
        if (module == "Global")
        {
            if (name == "kParamMasterTuning") return D::masterPitch;
            if (name == "kParamMasterVolume") return D::volume;
        }
        if (module == "FXFilter")
        {
            if (name == "kParamFreq") return D::fxFilterCutoff;
            if (name == "kParamReso") return D::fxFilterRes;
            if (name == "kParamWet")  return D::fxFilterMix;
        }
        if (module == "FXDistortion")
        {
            if (name == "kParamDrive") return D::distDrive;
            if (name == "kParamWet")   return D::distMix;
        }
        if (module == "FXBode")
        {
            if (name == "kParamShift") return D::bodeShift;
            if (name == "kParamWet")   return D::bodeMix;
        }
        if (module == "FXEQ")
        {
            if (name == "kParamGain1") return D::eqGain1;
            if (name == "kParamGain2") return D::eqGain2;
            if (name == "kParamFreq1") return D::eqFreq1;
            if (name == "kParamFreq2") return D::eqFreq2;
        }
        if (module == "FXHyperD")
        {
            if (name == "kParamWet")     return D::hyperMix;
            if (name == "kParamDimEWet") return D::dimensionMix;
        }
        if (module == "FXComp" && name == "kParamWet") return D::smash;
        if (name == "kParamWet")
        {
            if (module == "FXChorus")  return D::chorusMix;
            if (module == "FXPhaser")  return D::phaserMix;
            if (module == "FXFlanger") return D::flangerMix;
            if (module == "FXDelay")   return D::delayMix;
            if (module == "FXReverb")  return D::reverbMix;
        }
        return D::none;
    }

    //==========================================================================
    struct Builder
    {
        Builder (SerumImport::Result& r) : result (r) {}

        void set (const juce::String& id, double value) { result.preset.values.push_back ({ id, (float) value }); }
        void text (const juce::String& key, const juce::String& value) { result.preset.text.push_back ({ key, value }); }
        void note (const juce::String& line) { result.notes.addIfNotAlreadyThere (line); }

        SerumImport::Result& result;
    };

    juce::String oscId (int osc, const char* name) { return ForgeID::osc (ForgeID::oscLetter (osc), name); }

    // Writes a table embedded in the preset (one 2048-sample frame after another) as a WAV.
    juce::File writeEmbeddedTable (const juce::var& samples, const juce::File& file)
    {
        std::vector<float> audio;
        if (auto* list = samples.getArray())
        {
            audio.reserve ((size_t) list->size());
            for (const auto& s : *list)
                audio.push_back ((float) (double) s);
        }
        else if (auto* block = samples.getBinaryData())
        {
            audio.resize (block->getSize() / sizeof (float));
            std::memcpy (audio.data(), block->getData(), audio.size() * sizeof (float));
        }

        constexpr int frame = 2048;
        audio.resize ((audio.size() / frame) * frame);
        if (audio.empty())
            return {};

        file.getParentDirectory().createDirectory();
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return {};

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (32);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return {};

        const float* channels[] { audio.data() };
        writer->writeFromFloatArrays (channels, 1, (int) audio.size());
        return file;
    }

    juce::File findTable (const juce::String& relative, const juce::Array<juce::File>& roots)
    {
        const auto cleaned = relative.replaceCharacter ('\\', '/').trimCharactersAtStart ("/");
        if (cleaned.isEmpty())
            return {};

        for (const auto& root : roots)
        {
            const auto direct = root.getChildFile (cleaned);
            if (direct.existsAsFile())
                return direct;
        }

        // Same name anywhere under the table folders (Serum 1 tables live one level up).
        const auto name = cleaned.fromLastOccurrenceOf ("/", false, false);
        for (const auto& root : roots)
            if (root.isDirectory())
                for (const auto& entry : juce::RangedDirectoryIterator (root, true, name, juce::File::findFiles))
                    return entry.getFile();
        return {};
    }

    void convertOscillator (Builder& b, const juce::var& doc, int index, const juce::File& tableFolder,
                            const juce::Array<juce::File>& roots, const juce::String& presetName)
    {
        const auto& module = child (doc, ("Oscillator" + juce::String (index)).toRawUTF8());
        const auto on = param (module, "kParamEnable", index == 0 ? 1.0 : 0.0) > 0.5;
        b.set (oscId (index, ForgeID::on), on ? 1.0 : 0.0);
        if (! on)
            return;

        const auto type = textParam (module, "kParamType", "kOsc_WT");
        if (! type.containsIgnoreCase ("WT"))
            b.note ("Oscillator " + juce::String::charToString (ForgeID::oscLetter (index)) + " is a "
                    + type.fromFirstOccurrenceOf ("kOsc_", false, false) + " oscillator in Serum; it plays a wavetable here.");

        b.set (oscId (index, ForgeID::level), juce::jlimit (0.0, 1.0, param (module, "kParamVolume", 0.75)));
        b.set (oscId (index, ForgeID::pan), juce::jlimit (-1.0, 1.0, param (module, "kParamPan", 0.0) / 50.0));
        b.set (oscId (index, ForgeID::octave), juce::jlimit (-4.0, 4.0, param (module, "kParamOctave", 0.0)));
        b.set (oscId (index, ForgeID::semi), juce::jlimit (-24.0, 24.0, std::round (param (module, "kParamPitch", 0.0) + param (module, "kParamCoarsePit", 0.0))));
        b.set (oscId (index, ForgeID::fine), juce::jlimit (-100.0, 100.0, param (module, "kParamFine", 0.0)));

        const auto unison = (int) std::round (param (module, "kParamUnison", 1.0));
        if (unison > Forge::maxUnison)
            b.note ("Unison above " + juce::String (Forge::maxUnison) + " voices is capped.");
        b.set (oscId (index, ForgeID::unison), juce::jlimit (1, Forge::maxUnison, unison));
        b.set (oscId (index, ForgeID::detune), juce::jlimit (0.0, 1.0, param (module, "kParamDetune", 0.25)));

        const auto& wt = child (module, ("WTOsc" + juce::String (index)).toRawUTF8());
        b.set (oscId (index, ForgeID::randPhase), juce::jlimit (0.0, 1.0, param (wt, "kParamRandomPhase", 100.0) / 100.0));
        b.set (oscId (index, ForgeID::warp1Mode), warpMode (textParam (wt, "kParamWarpMenu")));
        b.set (oscId (index, ForgeID::warp1), juce::jlimit (0.0, 1.0, param (wt, "kParamWarp", 0.0)));
        b.set (oscId (index, ForgeID::warp2Mode), warpMode (textParam (wt, "kParamWarpMenu2")));
        b.set (oscId (index, ForgeID::warp2), juce::jlimit (0.0, 1.0, param (wt, "kParamWarp2", 0.0)));

        // The wavetable: embedded in the preset, or a file in the user's Serum tables.
        const auto frames = juce::jmax (1, (int) ((double) child (wt, "numFrames") / 2048.0));
        const auto position = param (wt, "kParamTablePos", 1.0);
        b.set (oscId (index, ForgeID::pos), frames > 1 ? juce::jlimit (0.0, 1.0, (position - 1.0) / (frames - 1)) : 0.0);

        juce::File table;
        const auto& embedded = child (wt, "embeddedWTData");
        const auto relative = child (wt, "relativePathToWT").toString();
        if (! embedded.isVoid())
            table = writeEmbeddedTable (embedded, tableFolder.getChildFile (juce::File::createLegalFileName (presetName) + " "
                                                                            + juce::String::charToString (ForgeID::oscLetter (index)) + ".wav"));
        else if (relative.isNotEmpty())
            table = findTable (relative, roots);

        if (table.existsAsFile())
        {
            b.text ("importedTable" + juce::String::charToString (ForgeID::oscLetter (index)), table.getFullPathName());
            b.set (oscId (index, ForgeID::table), Forge::importedTable());
        }
        else if (relative.isNotEmpty())
            b.note ("Couldn't find the wavetable \"" + relative + "\" in your Serum tables; a built-in one plays instead.");
    }

    void convertRoutes (Builder& b, const juce::var& doc, bool filter2On)
    {
        auto route = [&] (int slot) -> int
        {
            const auto& module = child (doc, ("RoutingSlot" + juce::String (slot)).toRawUTF8());
            const auto dest = textParam (module, "kParamRoutingDest", "kRoutingDestFilter");
            if (! dest.containsIgnoreCase ("Filter"))
                return (int) Forge::Route::direct;
            const auto balance = param (module, "kParamFilterBalance", filter2On ? 50.0 : 0.0);
            return (int) (balance <= 10.0 ? Forge::Route::filter1 : balance >= 90.0 ? Forge::Route::filter2 : Forge::Route::both);
        };

        for (int o = 0; o < Forge::numOscs; ++o)
            b.set (oscId (o, ForgeID::route), route (o));
        b.set (ForgeID::noiseRoute, route (3));
        b.set (ForgeID::subRoute, route (4));
    }

    void convertSubAndNoise (Builder& b, const juce::var& doc)
    {
        const auto& noise = child (doc, "Oscillator3");
        const auto noiseOn = param (noise, "kParamEnable", 0.0) > 0.5;
        b.set (ForgeID::noise, noiseOn ? juce::jlimit (0.0, 1.0, param (noise, "kParamVolume", 0.75)) : 0.0);
        if (noiseOn)
        {
            const auto& inner = child (noise, "NoiseOsc3");
            b.set (ForgeID::noiseColour, juce::jlimit (0.0, 1.0, param (inner, "kParamColor", 0.5)));
            if (child (inner, "relativePathToNoiseSample").toString().isNotEmpty())
                b.note ("Serum's noise samples aren't loaded; Forge's own noise plays instead.");
        }

        const auto& sub = child (doc, "Oscillator4");
        const auto subOn = param (sub, "kParamEnable", 0.0) > 0.5;
        b.set (ForgeID::subOn, subOn ? 1.0 : 0.0);
        if (subOn)
        {
            b.set (ForgeID::subLevel, juce::jlimit (0.0, 1.0, param (sub, "kParamVolume", 0.75)));
            // Forge's sub follows the played note; Serum's has its own octave like an oscillator.
            b.set (ForgeID::subOctave, juce::jlimit (0, 2, (int) -std::round (param (sub, "kParamOctave", 0.0))));
            const auto shape = textParam (child (sub, "SubOsc4"), "kParamShape", "kSine");
            b.set (ForgeID::subShape, shape.containsIgnoreCase ("Tri") ? 1 : shape.containsIgnoreCase ("Saw") ? 2
                                    : shape.containsIgnoreCase ("Square") || shape.containsIgnoreCase ("Rect") ? 3 : 0);
        }
    }

    bool convertFilters (Builder& b, const juce::var& doc)
    {
        bool secondOn = false;
        for (int f = 0; f < 2; ++f)
        {
            const auto& module = child (doc, ("VoiceFilter" + juce::String (f)).toRawUTF8());
            const auto on = param (module, "kParamEnable", 0.0) > 0.5;
            auto id = [f] (const char* name) { return ForgeID::filter (f, name); };
            b.set (id (ForgeID::on), on ? 1.0 : 0.0);
            if (f == 1)
                secondOn = on;
            if (! on)
                continue;

            b.set (id (ForgeID::type), filterType (textParam (module, "kParamType", "L12")));
            b.set (id (ForgeID::cutoffName), serumCutoffHz (param (module, "kParamFreq", 0.58)));
            b.set (id (ForgeID::res), juce::jlimit (0.0, 1.0, param (module, "kParamReso", 10.0) / 100.0));
            b.set (id (ForgeID::driveName), juce::jlimit (0.0, 1.0, param (module, "kParamDrive", 0.0) / 100.0));
            b.set (id (ForgeID::var), juce::jlimit (0.0, 1.0, param (module, "kParamVar", 0.0) / 100.0));
            b.set (id (ForgeID::mix), juce::jlimit (0.0, 1.0, param (module, "kParamWet", 100.0) / 100.0));
            b.set (id (ForgeID::keytrack), param (module, "kParamKeyTrack", 0.0) > 0.5 ? 1.0 : 0.0);
        }
        return secondOn;
    }

    void convertEnvelopes (Builder& b, const juce::var& doc)
    {
        for (int e = 0; e < 3; ++e)
        {
            const auto& module = child (doc, ("Env" + juce::String (e)).toRawUTF8());
            const auto attack  = juce::jlimit (0.5, 3000.0, param (module, "kParamAttack", 0.0005) * 1000.0);
            const auto decay   = juce::jlimit (5.0, 3000.0, (param (module, "kParamHold", 0.0) + param (module, "kParamDecay", 1.0)) * 1000.0);
            const auto sustain = juce::jlimit (0.0, 1.0, param (module, "kParamSustain", 1.0));
            const auto release = juce::jlimit (5.0, 5000.0, param (module, "kParamRelease", 0.015) * 1000.0);

            if (e == 0)
            {
                b.set (ForgeID::attack, attack);
                b.set (ForgeID::decay, decay);
                b.set (ForgeID::sustain, sustain);
                b.set (ForgeID::release, release);
            }
            else
            {
                b.set (ForgeID::env (e - 1, ForgeID::envAttack), attack);
                b.set (ForgeID::env (e - 1, ForgeID::envDecay), decay);
                b.set (ForgeID::env (e - 1, ForgeID::envSustain), sustain);
                b.set (ForgeID::env (e - 1, ForgeID::envRelease), juce::jmin (release, 5000.0));
            }
        }
    }

    juce::String curveText (const juce::var& curveData)
    {
        const auto* xs = child (curveData, "xVals").getArray();
        const auto* ys = child (curveData, "yVals").getArray();
        const auto* cs = child (curveData, "curveVals").getArray();
        if (xs == nullptr || ys == nullptr || xs->size() < 2 || ys->size() < xs->size())
            return {};

        juce::StringArray points;
        for (int i = 0; i < xs->size(); ++i)
        {
            // Serum draws y downwards (0 is the top), and bends a segment with 0.5 as straight.
            const auto x = juce::jlimit (0.0, 1.0, (double) (*xs)[i]);
            const auto y = 1.0 - juce::jlimit (0.0, 1.0, (double) (*ys)[i]);
            const auto c = cs != nullptr && i < cs->size() ? juce::jlimit (-1.0, 1.0, ((double) (*cs)[i] - 0.5) * 2.0) : 0.0;
            points.add (juce::String (x, 4) + "," + juce::String (y, 4) + "," + juce::String (c, 3));
        }
        return points.joinIntoString (";");
    }

    void convertLfos (Builder& b, const juce::var& doc)
    {
        for (int l = 0; l < Forge::numLfos; ++l)
        {
            const auto& module = child (doc, ("LFO" + juce::String (l)).toRawUTF8());
            auto id = [l] (const char* name) { return ForgeID::lfo (l, name); };

            const auto curve = curveText (child (module, "curveData"));
            if (curve.isNotEmpty())
            {
                b.set (id (ForgeID::shape), Forge::lfoShapeNames().size() - 1); // Custom
                b.text (ForgeID::lfoCurveProperty (l).toString(), curve);
            }
            else
            {
                b.set (id (ForgeID::shape), 1); // Triangle, Serum's starting shape
            }

            const auto sync = param (module, "kParamBeatSync", 1.0) > 0.5;
            const auto rate = param (module, "kParamRate", 4.0);
            b.set (id (ForgeID::sync), sync ? 1.0 : 0.0);
            if (sync)
                b.set (id (ForgeID::rate), syncedRateIndex (rate, param (module, "kParamTriplets", 0.0) > 0.5, param (module, "kParamDotted", 0.0) > 0.5));
            else
                b.set (id (ForgeID::hz), juce::jlimit (0.01, 40.0, rate * (param (module, "kParamRate10x", 0.0) > 0.5 ? 10.0 : 1.0)));

            const auto mode = textParam (module, "kParamMode", "Trig");
            b.set (id (ForgeID::mode), mode.containsIgnoreCase ("Env") ? 1 : mode.containsIgnoreCase ("Free") ? 2 : 0);
            b.set (id (ForgeID::smooth), juce::jlimit (0.0, 1.0, param (module, "kParamSmooth", 0.0) / 100.0));

            if (textParam (module, "kParamType").isNotEmpty())
                b.note ("LFO " + juce::String (l + 1) + " is a chaos LFO in Serum; it plays its drawn shape here.");
        }
    }

    void convertMacros (Builder& b, const juce::var& doc)
    {
        for (int m = 0; m < 8; ++m)
        {
            const auto& module = child (doc, ("Macro" + juce::String (m)).toRawUTF8());
            b.set (ForgeID::macro (m), juce::jlimit (0.0, 1.0, param (module, "kParamValue", 0.0) / 100.0));
            const auto name = child (module, "name").toString();
            if (name.isNotEmpty())
                b.text (ForgeID::macroNameProperty (m).toString(), name);
        }
    }

    void convertModulation (Builder& b, const juce::var& doc)
    {
        int slot = 0, skipped = 0, total = 0;
        for (int s = 0; s < 64; ++s)
        {
            const auto& module = child (doc, ("ModSlot" + juce::String (s)).toRawUTF8());
            if (! exists (module))
                continue;
            ++total;

            const auto* source = child (module, "source").getArray();
            const auto serumSource = source != nullptr && source->size() > 0 ? (int) (*source)[0] : 0;
            if (serumSource == 0)
            {
                --total; // an empty slot
                continue;
            }
            if (serumSource == 5)
                b.note ("Serum's fourth envelope isn't carried over.");
            if (serumSource >= 10 && serumSource <= 15)
                b.note ("Only Serum's first four LFOs are carried over.");
            const auto src = modSource (serumSource);
            const auto aux = modSource (source != nullptr && source->size() > 1 ? (int) (*source)[1] : 0);
            const auto dst = modDest (child (module, "destModuleTypeString").toString(), (int) child (module, "destModuleID"),
                                      child (module, "destModuleParamName").toString());

            if (src == Forge::ModSource::none || dst == Forge::ModDest::none || slot >= 16 || param (module, "kParamBypass", 0.0) > 0.5)
            {
                ++skipped;
                continue;
            }

            auto id = [slot] (const char* name) { return ForgeID::slot (slot, name); };
            b.set (id (ForgeID::source), (int) src);
            b.set (id (ForgeID::aux), (int) aux);
            b.set (id (ForgeID::dest), (int) dst);
            b.set (id (ForgeID::amount), juce::jlimit (-1.0, 1.0, param (module, "kParamAmount", 0.0) / 100.0));
            b.set (id (ForgeID::bipolar), param (module, "kParamBipolar", 0.0) > 0.5 ? 1.0 : 0.0);
            ++slot;
        }

        for (; slot < 16; ++slot)
            b.set (ForgeID::slot (slot, ForgeID::source), 0);

        if (skipped > 0)
            b.note (juce::String (skipped) + " of " + juce::String (total)
                    + " modulation routings have no Forge equivalent (or didn't fit in Forge's 16 slots) and were left out.");
    }

    void convertEffects (Builder& b, const juce::var& doc)
    {
        const auto* fxList = child (child (doc, "FXRack0"), "FX").getArray();
        if (fxList == nullptr)
            return;

        auto pct = [] (const juce::var& m, const char* name, double fallback) { return juce::jlimit (0.0, 1.0, param (m, name, fallback) / 100.0); };

        for (const auto& fx : *fxList)
        {
            const auto type = (int) child (fx, "type");
            static const char* keys[] { "FXDistortion", "FXFlanger", "FXPhaser", "FXChorus", "FXDelay", "FXComp", "FXReverb",
                                        "FXEQ", "FXFilter", "FXHyperD", "FXBode", "FXConv", "FXUtils" };
            if (type < 0 || type >= (int) std::size (keys))
                continue;
            const auto& m = child (fx, keys[type]);
            if (param (m, "kParamEnable", 1.0) < 0.5)
                continue;

            switch (type)
            {
                case 0:
                    b.set (ParamID::distType, distortionType (textParam (m, "kParamMode", "kTube")));
                    b.set (ParamID::distDrive, pct (m, "kParamDrive", 25.0));
                    b.set (ParamID::distMix, pct (m, "kParamWet", 100.0));
                    break;
                case 1:
                    b.set (ForgeID::flangerOn, 1);
                    b.set (ForgeID::flangerRate, juce::jlimit (0.0, 1.0, param (m, "kParamRate", 1.0) / 15.0));
                    b.set (ForgeID::flangerDepth, pct (m, "kParamDepth", 50.0));
                    b.set (ForgeID::flangerFeedback, pct (m, "kParamFeedback", 50.0));
                    b.set (ForgeID::flangerMix, pct (m, "kParamWet", 50.0));
                    break;
                case 2:
                    b.set (ForgeID::phaserOn, 1);
                    b.set (ForgeID::phaserRate, juce::jlimit (0.0, 1.0, param (m, "kParamRate", 1.0) / 15.0));
                    b.set (ForgeID::phaserDepth, pct (m, "kParamDepth", 50.0));
                    b.set (ForgeID::phaserFreq, juce::jlimit (40.0, 12000.0, param (m, "kParamFreq", 800.0)));
                    b.set (ForgeID::phaserFeedback, pct (m, "kParamFeedback", 50.0));
                    b.set (ForgeID::phaserMix, pct (m, "kParamWet", 50.0));
                    break;
                case 3:
                    b.set (ForgeID::chorusOn, 1);
                    b.set (ForgeID::chorusRate, juce::jlimit (0.0, 1.0, param (m, "kParamRate", 1.0) / 10.0));
                    b.set (ForgeID::chorusDepth, pct (m, "kParamDepth", 30.0));
                    b.set (ForgeID::chorusDelay, juce::jlimit (1.0, 30.0, param (m, "kParamDelay", 8.0)));
                    b.set (ForgeID::chorusFeedback, pct (m, "kParamFeedback", 0.0));
                    b.set (ForgeID::chorusMix, pct (m, "kParamWet", 50.0));
                    break;
                case 4:
                {
                    // Delay times are read as seconds and fitted to Forge's note lengths at 150 BPM.
                    b.set (ForgeID::delayOn, 1);
                    const auto beats = juce::jmax (0.01, param (m, "kParamTimeL", 0.2) * 150.0 / 60.0);
                    b.set (ForgeID::delayTime, nearestIndex (forgeDelayTimeBeats(), forgeDelayTimeNames().size(), beats));
                    b.set (ForgeID::delayFeedback, juce::jlimit (0.0, 0.95, param (m, "kParamFeedback", 40.0) / 100.0));
                    b.set (ForgeID::delayMix, pct (m, "kParamWet", 25.0));
                    b.set (ForgeID::delayPingPong, param (m, "kParamMode", 0.0) > 0.5 ? 1.0 : 0.0);
                    break;
                }
                case 5:
                    b.set (ParamID::squash, 0.6 * pct (m, "kParamWet", 100.0));
                    break;
                case 6:
                    b.set (ForgeID::reverbOn, 1);
                    b.set (ForgeID::reverbSize, pct (m, "kParamSize", 50.0));
                    b.set (ForgeID::reverbWidth, pct (m, "kParamWidth", 100.0));
                    b.set (ForgeID::reverbMix, pct (m, "kParamWet", 25.0));
                    break;
                case 7:
                    b.set (ForgeID::eqOn, 1);
                    b.set (ForgeID::eq1Freq, juce::jlimit (20.0, 20000.0, param (m, "kParamFreq1", 200.0)));
                    b.set (ForgeID::eq1Gain, juce::jlimit (-24.0, 24.0, param (m, "kParamGain1", 0.0)));
                    b.set (ForgeID::eq1Q, 0.5 + 9.5 * pct (m, "kParamReso1", 30.0));
                    b.set (ForgeID::eq1Shape, param (m, "kParamType1", 0.0) < 0.5 ? 0 : 1);
                    b.set (ForgeID::eq2Freq, juce::jlimit (20.0, 20000.0, param (m, "kParamFreq2", 4000.0)));
                    b.set (ForgeID::eq2Gain, juce::jlimit (-24.0, 24.0, param (m, "kParamGain2", 0.0)));
                    b.set (ForgeID::eq2Q, 0.5 + 9.5 * pct (m, "kParamReso2", 30.0));
                    b.set (ForgeID::eq2Shape, param (m, "kParamType2", 0.0) < 0.5 ? 2 : 1);
                    break;
                case 8:
                    b.set (ForgeID::fxfOn, 1);
                    b.set (ForgeID::fxfType, filterType (textParam (m, "kParamType", "L12")));
                    b.set (ForgeID::fxfCutoff, serumCutoffHz (param (m, "kParamFreq", 0.58)));
                    b.set (ForgeID::fxfRes, pct (m, "kParamReso", 10.0));
                    b.set (ForgeID::fxfDrive, pct (m, "kParamDrive", 0.0));
                    b.set (ForgeID::fxfVar, pct (m, "kParamVar", 0.0));
                    b.set (ForgeID::fxfMix, pct (m, "kParamWet", 100.0));
                    break;
                case 9:
                    b.set (ForgeID::hyperOn, 1);
                    b.set (ForgeID::hyperMix, pct (m, "kParamWet", 50.0));
                    b.set (ForgeID::hyperDetune, pct (m, "kParamDetune", 25.0));
                    b.set (ForgeID::hyperRate, pct (m, "kParamRate", 40.0));
                    b.set (ForgeID::hyperUnison, juce::jlimit (1, 8, (int) std::round (param (m, "kParamUnison", 3.0)) + 1));
                    b.set (ForgeID::dimSize, pct (m, "kParamDimESize", 50.0));
                    b.set (ForgeID::dimMix, pct (m, "kParamDimEWet", 0.0));
                    break;
                case 10:
                    b.set (ForgeID::bodeOn, 1);
                    b.set (ForgeID::bodeShift, juce::jlimit (-1.0, 1.0, param (m, "kParamShift", 10.0) / 100.0));
                    b.set (ForgeID::bodeFeedback, pct (m, "kParamFeedback", 0.0));
                    b.set (ForgeID::bodeMix, pct (m, "kParamWet", 50.0));
                    break;
                default:
                    if (type == 11)
                        b.note ("Serum's convolution effect isn't carried over.");
                    break;
            }
        }

        for (const char* bus : { "FXRack1", "FXRack2" })
            if (const auto* list = child (child (doc, bus), "FX").getArray(); list != nullptr && ! list->isEmpty())
                b.note ("Serum's send-bus effects aren't carried over.");
    }

    void convertVoicing (Builder& b, const juce::var& doc)
    {
        const auto& global = child (doc, "Global0");
        b.set (ForgeID::mono, param (global, "kParamMonoToggle", 0.0) > 0.5 ? 1.0 : 0.0);
        b.set (ForgeID::legato, param (global, "kParamLegato", 0.0) > 0.5 ? 1.0 : 0.0);
        b.set (ForgeID::glide, juce::jlimit (0.0, 1000.0, param (global, "kParamPortamentoTime", 0.0) * 1000.0));
        b.set (ForgeID::bendRange, juce::jlimit (1.0, 24.0, std::abs (param (global, "kParamBendRangeUp", 2.0))));
        b.set (ForgeID::polyphony, juce::jlimit (1.0, (double) ForgeEngine::maxVoices, param (global, "kParamPolyCount", 8.0)));
    }
}

//==============================================================================
juce::var SerumImport::readDocument (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock raw, unpacked;
    if (! file.loadFileAsData (raw))
    {
        error = "Couldn't read " + file.getFileName() + ".";
        return {};
    }

    juce::var header;
    if (! decompress (raw, unpacked, header, error))
        return {};

    Cbor reader (static_cast<const uint8_t*> (unpacked.getData()), unpacked.getSize());
    auto document = reader.read();
    if (! reader.ok() || document.getDynamicObject() == nullptr)
    {
        error = "The preset's settings couldn't be read.";
        return {};
    }
    return document;
}

juce::Array<juce::File> SerumImport::tableSearchFolders (const juce::File& serumPreset)
{
    juce::Array<juce::File> roots;
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    for (const auto* path : { "Xfer/Serum 2 Presets/Tables", "Xfer/Serum 2 Presets", "Xfer/Serum Presets/Tables" })
        if (documents.getChildFile (path).isDirectory())
            roots.add (documents.getChildFile (path));

    // A preset kept inside a Serum presets folder: look for that folder's Tables.
    for (auto dir = serumPreset.getParentDirectory(); dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
        if (dir.getChildFile ("Tables").isDirectory())
            roots.addIfNotAlreadyThere (dir.getChildFile ("Tables"));
    return roots;
}

bool SerumImport::convert (const juce::File& serumPreset, const juce::File& tableFolder, Result& result, juce::String& error)
{
    const auto doc = readDocument (serumPreset, error);
    if (doc.isVoid())
        return false;

    if (! exists (child (doc, "Oscillator0")) && ! exists (child (doc, "Global0")))
    {
        error = "This Serum 2 file doesn't look like a synth preset.";
        return false;
    }

    result = {};
    auto name = child (doc, "presetName").toString();
    if (name.isEmpty())
        name = serumPreset.getFileNameWithoutExtension();
    result.preset.name = name;
    result.preset.category = "Imported";

    Builder b (result);
    // Start from a clean voice: Forge's legacy chain stays out of the way unless the preset uses it.
    b.set (ParamID::distDrive, 0.0);
    b.set (ParamID::squash, 0.0);
    b.set (ParamID::clip, 0.15);
    b.set (ParamID::gate, 1.0);
    b.set (ParamID::master, -9.0);
    b.set (ForgeID::hitSeqOn, 0.0);
    b.set (ForgeID::filtersParallel, 0.0);

    const auto roots = tableSearchFolders (serumPreset);
    for (int o = 0; o < Forge::numOscs; ++o)
        convertOscillator (b, doc, o, tableFolder, roots, name);
    convertSubAndNoise (b, doc);
    convertRoutes (b, doc, convertFilters (b, doc));
    convertEnvelopes (b, doc);
    convertLfos (b, doc);
    convertMacros (b, doc);
    convertModulation (b, doc);
    convertEffects (b, doc);
    convertVoicing (b, doc);

    // Later values win: keep the last one written for each parameter.
    std::map<juce::String, float> latest;
    for (const auto& [id, value] : result.preset.values)
        latest[id] = value;
    result.preset.values.assign (latest.begin(), latest.end());

    levelMatch (result);
    return true;
}

void SerumImport::levelMatch (Result& result)
{
    // Plays the sound once offline and sets the output so it sits with the factory presets.
    constexpr float targetDb = -15.0f;
    ForgeAudition::Host host;
    std::vector<FactoryPreset> none;
    PresetManager manager (host.parameters, none, "Serum Import");
    manager.loadUnlisted (result.preset);

    const auto features = ForgeAudition::analyse (ForgeAudition::render (host.parameters, false, 1));
    if (! features.finite)
    {
        result.notes.add ("The imported sound became unstable when played; check the filter and feedback settings.");
        return;
    }
    if (features.rmsDb < -60.0f)
    {
        result.notes.add ("The imported sound is nearly silent when played as a bass line; it may need a macro or mod wheel turned up.");
        return;
    }

    auto& values = result.preset.values;
    auto master = std::find_if (values.begin(), values.end(), [] (const auto& v) { return v.first == ParamID::master; });
    const auto current = master != values.end() ? master->second : -6.0f;
    const auto levelled = juce::jlimit (-24.0f, 6.0f, current + targetDb - features.rmsDb);
    if (master != values.end())
        master->second = levelled;
    else
        values.emplace_back (ParamID::master, levelled);
}
