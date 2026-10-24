/* Copyright 2026 Draco Gen contributors
 *
 * Draco Gen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Draco Gen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Draco Gen.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "wavetable_factory.h"

#include "JuceHeader.h"
#include "generator_random.h"
#include "synth_constants.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace draco {

  namespace {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    constexpr int kSize = WavetableFactory::kFrameSize;

    // WavetableComponent::InterpolationStyle and WaveSource::InterpolationMode values.
    constexpr int kInterpolationStyleLinear = 1;
    constexpr int kInterpolationStyleCubic = 2;
    constexpr int kInterpolationModeTime = 0;
    constexpr int kInterpolationModeFrequency = 1;

    struct Family {
      std::string name;
      std::string display_name;
      int num_keyframes;
      int interpolation_mode;
      std::function<void(std::vector<double>&, double t, GeneratorRandom& random)> render;
    };

    double saw(double phase) {
      return 2.0 * (phase - std::floor(phase)) - 1.0;
    }

    double gaussian(double x, double center, double width) {
      double d = (x - center) / width;
      return std::exp(-0.5 * d * d);
    }

    // Additive synthesis helper: amplitudes[k] is the level of harmonic k + 1.
    void additive(std::vector<double>& out, const std::vector<double>& amplitudes,
                  const std::vector<double>* phases = nullptr) {
      std::fill(out.begin(), out.end(), 0.0);
      for (size_t k = 0; k < amplitudes.size(); ++k) {
        double amplitude = amplitudes[k];
        if (std::fabs(amplitude) < 1e-5)
          continue;
        double harmonic = k + 1.0;
        double offset = phases ? (*phases)[k] : 0.0;
        for (int i = 0; i < kSize; ++i)
          out[i] += amplitude * std::sin(kTwoPi * harmonic * i / kSize + offset);
      }
    }

    // Formant frequencies (Hz) for the vowels used by the growl tables.
    struct Vowel { double f1, f2, f3; };
    const Vowel kVowels[] = {
      { 800.0, 1150.0, 2900.0 }, // a
      { 400.0, 1600.0, 2700.0 }, // e
      { 350.0, 1700.0, 2700.0 }, // i
      { 450.0, 800.0, 2830.0 },  // o
      { 325.0, 700.0, 2530.0 },  // u
    };
    constexpr int kNumVowels = sizeof(kVowels) / sizeof(Vowel);

    const std::vector<Family>& families() {
      static const std::vector<Family> kFamilies = {
        { "dragon_saw", "Dragon Saw", 6, kInterpolationModeFrequency,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double center = 3.0 * std::pow(14.0, t);
            double boost = 2.0 + 4.0 * random.unit();
            std::vector<double> amplitudes(160);
            for (size_t k = 0; k < amplitudes.size(); ++k) {
              double harmonic = k + 1.0;
              amplitudes[k] = (1.0 / harmonic) * (1.0 + boost * gaussian(harmonic, center, 1.0 + center * 0.25));
            }
            additive(out, amplitudes);
          } },
        { "fold_sine", "Fold Sine", 8, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double gain = 1.0 + 9.0 * t * t;
            double bias = 0.25 * t * random.unit();
            for (int i = 0; i < kSize; ++i) {
              double x = std::sin(kTwoPi * i / kSize) + bias;
              out[i] = std::sin(0.5 * kPi * gain * x);
            }
          } },
        { "fm_growl", "FM Growl", 8, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            static const int kRatios[] = { 1, 2, 3, 4 };
            int ratio = kRatios[random.integer(0, 3)];
            double index = 7.0 * std::pow(t, 1.3);
            double feedback = 0.4 * t;
            double previous = 0.0;
            for (int i = 0; i < kSize; ++i) {
              double phase = kTwoPi * i / kSize;
              double modulator = std::sin(ratio * phase + feedback * previous);
              previous = modulator;
              out[i] = std::sin(phase + index * modulator);
            }
          } },
        { "sync_screech", "Sync Screech", 8, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double ratio = 1.0 + 7.0 * t + 0.5 * random.unit() * t;
            for (int i = 0; i < kSize; ++i) {
              double phase = i / (double)kSize;
              double window = std::pow(1.0 - phase, 0.35);
              out[i] = saw(phase * ratio) * window;
            }
          } },
        { "vowel_growl", "Vowel Growl", 8, kInterpolationModeFrequency,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            // Walk through four vowels picked by the seed.
            int order[4];
            for (int& index : order)
              index = random.integer(0, kNumVowels - 1);
            double position = t * 3.0;
            int from = std::min(2, (int)position);
            double blend = position - from;
            const Vowel& a = kVowels[order[from]];
            const Vowel& b = kVowels[order[from + 1]];
            double f1 = a.f1 + (b.f1 - a.f1) * blend;
            double f2 = a.f2 + (b.f2 - a.f2) * blend;
            double f3 = a.f3 + (b.f3 - a.f3) * blend;
            constexpr double kFundamental = 90.0;
            std::vector<double> amplitudes(200);
            for (size_t k = 0; k < amplitudes.size(); ++k) {
              double frequency = (k + 1.0) * kFundamental;
              double formants = 1.0 * gaussian(frequency, f1, 90.0) + 0.7 * gaussian(frequency, f2, 120.0) +
                                0.35 * gaussian(frequency, f3, 160.0);
              amplitudes[k] = (0.15 / std::sqrt(k + 1.0) + formants) / std::sqrt(k + 1.0);
            }
            additive(out, amplitudes);
          } },
        { "pwm_hollow", "PWM Hollow", 6, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double width = 0.5 - 0.44 * t;
            double skew = 0.03 * random.unit();
            for (int i = 0; i < kSize; ++i) {
              double phase = i / (double)kSize;
              out[i] = phase < width + skew ? 1.0 : -1.0;
            }
          } },
        { "harsh_digital", "Harsh Digital", 8, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double levels = std::max(2.0, std::round(48.0 * std::pow(1.0 - t, 2.0)));
            int extra_harmonic = random.integer(5, 23);
            for (int i = 0; i < kSize; ++i) {
              double phase = i / (double)kSize;
              double value = 0.8 * saw(phase) + 0.35 * t * std::sin(kTwoPi * extra_harmonic * phase);
              out[i] = std::round(value * levels) / levels;
            }
          } },
        { "stack_saw", "Stack Saw", 6, kInterpolationModeFrequency,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            // Saw harmonics with a moving comb, like several saws beating together.
            double comb = 2.0 + 6.0 * random.unit();
            std::vector<double> amplitudes(160);
            for (size_t k = 0; k < amplitudes.size(); ++k) {
              double harmonic = k + 1.0;
              double notch = 0.5 + 0.5 * std::cos(kTwoPi * (harmonic / comb + t));
              amplitudes[k] = (1.0 / harmonic) * (0.35 + 0.65 * notch);
            }
            additive(out, amplitudes);
          } },
        { "soft_lead", "Soft Lead", 6, kInterpolationModeTime,
          [](std::vector<double>& out, double t, GeneratorRandom& random) {
            double drive = 1.0 + 2.0 * random.unit() * t;
            for (int i = 0; i < kSize; ++i) {
              double phase = i / (double)kSize;
              double triangle = 1.0 - 4.0 * std::fabs(phase - 0.5);
              double value = (1.0 - t) * triangle + t * saw(phase + 0.5);
              out[i] = std::tanh(drive * value);
            }
          } },
        { "sub_sine", "Sub Sine", 1, kInterpolationModeTime,
          [](std::vector<double>& out, double, GeneratorRandom&) {
            for (int i = 0; i < kSize; ++i)
              out[i] = std::sin(kTwoPi * i / kSize);
          } },
        { "warm_saw", "Warm Saw", 4, kInterpolationModeFrequency,
          [](std::vector<double>& out, double t, GeneratorRandom&) {
            // Saw with a soft low pass that opens across the table.
            double cutoff = 4.0 + 60.0 * t * t;
            std::vector<double> amplitudes(160);
            for (size_t k = 0; k < amplitudes.size(); ++k) {
              double harmonic = k + 1.0;
              amplitudes[k] = (1.0 / harmonic) / std::sqrt(1.0 + std::pow(harmonic / cutoff, 4.0));
            }
            additive(out, amplitudes);
          } },
      };
      return kFamilies;
    }

    const Family* findFamily(const std::string& name) {
      for (const Family& family : families()) {
        if (family.name == name)
          return &family;
      }
      return nullptr;
    }

    void normalize(std::vector<double>& wave) {
      double mean = 0.0;
      for (double sample : wave)
        mean += sample;
      mean /= wave.size();

      double peak = 0.0;
      for (double& sample : wave) {
        sample -= mean;
        peak = std::max(peak, std::fabs(sample));
      }

      if (peak > 1e-9) {
        for (double& sample : wave)
          sample /= peak;
      }
    }

    std::vector<float> renderFamilyFrame(const Family& family, double t, uint32_t seed) {
      // Each frame gets a fresh random source from the same seed so per-table
      // choices (ratios, vowel order, ...) are stable across the whole table.
      GeneratorRandom random(seed);
      std::vector<double> wave(kSize, 0.0);
      family.render(wave, t, random);
      normalize(wave);
      return std::vector<float>(wave.begin(), wave.end());
    }
  } // namespace

  const std::vector<std::string>& WavetableFactory::names() {
    static std::vector<std::string> kNames = [] {
      std::vector<std::string> result;
      for (const Family& family : families())
        result.push_back(family.name);
      return result;
    }();
    return kNames;
  }

  bool WavetableFactory::exists(const std::string& name) {
    return findFamily(name) != nullptr;
  }

  std::vector<float> WavetableFactory::renderFrame(const std::string& name, float t, uint32_t seed) {
    const Family* family = findFamily(name);
    if (family == nullptr)
      family = findFamily("warm_saw");
    return renderFamilyFrame(*family, t, seed);
  }

  json WavetableFactory::create(const std::string& name, uint32_t seed) {
    const Family* family = findFamily(name);
    if (family == nullptr)
      family = findFamily("warm_saw");

    json keyframes = json::array();
    int num_keyframes = family->num_keyframes;
    for (int i = 0; i < num_keyframes; ++i) {
      double t = num_keyframes > 1 ? i / (num_keyframes - 1.0) : 0.0;
      int position = num_keyframes > 1 ? (int)std::round(t * kMaxPosition) : 0;
      std::vector<float> frame = renderFamilyFrame(*family, t, seed);
      juce::String encoded = juce::Base64::toBase64(frame.data(), sizeof(float) * frame.size());
      keyframes.push_back({ { "position", position }, { "wave_data", encoded.toStdString() } });
    }

    json component = {
      { "keyframes", keyframes },
      { "type", "Wave Source" },
      { "interpolation_style", num_keyframes > 2 ? kInterpolationStyleCubic : kInterpolationStyleLinear },
      { "interpolation", family->interpolation_mode },
    };

    json group = { { "components", json::array({ component }) } };

    return {
      { "groups", json::array({ group }) },
      { "name", family->display_name },
      { "author", "Draco Gen" },
      { "version", vital::kFileFormatVersion },
      { "remove_all_dc", true },
      { "full_normalize", true },
    };
  }

} // namespace draco
