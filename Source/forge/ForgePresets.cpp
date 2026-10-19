#include "../PresetManager.h"
#include "../Parameters.h"
#include "ForgeParameters.h"

namespace
{
    using Values = std::vector<std::pair<juce::String, float>>;
    using Forge::ModDest;
    using Forge::ModSource;
    using Forge::Warp;

    Values operator+ (Values a, const Values& b)
    {
        a.insert (a.end(), b.begin(), b.end());
        return a;
    }

    Values warp (char osc, int slot, Warp mode, float amount)
    {
        return { { ForgeID::osc (osc, slot == 0 ? ForgeID::warp1Mode : ForgeID::warp2Mode), (float) mode },
                 { ForgeID::osc (osc, slot == 0 ? ForgeID::warp1 : ForgeID::warp2), amount } };
    }

    Values mod (int slot, ModSource source, ModDest dest, float amount)
    {
        return { { ForgeID::slot (slot, ForgeID::source), (float) source },
                 { ForgeID::slot (slot, ForgeID::dest), (float) dest },
                 { ForgeID::slot (slot, ForgeID::amount), amount } };
    }

    Values hitSequence (std::initializer_list<int> rates)
    {
        Values v { { ForgeID::hitSeqOn, 1.0f }, { ForgeID::hitSeqLen, (float) rates.size() } };
        int i = 0;
        for (auto r : rates)
            v.push_back ({ ForgeID::hitRate (i++), (float) r });
        return v;
    }

