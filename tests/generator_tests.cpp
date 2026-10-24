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

// Unit tests for the Draco Gen patch generator. Plain asserts, no framework, so
// the binary runs anywhere CTest does.

#include "JuceHeader.h"
#include "synth_base.h"
#include "load_save.h"
#include "patch_auditor.h"
#include "patch_generator.h"
#include "patch_history.h"
#include "wavetable_factory.h"

#include <cmath>
#include <functional>
#include <iostream>

namespace {
  int failures = 0;
  int checks = 0;

  void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
      ++failures;
      std::cout << "  FAILED: " << message << std::endl;
    }
  }

  void test(const std::string& name, const std::function<void()>& body) {
    int before = failures;
    std::cout << name << std::endl;
    body();
    std::cout << (failures == before ? "  ok" : "  FAILED") << std::endl;
  }

  float valueOr(const json& settings, const std::string& name) {
    if (settings.count(name))
      return settings[name].get<float>();
    return vital::Parameters::getDetails(name).default_value;
  }
} // namespace

int main() {
  ScopedJuceInitialiser_GUI juce_init;
  draco::PatchGenerator generator;

  test("configs load for Lead/Bass x Hybrid Trap/Tearout", [&] {
    for (std::string type : { "Lead", "Bass" }) {
      for (std::string genre : { "Hybrid Trap", "Tearout" })
        check(generator.hasConfig(type, genre), type + " / " + genre + " config is missing");
    }
    std::vector<std::string> types = generator.getTypes();
    check(!types.empty() && types[0] == "Lead", "types are ordered Lead, Bass, ...");
  });

  test("invalid configs are rejected", [&] {
    draco::PatchGenerator local;
    std::string error;
    check(!local.addConfig(json::object(), error), "empty config accepted");
    check(!local.addConfig({ { "format", "draco-generator-config" } }, error), "config without type accepted");
  });

  test("same seed gives the same patch", [&] {
    for (std::string type : { "Lead", "Bass" }) {
      for (std::string genre : { "Hybrid Trap", "Tearout" }) {
        json a = generator.generate(type, genre, 12345).state;
        json b = generator.generate(type, genre, 12345).state;
        check(a == b, type + " / " + genre + " is not deterministic");
        json c = generator.generate(type, genre, 54321).state;
        check(a["settings"] != c["settings"], type + " / " + genre + " ignores the seed");
      }
    }
  });

  test("generated parameters are valid and in range", [&] {
    for (std::string type : { "Lead", "Bass" }) {
      for (std::string genre : { "Hybrid Trap", "Tearout" }) {
        for (uint32_t seed = 1; seed <= 30; ++seed) {
          draco::PatchGenerator::Result result = generator.generate(type, genre, seed);
          const json& settings = result.state["settings"];
          for (auto& entry : settings.items()) {
            if (!entry.value().is_number())
              continue;
            check(vital::Parameters::isParameter(entry.key()), "unknown parameter " + entry.key());
            const vital::ValueDetails& details = vital::Parameters::getDetails(entry.key());
            float value = entry.value().get<float>();
            check(std::isfinite(value) && value >= details.min - 1e-4f && value <= details.max + 1e-4f,
                  entry.key() + " out of range in " + type + "/" + genre);
            check(!vital::isEffectParameter(entry.key()), "effect parameter set: " + entry.key());
          }
          check(settings["wavetables"].size() == vital::kNumOscillators, "needs one wavetable per oscillator");
          check(settings["lfos"].size() == vital::kNumLfos, "needs one shape per LFO");
          check(valueOr(settings, "osc_1_on") == 1.0f, "oscillator 1 must be on");
          check(result.state["draco"]["type"] == type && result.state["draco"]["genre"] == genre, "missing tags");
          check(!result.name.empty(), "patch needs a name");

          for (const json& modulation : settings["modulations"]) {
            std::string destination = modulation["destination"];
            check(vital::Parameters::isParameter(destination), "bad modulation destination " + destination);
          }
        }
      }
    }
  });

  test("bass patches are mono and pitched down; tearout bass has fast LFO modulation", [&] {
    for (uint32_t seed = 1; seed <= 30; ++seed) {
      json settings = generator.generate("Bass", "Tearout", seed).state["settings"];
      check(valueOr(settings, "polyphony") == 1.0f, "tearout bass should be mono");
      check(valueOr(settings, "osc_1_transpose") <= -12.0f, "tearout bass osc 1 should be an octave down");
      bool lfo_to_filter = false;
      bool lfo_to_frame = false;
      for (const json& modulation : settings["modulations"]) {
        if (modulation["source"] == "lfo_1" && modulation["destination"] == "filter_1_cutoff")
          lfo_to_filter = true;
        if (modulation["source"] == "lfo_1" && modulation["destination"] == "osc_1_wave_frame")
          lfo_to_frame = true;
      }
      check(lfo_to_filter && lfo_to_frame, "tearout bass needs LFO on filter and wavetable position");
      // kSyncedFrequencyNames: 8 = 1/4, 9 = 1/8. Sync 3 = triplets, so 1/4T counts as fast.
      float sync = valueOr(settings, "lfo_1_sync");
      float tempo = valueOr(settings, "lfo_1_tempo");
      check(sync >= 1.0f && (tempo >= 9.0f || (tempo == 8.0f && sync == 3.0f)),
            "tearout bass LFO should be tempo synced at 1/4T or faster");
    }
  });

  test("wavetable frames are normalized and DC free", [&] {
    for (const std::string& name : draco::WavetableFactory::names()) {
      for (float t : { 0.0f, 0.5f, 1.0f }) {
        std::vector<float> frame = draco::WavetableFactory::renderFrame(name, t, 7);
        check(frame.size() == draco::WavetableFactory::kFrameSize, name + " frame size");
        double peak = 0.0, mean = 0.0;
        for (float sample : frame) {
          peak = std::max(peak, (double)std::fabs(sample));
          mean += sample;
        }
        mean /= frame.size();
        check(std::fabs(peak - 1.0) < 1e-3, name + " is not normalized");
        check(std::fabs(mean) < 1e-3, name + " has DC offset");
      }
      json table = draco::WavetableFactory::create(name, 3);
      check(table["groups"][0]["components"][0]["type"] == "Wave Source", name + " component type");
    }
  });

  test("history walks back and forth", [&] {
    draco::PatchHistory history;
    check(history.current() == nullptr && !history.hasPrevious() && !history.hasNext(), "empty history");
    for (uint32_t seed = 1; seed <= 3; ++seed) {
      draco::PatchGenerator::Result result;
      result.seed = seed;
      history.push(result);
    }
    check(history.current()->seed == 3, "push moves to newest");
    check(history.previous()->seed == 2 && history.previous()->seed == 1, "previous walks back");
    check(history.previous() == nullptr, "previous stops at the start");
    check(history.next()->seed == 2 && history.next()->seed == 3, "next walks forward");
    check(history.next() == nullptr, "next stops at the end");

    draco::PatchHistory long_history;
    for (int i = 0; i < draco::PatchHistory::kMaxEntries + 20; ++i)
      long_history.push({});
    check(long_history.size() == draco::PatchHistory::kMaxEntries, "history is capped");
  });

  test("save writes a tagged preset that loads back", [&] {
    File folder = File::getSpecialLocation(File::tempDirectory).getChildFile("draco_gen_test_presets");
    folder.deleteRecursively();
    draco::PatchGenerator::Result result = generator.generate("Bass", "Tearout", 99);
    File first = draco::PatchSaver::save(result, folder);
    File second = draco::PatchSaver::save(result, folder);
    check(first.existsAsFile() && second.existsAsFile() && first != second, "saves are unique files");
    check(first.getParentDirectory().getFileName() == "Tearout" &&
          first.getParentDirectory().getParentDirectory().getFileName() == "Bass", "saved under Type/Genre");
    check(first.hasFileExtension("draco"), "saved with the .draco extension");

    json loaded = json::parse(first.loadFileAsString().toStdString());
    check(loaded["draco"]["genre"] == "Tearout", "genre tag saved");
    check(loaded["preset_style"] == "Bass", "preset style saved");

    HeadlessSynth synth;
    std::string error;
    check(synth.loadFromFile(first, error), "saved preset loads: " + error);
    folder.deleteRecursively();
  });

  test("generated patches are playable and normalized", [&] {
    draco::PatchAuditor auditor;
    for (std::string type : { "Lead", "Bass" }) {
      for (std::string genre : { "Hybrid Trap", "Tearout" }) {
        for (uint32_t seed = 200; seed < 205; ++seed) {
          draco::PatchAuditor::Report report;
          auditor.generatePlayable(generator, type, genre, seed, json(), &report);
          check(report.playable(), type + "/" + genre + " seed " + std::to_string(seed) + " not playable");
          check(report.peak_db <= -0.9f, "peak above the limiter ceiling");
          check(report.rms_db > -22.0f, "patch is too quiet");
        }
      }
    }
  });

  test("output limiter caps loud chords", [&] {
    HeadlessSynth synth;
    AudioSampleBuffer buffer(2, 44100);
    buffer.clear();
    synth.renderNotesToBuffer(buffer, 44100, 0.8f, 120.0f, { 48, 52, 55, 60, 64, 67, 72, 76 }, 1.0f);
    float peak = std::max(buffer.getMagnitude(0, 0, buffer.getNumSamples()),
                          buffer.getMagnitude(1, 0, buffer.getNumSamples()));
    check(peak <= 0.8915f, "limiter let a peak through: " + std::to_string(peak));
    check(peak > 0.1f, "render is silent");
  });

  std::cout << std::endl << checks << " checks, " << failures << " failures" << std::endl;
  return failures == 0 ? 0 : 1;
}
