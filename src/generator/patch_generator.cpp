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

#include "patch_generator.h"

#include "JuceHeader.h"
#include "generator_random.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "compressor.h"
#include "digital_svf.h"
#include "distortion.h"
#include "random_lfo.h"
#include "synth_lfo.h"
#include "synth_oscillator.h"
#include "voice_handler.h"
#include "wavetable.h"
#include "synth_strings.h"
#include "wavetable_factory.h"

#include <algorithm>
#include <cmath>

namespace draco {

  namespace {
    const std::vector<std::string> kDefaultTypeOrder = { "Lead", "Bass", "Pad", "Pluck", "FX/Zap" };
    const std::vector<std::string> kDefaultGenreOrder = {
      "Hybrid Trap", "Tearout", "Dark Cyberpunk", "Emotional Trap", "Synthwave"
    };

    const std::vector<std::string> kNameWords = {
      "Wyrm", "Ember", "Fang", "Obsidian", "Ash", "Venom", "Scale", "Talon", "Cinder", "Hex",
      "Brimstone", "Razor", "Onyx", "Smolder", "Inferno", "Basilisk", "Drake", "Ruin", "Sulfur", "Shard",
      "Wraith", "Molten", "Abyss", "Crimson", "Grim", "Viper", "Rift", "Char", "Nox", "Tremor"
    };

    // Oscillator distortion ("warp") types. The Vital names for the FM/RM types
    // repeat, so the config uses its own unambiguous names.
    const std::vector<std::pair<std::string, int>> kDistortionTypes = {
      { "None", 0 }, { "Sync", 1 }, { "Formant", 2 }, { "Quantize", 3 }, { "Bend", 4 }, { "Squeeze", 5 },
      { "Pulse", 6 }, { "FM A", 7 }, { "FM B", 8 }, { "FM Sample", 9 }, { "RM A", 10 }, { "RM B", 11 },
      { "RM Sample", 12 },
    };

    const std::vector<std::pair<std::string, int>> kDestinations = {
      { "Filter 1", vital::constants::kFilter1 },
      { "Filter 2", vital::constants::kFilter2 },
      { "Filter 1+2", vital::constants::kDualFilters },
      { "Output", vital::constants::kEffects },
      { "Direct Out", vital::constants::kDirectOut },
    };

    constexpr int kNumMorphTypes = sizeof(::strings::kSpectralMorphNames) / sizeof(std::string);
    constexpr int kNumFilterModels = sizeof(::strings::kFilterModelNames) / sizeof(std::string);
    constexpr int kNumFilterStyles = sizeof(::strings::kFilterStyleNames) / sizeof(std::string);
    constexpr int kNumStackStyles = sizeof(::strings::kUnisonStackNames) / sizeof(std::string);
    constexpr int kNumSyncTypes = sizeof(::strings::kSyncNames) / sizeof(std::string);
    constexpr int kNumTempos = sizeof(::strings::kSyncedFrequencyNames) / sizeof(std::string);

    int findName(const std::string& name, const std::string* names, int num_names, int fallback) {
      for (int i = 0; i < num_names; ++i) {
        if (juce::String(names[i]).equalsIgnoreCase(juce::String(name)))
          return i;
      }
      return fallback;
    }

    int findName(const std::string& name, const std::vector<std::pair<std::string, int>>& names, int fallback) {
      for (const auto& entry : names) {
        if (juce::String(entry.first).equalsIgnoreCase(juce::String(name)))
          return entry.second;
      }
      return fallback;
    }

    // A range spec is a number, [value] or [min, max].
    bool hasValue(const json& spec, const char* key) {
      return spec.is_object() && spec.count(key) && !spec[key].is_null();
    }

    double rangeValue(const json& spec, GeneratorRandom& random, double fallback) {
      if (spec.is_number())
        return spec.get<double>();
      if (spec.is_array() && !spec.empty() && spec[0].is_number()) {
        double min = spec[0].get<double>();
        double max = spec.size() > 1 && spec[1].is_number() ? spec[1].get<double>() : min;
        return random.uniform(std::min(min, max), std::max(min, max));
      }
      return fallback;
    }