    // LFO rates: 3 1/4T, 5 1/8T, 7 1/16T, 8 1/32.  Tables: 0 Sync Saw, 1 Growl FM, 3 Tearout Fold,
    // 4 Metal Screech, 5 Pulse Width, 6 Reese Stack, 7 Basic Shapes (sine to square).
    std::vector<FactoryPreset> makePresets()
    {
        return {
            { "Init", {} },

            { "Hit Seq Riddim",
              Values { { ForgeID::osc ('A', ForgeID::table), 0 }, { ForgeID::osc ('A', ForgeID::pos), 0.5f },
                       { ForgeID::filterType, 1 }, { ForgeID::cutoff, 180 }, { ForgeID::resonance, 0.45f }, { ForgeID::drive, 0.3f },
                       { ForgeID::pitchEnv, 12 }, { ForgeID::pitchDecay, 25 },
                       { ParamID::distDrive, 0.55f }, { ParamID::squash, 0.7f }, { ParamID::clip, 0.5f }, { ParamID::resample, 1 } }
                  + warp ('A', 0, Warp::sync, 0.25f) + warp ('A', 1, Warp::bendPlus, 0.3f)
                  + mod (0, ModSource::lfo1, ModDest::cutoff, 0.75f) + mod (1, ModSource::lfo1, ModDest::warpA1, 0.5f)
                  + hitSequence ({ 5, 7, 7, 5, 8, 3 }) },

            { "Window Sync Wob",
              Values { { ForgeID::osc ('A', ForgeID::table), 7 }, { ForgeID::osc ('A', ForgeID::pos), 1 },
                       { ForgeID::cutoff, 400 }, { ForgeID::resonance, 0.3f },
                       { ParamID::vowelMix, 0.4f }, { ParamID::distDrive, 0.5f }, { ParamID::squash, 0.6f }, { ParamID::clip, 0.4f } }
                  + warp ('A', 0, Warp::syncWindow, 0.1f)
                  + mod (0, ModSource::lfo1, ModDest::warpA1, 0.8f) + mod (1, ModSource::lfo1, ModDest::cutoff, 0.4f)
                  + hitSequence ({ 5, 7, 5, 3 }) },

            { "Asym Robot",
              Values { { ForgeID::osc ('A', ForgeID::table), 5 }, { ForgeID::osc ('A', ForgeID::pos), 0.3f },
                       { ForgeID::osc ('B', ForgeID::on), 1 }, { ForgeID::osc ('B', ForgeID::table), 4 }, { ForgeID::osc ('B', ForgeID::level), 0 },
                       { ForgeID::lfo (0, ForgeID::shape), 4 }, { ForgeID::lfo (1, ForgeID::shape), 3 }, { ForgeID::lfo (1, ForgeID::rate), 6 },
                       { ParamID::vowelMix, 0.7f }, { ParamID::vowel, 0.25f }, { ParamID::combMix, 0.3f },
                       { ParamID::distDrive, 0.55f }, { ParamID::squash, 0.7f }, { ParamID::clip, 0.5f }, { ParamID::resample, 1 } }
                  + warp ('A', 0, Warp::asymPlusMinus, 0.5f) + warp ('A', 1, Warp::rmOther, 0.5f)
                  + mod (0, ModSource::lfo1, ModDest::warpA1, 0.45f) + mod (1, ModSource::lfo2, ModDest::posB, 0.6f)
                  + mod (2, ModSource::lfo1, ModDest::fxVowel, 0.4f)
                  + hitSequence ({ 5, 7, 7, 5, 8, 5 }) },

            { "PWM Yoi",
              Values { { ForgeID::osc ('A', ForgeID::table), 7 }, { ForgeID::osc ('A', ForgeID::pos), 1 },
                       { ForgeID::lfo (0, ForgeID::shape), 1 },
                       { ParamID::vowelMix, 0.85f }, { ParamID::vowel, 0.3f },
                       { ParamID::distDrive, 0.5f }, { ParamID::squash, 0.6f }, { ParamID::clip, 0.45f } }
                  + warp ('A', 0, Warp::pwm, 0.2f)
                  + mod (0, ModSource::lfo1, ModDest::warpA1, 0.6f) + mod (1, ModSource::lfo1, ModDest::fxVowel, 0.7f)
                  + hitSequence ({ 5, 3, 7, 5 }) },

            { "FM Sub Growl",
              Values { { ForgeID::osc ('A', ForgeID::table), 1 }, { ForgeID::osc ('A', ForgeID::pos), 0.4f },
                       { ForgeID::cutoff, 1500 }, { ForgeID::resonance, 0.25f },
                       { ParamID::distType, 2 }, { ParamID::distDrive, 0.5f }, { ParamID::combMix, 0.35f }, { ParamID::combFeedback, 0.7f },
                       { ParamID::squash, 0.65f }, { ParamID::clip, 0.5f } }
                  + warp ('A', 0, Warp::fmSub, 0.4f) + warp ('A', 1, Warp::mirror, 0.3f)
                  + mod (0, ModSource::lfo1, ModDest::warpA1, 0.5f) + mod (1, ModSource::lfo1, ModDest::posA, 0.4f)
                  + mod (2, ModSource::env2, ModDest::cutoff, 0.3f)
                  + hitSequence ({ 5, 7, 5, 8 }) },

            { "Tearout Flip",
              Values { { ForgeID::osc ('A', ForgeID::table), 3 }, { ForgeID::osc ('A', ForgeID::pos), 0.6f },
                       { ForgeID::osc ('A', ForgeID::unison), 3 }, { ForgeID::osc ('A', ForgeID::detune), 0.15f }, { ForgeID::osc ('A', ForgeID::blend), 0.3f },
                       { ForgeID::osc ('B', ForgeID::on), 1 }, { ForgeID::osc ('B', ForgeID::table), 6 }, { ForgeID::osc ('B', ForgeID::level), 0.4f },
                       { ForgeID::osc ('B', ForgeID::octave), -1 },
                       { ForgeID::lfo (0, ForgeID::shape), 5 },
                       { ParamID::distType, 1 }, { ParamID::distDrive, 0.6f }, { ParamID::squash, 0.75f }, { ParamID::clip, 0.6f },
                       { ParamID::tilt, 0.2f }, { ParamID::resample, 1 } }
                  + warp ('A', 0, Warp::flip, 0.3f) + warp ('A', 1, Warp::bendPlusMinus, 0.7f)
                  + mod (0, ModSource::lfo1, ModDest::warpA1, 0.6f) + mod (1, ModSource::lfo1, ModDest::posA, 0.3f)
                  + hitSequence ({ 7, 5, 8, 5, 3 }) },

            { "Clean Sub",
              Values { { ForgeID::osc ('A', ForgeID::table), 7 }, { ForgeID::osc ('A', ForgeID::level), 0.3f }, { ForgeID::subLevel, 0.8f },
                       { ParamID::distDrive, 0 }, { ParamID::squash, 0.2f }, { ParamID::clip, 0.2f } } },
        };
    }
}

const std::vector<FactoryPreset>& forgeFactoryPresets()
{
    static const auto presets = makePresets();
    return presets;
}
