/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "value.h"

#include <string>

namespace vital {

  constexpr int kNumLfos = 8;
  constexpr int kNumOscillators = 3;
  constexpr int kNumOscillatorWaveFrames = 257;
  constexpr int kNumEnvelopes = 6;
  constexpr int kNumRandomLfos = 4;
  constexpr int kNumMacros = 4;
  constexpr int kNumFilters = 2;
  constexpr int kNumFormants = 4;
  constexpr int kNumChannels = 2;
  constexpr int kMaxPolyphony = 33;
  constexpr int kMaxActivePolyphony = 32;
  constexpr int kLfoDataResolution = 2048;
  constexpr int kMaxModulationConnections = 64;

  constexpr int kOscilloscopeMemorySampleRate = 22000;
  constexpr int kOscilloscopeMemoryResolution = 512;
  constexpr int kAudioMemorySamples = 1 << 15;
  constexpr int kDefaultWindowWidth = 1400;
  constexpr int kDefaultWindowHeight = 820;
  constexpr int kMinWindowWidth = 350;
  constexpr int kMinWindowHeight = 205;

  constexpr int kDefaultKeyboardOffset = 48;
  constexpr wchar_t kDefaultKeyboardOctaveUp = 'x';
  constexpr wchar_t kDefaultKeyboardOctaveDown = 'z';
  const std::wstring kDefaultKeyboard = L"awsedftgyhujkolp;'";

  // Version written into (and compared against) preset, wavetable and skin files.
  // This tracks the Vital file format rather than the Draco Gen release version
  // so presets stay interchangeable with Vital 1.x.
  const std::string kFileFormatVersion = "1.5.5";

  // Draco Gen file types. Files using the original Vital extensions share the
  // same format, so they are still accepted when loading.
  const std::string kPresetExtension = "draco";
  const std::string kLegacyPresetExtension = "vital";
  const std::string kPresetExtensionsList = "*." + kPresetExtension + ";*." + kLegacyPresetExtension;
  const std::string kWavetableExtension = "dracotable";
  const std::string kLegacyWavetableExtension = "vitaltable";
  const std::string kWavetableExtensionsList = "*." + kWavetableExtension + ";*." + kLegacyWavetableExtension +
                                               ";*.wav;*.flac";
  const std::string kSampleExtensionsList = "*.wav;*.flac";
  const std::string kSkinExtension = "dracoskin";
  const std::string kSkinExtensionsList = "*." + kSkinExtension + ";*.vitalskin";
  const std::string kLfoExtension = "dracolfo";
  const std::string kLfoExtensionsList = "*." + kLfoExtension + ";*.vitallfo";
  const std::string kBankExtension = "dracobank";
  const std::string kBankExtensionsList = "*." + kBankExtension + ";*.vitalbank";

  // Draco Gen v1 has no effects section. Effect parameters still exist so presets
  // load unchanged, but they are not processed, shown, automatable or modulatable.
  inline bool isEffectParameter(const std::string& name) {
    static const std::string kEffectPrefixes[] = {
      "chorus_", "compressor_", "delay_", "distortion_", "phaser_", "flanger_", "reverb_", "eq_", "filter_fx_"
    };
    for (const std::string& prefix : kEffectPrefixes) {
      if (name.compare(0, prefix.length(), prefix) == 0)
        return true;
    }
    return name == "effect_chain_order";
  }

  inline bool isWavetableFileExtension(const std::string& extension) {
    return extension == "." + kWavetableExtension || extension == "." + kLegacyWavetableExtension;
  }

  namespace constants {
    enum SourceDestination {
      kFilter1,
      kFilter2,
      kDualFilters,
      kEffects,
      kDirectOut,
      kNumSourceDestinations
    };

    static SourceDestination toggleFilter1(SourceDestination current_destination, bool on) {
      if (on) {
        if (current_destination == vital::constants::kFilter2)
          return vital::constants::kDualFilters;
        else
          return vital::constants::kFilter1;
      }
      else if (current_destination == vital::constants::kDualFilters)
        return vital::constants::kFilter2;
      else if (current_destination == vital::constants::kFilter1)
        return vital::constants::kEffects;

      return current_destination;
    }

    static SourceDestination toggleFilter2(SourceDestination current_destination, bool on) {
      if (on) {
        if (current_destination == vital::constants::kFilter1)
          return vital::constants::kDualFilters;
        else
          return vital::constants::kFilter2;
      }
      else if (current_destination == vital::constants::kDualFilters)
        return vital::constants::kFilter1;
      else if (current_destination == vital::constants::kFilter2)
        return vital::constants::kEffects;

      return current_destination;
    }

    enum Effect {
      kChorus,
      kCompressor,
      kDelay,
      kDistortion,
      kEq,
      kFilterFx,
      kFlanger,
      kPhaser,
      kReverb,
      kNumEffects
    };

    enum FilterModel {
      kAnalog,
      kDirty,
      kLadder,
      kDigital,
      kDiode,
      kFormant,
      kComb,
      kPhase,
      kNumFilterModels
    };

    enum RetriggerStyle {
      kFree,
      kRetrigger,
      kSyncToPlayHead,
      kNumRetriggerStyles,
    };

    constexpr int kNumSyncedFrequencyRatios = 13;
    constexpr vital::mono_float kSyncedFrequencyRatios[kNumSyncedFrequencyRatios] = {
      0.0f,
      1.0f / 128.0f,
      1.0f / 64.0f,
      1.0f / 32.0f,
      1.0f / 16.0f,
      1.0f / 8.0f,
      1.0f / 4.0f,
      1.0f / 2.0f,
      1.0f,
      2.0f,
      4.0f,
      8.0f,
      16.0f
    };

    const poly_float kLeftOne(1.0f, 0.0f);
    const poly_float kRightOne(0.0f, 1.0f);
    const poly_float kFirstVoiceOne(1.0f, 1.0f, 0.0f, 0.0f);
    const poly_float kSecondVoiceOne(0.0f, 0.0f, 1.0f, 1.0f);
    const poly_float kStereoSplit = kLeftOne - kRightOne;
    const poly_float kPolySqrt2 = kSqrt2;
    const poly_mask kFullMask = poly_float::equal(0.0f, 0.0f);
    const poly_mask kLeftMask = poly_float::equal(kLeftOne, 1.0f);
    const poly_mask kRightMask = poly_float::equal(kRightOne, 1.0f);
    const poly_mask kFirstMask = poly_float::equal(kFirstVoiceOne, 1.0f);
    const poly_mask kSecondMask = poly_float::equal(kSecondVoiceOne, 1.0f);

    const cr::Value kValueZero(0.0f);
    const cr::Value kValueOne(1.0f);
    const cr::Value kValueTwo(2.0f);
    const cr::Value kValueHalf(0.5f);
    const cr::Value kValueFifth(0.2f);
    const cr::Value kValueTenth(0.1f);
    const cr::Value kValuePi(kPi);
    const cr::Value kValue2Pi(2.0f * kPi);
    const cr::Value kValueSqrt2(kSqrt2);
    const cr::Value kValueNegOne(-1.0f);
  } // namespace constants
} // namespace vital