    double rangeValue(const json& section, const char* key, GeneratorRandom& random, double fallback) {
      if (!hasValue(section, key))
        return fallback;
      return rangeValue(section[key], random, fallback);
    }

    int rangeInt(const json& section, const char* key, GeneratorRandom& random, int fallback) {
      if (!hasValue(section, key))
        return fallback;
      const json& spec = section[key];
      if (spec.is_number())
        return (int)std::round(spec.get<double>());
      if (spec.is_array() && !spec.empty() && spec[0].is_number()) {
        int min = (int)std::round(spec[0].get<double>());
        int max = spec.size() > 1 && spec[1].is_number() ? (int)std::round(spec[1].get<double>()) : min;
        return random.integer(std::min(min, max), std::max(min, max));
      }
      return fallback;
    }

    // A choice spec is a string or a list of strings (repeat an entry to weight it).
    std::string pickString(const json& section, const char* key, GeneratorRandom& random,
                           const std::string& fallback) {
      if (!hasValue(section, key))
        return fallback;
      const json& spec = section[key];
      if (spec.is_string())
        return spec.get<std::string>();
      if (spec.is_array() && !spec.empty()) {
        const json& choice = spec[random.integer(0, (int)spec.size() - 1)];
        if (choice.is_string())
          return choice.get<std::string>();
      }
      return fallback;
    }

    float clampParameter(const std::string& name, double value) {
      if (!vital::Parameters::isParameter(name))
        return (float)value;
      const vital::ValueDetails& details = vital::Parameters::getDetails(name);
      return (float)std::max<double>(details.min, std::min<double>(details.max, value));
    }

    void setParameter(json& settings, const std::string& name, double value) {
      if (!vital::Parameters::isParameter(name))
        return;
      settings[name] = clampParameter(name, value);
    }

    // LFO shapes, written in value space (0 bottom, 1 top). Vital stores y flipped.
    json makeLineShape(const std::string& name, const std::vector<std::pair<float, float>>& points,
                       const std::vector<float>& powers, bool smooth) {
      json point_data = json::array();
      json power_data = json::array();
      for (size_t i = 0; i < points.size(); ++i) {
        point_data.push_back(points[i].first);
        point_data.push_back(1.0f - points[i].second);
        power_data.push_back(i < powers.size() ? powers[i] : 0.0f);
      }
      return {
        { "name", name },
        { "num_points", (int)points.size() },
        { "points", point_data },
        { "powers", power_data },
        { "smooth", smooth },
      };
    }

    json makeLfoShape(const std::string& shape, GeneratorRandom& random) {
      if (shape == "sine")
        return makeLineShape("Sine", { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }, { }, true);
      if (shape == "saw_down")
        return makeLineShape("Saw Down", { { 0.0f, 1.0f }, { 1.0f, 0.0f } }, { }, false);
      if (shape == "saw_up")
        return makeLineShape("Saw Up", { { 0.0f, 0.0f }, { 1.0f, 1.0f } }, { }, false);
      if (shape == "square") {
        return makeLineShape("Square", { { 0.0f, 1.0f }, { 0.5f, 1.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f } },
                             { }, false);
      }
      if (shape == "pluck")
        return makeLineShape("Pluck", { { 0.0f, 1.0f }, { 1.0f, 0.0f } }, { -5.0f }, false);
      if (shape == "wobble") {
        float peak = (float)random.uniform(0.3, 0.6);
        return makeLineShape("Wobble", { { 0.0f, 0.0f }, { peak, 1.0f }, { 1.0f, 0.0f } }, { 2.0f, -2.0f }, false);
      }
      if (shape == "yoi") {
        return makeLineShape("Yoi", { { 0.0f, 0.0f }, { 0.22f, 1.0f }, { 0.55f, 0.25f }, { 0.8f, 0.6f },
                                      { 1.0f, 0.0f } }, { }, true);
      }
      if (shape == "stepped") {
        int steps = random.chance(0.5) ? 4 : 8;
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < steps; ++i) {
          float value = (float)random.unit();
          points.emplace_back(i / (float)steps, value);
          points.emplace_back((i + 1) / (float)steps, value);
        }
        return makeLineShape("Stepped", points, { }, false);
      }
      if (shape == "chop") {
        constexpr int kSteps = 8;
        bool gates[kSteps];
        int open = 0;
        for (bool& gate : gates) {
          gate = random.chance(0.65);
          open += gate ? 1 : 0;
        }
        if (open < kSteps / 2)
          gates[0] = gates[2] = gates[4] = gates[6] = true;
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < kSteps; ++i) {
          float value = gates[i] ? 1.0f : 0.0f;
          points.emplace_back(i / (float)kSteps, value);
          points.emplace_back((i + 1) / (float)kSteps, value);
        }
        return makeLineShape("Chop", points, { }, false);
      }
      return makeLineShape("Triangle", { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }, { }, false);
    }

    json defaultLfo() {
      GeneratorRandom unused(0);
      return makeLfoShape("triangle", unused);
    }

    json defaultWavetable() {
      return WavetableFactory::create("warm_saw", 0);
    }

    // Parses tempo names like "1/16", "1/8T" (triplet) and "1/4D" (dotted).
    bool parseTempo(std::string tempo, int& tempo_index, int& sync) {
      sync = 1;
      if (!tempo.empty() && (tempo.back() == 'T' || tempo.back() == 't')) {
        sync = 3;
        tempo.pop_back();
      }
      else if (!tempo.empty() && (tempo.back() == 'D' || tempo.back() == 'd')) {
        sync = 2;
        tempo.pop_back();
      }
      tempo_index = findName(tempo, ::strings::kSyncedFrequencyNames, kNumTempos, -1);
      return tempo_index >= 0;
    }

    std::string sectionKey(int index) { return std::to_string(index); }
  } // namespace

  float PatchGenerator::envelopeSecondsToValue(double seconds) {
    return (float)std::pow(std::max(0.0, seconds), 0.25);
  }

  float PatchGenerator::glideSecondsToValue(double seconds) {
    if (seconds <= 0.001)
      return -10.0f;
    return (float)std::max(-10.0, std::min(4.0, std::log2(seconds)));
  }

  float PatchGenerator::lfoHzToValue(double hz) {
    return (float)std::log2(std::max(0.01, hz));
  }

  float PatchGenerator::cutoffHzToValue(double hz) {
    double midi = 69.0 + 12.0 * std::log2(std::max(1.0, hz) / 440.0);
    return (float)std::max(8.0, std::min(136.0, midi));
  }

  float PatchGenerator::volumeDbToValue(double db) {
    double offset = std::max(0.0, db + 80.0);
    return (float)(offset * offset);
  }

  float PatchGenerator::detunePercentToValue(double percent) {
    return (float)std::sqrt(std::max(0.0, percent));
  }

  std::string PatchGenerator::makeKey(const std::string& type, const std::string& genre) {
    return juce::String(type + "|" + genre).toLowerCase().toStdString();
  }

  PatchGenerator::PatchGenerator() {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
      std::string filename = BinaryData::originalFilenames[i];
      if (!juce::String(filename).endsWithIgnoreCase(".json"))
        continue;

      int size = 0;
      const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
      if (data == nullptr)
        continue;

      try {
        json config = json::parse(std::string(data, size));
        std::string error;
        addConfig(config, error);
      }
      catch (const json::exception&) { }
    }
  }

  bool PatchGenerator::addConfig(const json& config, std::string& error) {
    if (!config.is_object() || config.value("format", "") != "draco-generator-config") {
      error = "Not a Draco Gen generator config.";
      return false;
    }
    if (!config.count("type") || !config.count("genre") || !config["type"].is_string() ||
        !config["genre"].is_string()) {
      error = "Generator config needs a type and a genre.";
      return false;
    }

    std::string type = config["type"];
    std::string genre = config["genre"];
    configs_[makeKey(type, genre)] = config;

    auto addOrdered = [](std::vector<std::string>& order, const std::vector<std::string>& defaults,
                         const std::string& value) {
      if (std::find(order.begin(), order.end(), value) != order.end())
        return;
      order.push_back(value);
      std::stable_sort(order.begin(), order.end(), [&defaults](const std::string& a, const std::string& b) {
        auto index = [&defaults](const std::string& v) {
          auto found = std::find(defaults.begin(), defaults.end(), v);
          return found == defaults.end() ? defaults.size() : (size_t)(found - defaults.begin());
        };
        return index(a) < index(b);
      });
    };
    addOrdered(type_order_, kDefaultTypeOrder, type);
    addOrdered(genre_order_, kDefaultGenreOrder, genre);
    return true;
  }

  std::vector<std::string> PatchGenerator::getTypes() const {
    return type_order_;
  }

  std::vector<std::string> PatchGenerator::getGenres() const {
    return genre_order_;
  }

  std::vector<std::string> PatchGenerator::getGenresForType(const std::string& type) const {
    std::vector<std::string> genres;
    for (const std::string& genre : genre_order_) {
      if (hasConfig(type, genre))
        genres.push_back(genre);
    }
    return genres;
  }

  bool PatchGenerator::hasConfig(const std::string& type, const std::string& genre) const {
    return configs_.count(makeKey(type, genre)) > 0;
  }

  const json* PatchGenerator::getConfig(const std::string& type, const std::string& genre) const {
    auto found = configs_.find(makeKey(type, genre));
    return found == configs_.end() ? nullptr : &found->second;
  }

  PatchGenerator::Result PatchGenerator::generate(const std::string& type, const std::string& genre,
                                                  uint32_t seed, const json& base_state) const {
    Result result;
    result.type = type;
    result.genre = genre;
    result.seed = seed;

    const json* config_pointer = getConfig(type, genre);
    if (config_pointer == nullptr && !configs_.empty())
      config_pointer = &configs_.begin()->second;

    json config = config_pointer ? *config_pointer : json::object();
    GeneratorRandom random(seed);

    // Pick a weighted variant and apply its overrides on top of the base ranges.
    json recipe = config.value("base", json::object());
    std::string variant_name = "";
    std::vector<std::string> tags = config.value("tags", std::vector<std::string>());
    if (config.count("variants") && config["variants"].is_array() && !config["variants"].empty()) {
      double total_weight = 0.0;
      for (const json& variant : config["variants"])
        total_weight += std::max(0.0, variant.value("weight", 1.0));

      double choice = random.unit() * total_weight;
      const json* chosen = &config["variants"][0];
      for (const json& variant : config["variants"]) {
        choice -= std::max(0.0, variant.value("weight", 1.0));
        if (choice < 0.0) {
          chosen = &variant;
          break;
        }
      }

      variant_name = chosen->value("name", "");
      json overrides = *chosen;
      overrides.erase("name");
      overrides.erase("weight");
      if (overrides.count("tags")) {
        for (const json& tag : overrides["tags"])
          tags.push_back(tag.get<std::string>());
        overrides.erase("tags");
      }
      recipe.merge_patch(overrides);
    }

    json settings = json::object();

    // Voice / global.
    json voice = recipe.value("voice", json::object());
    int polyphony = rangeInt(voice, "polyphony", random, 8);
    setParameter(settings, "polyphony", polyphony);
    setParameter(settings, "legato", rangeInt(voice, "legato", random, 0));
    double glide = 0.0;
    if (random.chance(rangeValue(voice, "glide_chance", random, 1.0)))
      glide = rangeValue(voice, "glide_s", random, 0.0);
    setParameter(settings, "portamento_time", glideSecondsToValue(glide));
    setParameter(settings, "portamento_slope", rangeValue(voice, "glide_slope", random, 0.0));
    setParameter(settings, "portamento_force", rangeInt(voice, "glide_always", random, 0));
    setParameter(settings, "pitch_bend_range", rangeInt(voice, "pitch_bend_range", random, 2));
    setParameter(settings, "velocity_track", rangeValue(voice, "velocity_track", random, 0.0));
    setParameter(settings, "volume", volumeDbToValue(rangeValue(voice, "volume_db", random, -6.0)));
    setParameter(settings, "stereo_routing", rangeValue(voice, "stereo_routing", random, 1.0));
    setParameter(settings, "oversampling", rangeInt(voice, "oversampling", random, 1));

    // Oscillators.
    json oscillators = recipe.value("oscillators", json::object());
    json wavetables = json::array();
    bool osc_on[vital::kNumOscillators] = { false, false, false };
    for (int i = 0; i < vital::kNumOscillators; ++i) {
      std::string prefix = "osc_" + std::to_string(i + 1) + "_";
      json osc = oscillators.value(sectionKey(i + 1), json());
      bool on = osc.is_object() && random.chance(rangeValue(osc, "chance", random, 1.0));
      if (i == 0 && osc.is_object())
        on = true;
      osc_on[i] = on;
      setParameter(settings, prefix + "on", on ? 1.0 : 0.0);

      if (!on) {
        wavetables.push_back(defaultWavetable());
        continue;
      }

      std::string table = pickString(osc, "wavetables", random, "warm_saw");
      wavetables.push_back(WavetableFactory::create(table, random.next()));

      setParameter(settings, prefix + "level", rangeValue(osc, "level", random, 0.7));
      setParameter(settings, prefix + "transpose", rangeInt(osc, "transpose", random, 0));
      setParameter(settings, prefix + "tune", rangeValue(osc, "tune", random, 0.0));
      setParameter(settings, prefix + "pan", rangeValue(osc, "pan", random, 0.0));
      setParameter(settings, prefix + "unison_voices", rangeInt(osc, "unison_voices", random, 1));
      setParameter(settings, prefix + "unison_detune",
                   detunePercentToValue(rangeValue(osc, "unison_detune_percent", random, 20.0)));
      setParameter(settings, prefix + "unison_blend", rangeValue(osc, "unison_blend", random, 0.8));
      setParameter(settings, prefix + "stereo_spread", rangeValue(osc, "stereo_spread", random, 1.0));
      setParameter(settings, prefix + "detune_power", rangeValue(osc, "detune_power", random, 1.5));
      setParameter(settings, prefix + "wave_frame", rangeValue(osc, "wave_frame", random, 0.0));
      setParameter(settings, prefix + "frame_spread", rangeValue(osc, "frame_spread", random, 0.0));
      setParameter(settings, prefix + "random_phase", rangeValue(osc, "random_phase", random, 1.0));
      setParameter(settings, prefix + "phase", rangeValue(osc, "phase", random, 0.5));

      std::string stack = pickString(osc, "stack_style", random, "Unison");
      setParameter(settings, prefix + "stack_style",
                   findName(stack, ::strings::kUnisonStackNames, kNumStackStyles, 0));

      std::string morph = pickString(osc, "spectral_morph", random, "None");
      setParameter(settings, prefix + "spectral_morph_type",
                   findName(morph, ::strings::kSpectralMorphNames, kNumMorphTypes, 0));
      setParameter(settings, prefix + "spectral_morph_amount",
                   rangeValue(osc, "spectral_morph_amount", random, 0.5));

      std::string distortion = pickString(osc, "distortion", random, "None");
      int distortion_type = findName(distortion, kDistortionTypes, 0);
      setParameter(settings, prefix + "distortion_type", distortion_type);
      setParameter(settings, prefix + "distortion_amount", rangeValue(osc, "distortion_amount", random, 0.5));

      std::string destination = pickString(osc, "destination", random, "Filter 1");
      setParameter(settings, prefix + "destination", findName(destination, kDestinations, 0));
    }

    // FM/RM warps read from the other oscillators. If that oscillator is off the
    // warp would do nothing, so fall back to a bend warp.
    for (int i = 0; i < vital::kNumOscillators; ++i) {
      std::string prefix = "osc_" + std::to_string(i + 1) + "_";
      if (!osc_on[i] || !settings.count(prefix + "distortion_type"))
        continue;
      int distortion_type = (int)settings[prefix + "distortion_type"].get<float>();
      int other = -1;
      if (distortion_type == 7 || distortion_type == 10)
        other = (i + 1) % vital::kNumOscillators;
      else if (distortion_type == 8 || distortion_type == 11)
        other = (i + 2) % vital::kNumOscillators;
      if (other >= 0 && !osc_on[other])
        setParameter(settings, prefix + "distortion_type", 4);
    }

    // Filters.
    json filters = recipe.value("filters", json::object());
    bool filter_on[2] = { false, false };
    for (int i = 0; i < 2; ++i) {
      std::string prefix = "filter_" + std::to_string(i + 1) + "_";
      json filter = filters.value(sectionKey(i + 1), json());
      bool on = filter.is_object() && random.chance(rangeValue(filter, "chance", random, 1.0));
      filter_on[i] = on;
      setParameter(settings, prefix + "on", on ? 1.0 : 0.0);
      if (!on)
        continue;

      std::string model = pickString(filter, "model", random, "Analog");
      setParameter(settings, prefix + "model",
                   findName(model, ::strings::kFilterModelNames, kNumFilterModels, 0));
      if (hasValue(filter, "style") && filter["style"].is_array() && !filter["style"].empty() &&
          filter["style"][0].is_number()) {
        setParameter(settings, prefix + "style", rangeInt(filter, "style", random, 0));
      }
      else {
        std::string style = pickString(filter, "style", random, "12dB");
        setParameter(settings, prefix + "style",
                     findName(style, ::strings::kFilterStyleNames, kNumFilterStyles, 0));
      }
      setParameter(settings, prefix + "cutoff", cutoffHzToValue(rangeValue(filter, "cutoff_hz", random, 2000.0)));
      setParameter(settings, prefix + "resonance", rangeValue(filter, "resonance", random, 0.3));
      setParameter(settings, prefix + "drive", rangeValue(filter, "drive_db", random, 0.0));
      setParameter(settings, prefix + "blend", rangeValue(filter, "blend", random, 0.0));
      setParameter(settings, prefix + "keytrack", rangeValue(filter, "keytrack", random, 0.0));
      setParameter(settings, prefix + "mix", rangeValue(filter, "mix", random, 1.0));
      setParameter(settings, prefix + "formant_x", rangeValue(filter, "formant_x", random, 0.5));
      setParameter(settings, prefix + "formant_y", rangeValue(filter, "formant_y", random, 0.5));
      setParameter(settings, prefix + "formant_resonance", rangeValue(filter, "formant_resonance", random, 0.85));
      setParameter(settings, prefix + "blend_transpose", rangeValue(filter, "blend_transpose", random, 42.0));
    }

    // Keep the legacy filter input switches in line with the oscillator routing.
    for (int osc = 0; osc < vital::kNumOscillators; ++osc) {
      std::string destination_name = "osc_" + std::to_string(osc + 1) + "_destination";
      int destination = settings.count(destination_name) ? (int)settings[destination_name].get<float>() :
                        (int)vital::Parameters::getDetails(destination_name).default_value;
      bool to_filter_1 = destination == vital::constants::kFilter1 || destination == vital::constants::kDualFilters;
      bool to_filter_2 = destination == vital::constants::kFilter2 || destination == vital::constants::kDualFilters;
      setParameter(settings, "filter_1_osc" + std::to_string(osc + 1) + "_input", to_filter_1 ? 1.0 : 0.0);
      setParameter(settings, "filter_2_osc" + std::to_string(osc + 1) + "_input", to_filter_2 ? 1.0 : 0.0);
    }

    // Envelopes. env 1 is the amp envelope.
    json envelopes = recipe.value("envelopes", json::object());
    for (int i = 0; i < vital::kNumEnvelopes; ++i) {
      json envelope = envelopes.value(sectionKey(i + 1), json());
      if (!envelope.is_object())
        continue;
      std::string prefix = "env_" + std::to_string(i + 1) + "_";
      setParameter(settings, prefix + "delay", envelopeSecondsToValue(rangeValue(envelope, "delay_s", random, 0.0)));
      setParameter(settings, prefix + "attack",
                   envelopeSecondsToValue(rangeValue(envelope, "attack_s", random, 0.0005)));
      setParameter(settings, prefix + "hold", envelopeSecondsToValue(rangeValue(envelope, "hold_s", random, 0.0)));
      setParameter(settings, prefix + "decay", envelopeSecondsToValue(rangeValue(envelope, "decay_s", random, 1.0)));
      setParameter(settings, prefix + "sustain", rangeValue(envelope, "sustain", random, 1.0));
      setParameter(settings, prefix + "release",
                   envelopeSecondsToValue(rangeValue(envelope, "release_s", random, 0.09)));
      setParameter(settings, prefix + "attack_power", rangeValue(envelope, "attack_power", random, 0.0));
      setParameter(settings, prefix + "decay_power", rangeValue(envelope, "decay_power", random, -2.0));
      setParameter(settings, prefix + "release_power", rangeValue(envelope, "release_power", random, -2.0));
    }

    // LFOs.
    json lfo_specs = recipe.value("lfos", json::object());
    json lfos = json::array();
    bool lfo_on[vital::kNumLfos] = { };
    for (int i = 0; i < vital::kNumLfos; ++i) {
      json lfo = lfo_specs.value(sectionKey(i + 1), json());
      bool on = lfo.is_object() && random.chance(rangeValue(lfo, "chance", random, 1.0));
      lfo_on[i] = on;
      if (!on) {
        lfos.push_back(defaultLfo());
        continue;
      }

      std::string prefix = "lfo_" + std::to_string(i + 1) + "_";
      lfos.push_back(makeLfoShape(pickString(lfo, "shape", random, "triangle"), random));

      std::string sync_type = pickString(lfo, "sync_type", random, "Trigger");
      setParameter(settings, prefix + "sync_type",
                   findName(sync_type, ::strings::kSyncNames, kNumSyncTypes, 0));

      int tempo_index = 0;
      int sync = 1;
      if (hasValue(lfo, "tempo") && parseTempo(pickString(lfo, "tempo", random, "1/4"), tempo_index, sync)) {
        setParameter(settings, prefix + "sync", sync);
        setParameter(settings, prefix + "tempo", tempo_index);
      }
      else {
        setParameter(settings, prefix + "sync", 0);
        setParameter(settings, prefix + "frequency", lfoHzToValue(rangeValue(lfo, "rate_hz", random, 2.0)));
      }

      setParameter(settings, prefix + "phase", rangeValue(lfo, "phase", random, 0.0));
      setParameter(settings, prefix + "fade_time", rangeValue(lfo, "fade_s", random, 0.0));
      setParameter(settings, prefix + "delay_time", rangeValue(lfo, "delay_s", random, 0.0));
      setParameter(settings, prefix + "smooth_mode", rangeInt(lfo, "smooth_mode", random, 0));
      setParameter(settings, prefix + "stereo", rangeValue(lfo, "stereo", random, 0.0));
    }

    // Modulations. Entries are keyed by name so variants can override or remove them.
    auto sourceAvailable = [&](const std::string& source) {
      if (source.rfind("lfo_", 0) == 0) {
        int index = std::atoi(source.c_str() + 4) - 1;
        return index >= 0 && index < vital::kNumLfos && lfo_on[index];
      }
      return true;
    };
    auto destinationAvailable = [&](const std::string& destination) {
      if (!vital::Parameters::isParameter(destination) || vital::isEffectParameter(destination))
        return false;
      if (destination.rfind("osc_", 0) == 0) {
        int index = std::atoi(destination.c_str() + 4) - 1;
        return index >= 0 && index < vital::kNumOscillators && osc_on[index];
      }
      if (destination.rfind("filter_1_", 0) == 0)
        return filter_on[0];
      if (destination.rfind("filter_2_", 0) == 0)
        return filter_on[1];
      return true;
    };

    json modulations = json::array();
    json modulation_specs = recipe.value("modulations", json::object());
    for (auto& entry : modulation_specs.items()) {
      const json& modulation = entry.value();
      if (!modulation.is_object())
        continue;
      if (modulations.size() >= vital::kMaxModulationConnections)
        break;
      if (!random.chance(rangeValue(modulation, "chance", random, 1.0)))
        continue;

      std::string source = pickString(modulation, "source", random, "");
      std::string destination = pickString(modulation, "destination", random, "");
      if (source.empty() || destination.empty() || !sourceAvailable(source) || !destinationAvailable(destination))
        continue;

      int number = (int)modulations.size() + 1;
      std::string prefix = "modulation_" + std::to_string(number) + "_";
      modulations.push_back({ { "source", source }, { "destination", destination } });
      setParameter(settings, prefix + "amount", rangeValue(modulation, "amount", random, 0.5));
      setParameter(settings, prefix + "bipolar", rangeInt(modulation, "bipolar", random, 0));
      setParameter(settings, prefix + "power", rangeValue(modulation, "power", random, 0.0));
      setParameter(settings, prefix + "stereo", rangeInt(modulation, "stereo", random, 0));
    }

    // Escape hatch: raw Vital parameter values.
    json raw = recipe.value("raw", json::object());
    for (auto& entry : raw.items())
      setParameter(settings, entry.key(), rangeValue(entry.value(), random, 0.0));

    // Macros: values and names.
    json macros = recipe.value("macros", json::object());
    json macro_names = json::object();
    for (int i = 0; i < vital::kNumMacros; ++i) {
      json macro = macros.value(sectionKey(i + 1), json());
      if (!macro.is_object())
        continue;
      setParameter(settings, "macro_control_" + std::to_string(i + 1), rangeValue(macro, "value", random, 0.0));
      if (macro.count("name") && macro["name"].is_string())
        macro_names["macro" + std::to_string(i + 1)] = macro["name"];
    }

    settings["modulations"] = modulations;
    settings["wavetables"] = wavetables;
    settings["lfos"] = lfos;
    if (base_state.is_object() && base_state.count("settings") && base_state["settings"].count("sample"))
      settings["sample"] = base_state["settings"]["sample"];

    std::string word = random.pick(kNameWords);
    std::string name = word + " " + (variant_name.empty() ? type : variant_name);
    result.name = name;
    result.variant = variant_name;

    tags.insert(tags.begin(), { type, genre });
    if (!variant_name.empty())
      tags.push_back(variant_name);
    result.tags = tags;

    std::string comments = "Generated by Draco Gen. Type: " + type + ". Genre: " + genre + ".";
    if (!variant_name.empty())
      comments += " Variant: " + variant_name + ".";
    comments += " Seed: " + std::to_string(seed) + ".";

    json state = {
      { "synth_version", vital::kFileFormatVersion },
      { "preset_name", name },
      { "author", "Draco Gen" },
      { "comments", comments },
      { "preset_style", config.value("preset_style", type) },
      { "settings", settings },
      { "draco", {
          { "generator_version", kGeneratorVersion },
          { "type", type },
          { "genre", genre },
          { "variant", variant_name },
          { "seed", seed },
          { "tags", tags },
        } },
    };
    for (auto& entry : macro_names.items())
      state[entry.key()] = entry.value();

    result.state = state;
    return result;
  }

} // namespace draco
