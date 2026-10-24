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

#include "patch_auditor.h"

#include "synth_base.h"

#include <cmath>

namespace draco {

  namespace {
    constexpr int kMaxAttempts = 8;
    constexpr float kTailSeconds = 0.15f;

    float toDb(double magnitude) {
      return (float)(20.0 * std::log10(std::max(1e-6, magnitude)));
    }

    double volumeValueToDb(double value) {
      return std::sqrt(std::max(0.0, value)) - 80.0;
    }
  } // namespace

  PatchAuditor::PatchAuditor() : synth_(std::make_unique<HeadlessSynth>()) {
    init_state_ = synth_->saveToJson();
  }

  PatchAuditor::~PatchAuditor() = default;

  void PatchAuditor::load(const json& state) {
    synth_->loadFromJson(state);
  }

  void PatchAuditor::renderToBuffer(const json& state, int note, float note_seconds, AudioSampleBuffer& buffer) {
    load(state);
    buffer.setSize(2, (int)((note_seconds + kTailSeconds) * kSampleRate));
    buffer.clear();
    synth_->renderNotesToBuffer(buffer, kSampleRate, note_seconds, 120.0f, { note });
  }

  PatchAuditor::Report PatchAuditor::audit(const json& state, const std::vector<int>& notes, float note_seconds) {
    constexpr int kWindow = kSampleRate / 20;
    constexpr int kHop = kWindow / 2;

    Report report;
    load(state);

    AudioSampleBuffer buffer(2, (int)((note_seconds + kTailSeconds) * kSampleRate));
    double peak = 0.0;
    double sum_db = 0.0;
    report.quietest_note_db = 1000.0f;
    for (int note : notes) {
      buffer.clear();
      synth_->renderNotesToBuffer(buffer, kSampleRate, note_seconds, 120.0f, { note });

      const float* left = buffer.getReadPointer(0);
      const float* right = buffer.getReadPointer(1);
      int num_samples = buffer.getNumSamples();
      for (int i = 0; i < num_samples; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i])) {
          report.non_finite = true;
          continue;
        }
        peak = std::max(peak, (double)std::max(std::fabs(left[i]), std::fabs(right[i])));
      }

      double loudest_window = 0.0;
      for (int start = 0; start + kWindow <= num_samples; start += kHop) {
        double sum = 0.0;
        for (int i = start; i < start + kWindow; ++i) {
          if (std::isfinite(left[i]) && std::isfinite(right[i]))
            sum += left[i] * left[i] + right[i] * right[i];
        }
        loudest_window = std::max(loudest_window, std::sqrt(sum / (2.0 * kWindow)));
      }

      float note_db = toDb(loudest_window);
      sum_db += note_db;
      report.quietest_note_db = std::min(report.quietest_note_db, note_db);
      report.loudest_note_db = std::max(report.loudest_note_db, note_db);
    }

    report.peak_db = toDb(peak);
    report.rms_db = notes.empty() ? -120.0f : (float)(sum_db / notes.size());
    return report;
  }

  PatchAuditor::Report PatchAuditor::normalize(json& state, const std::vector<int>& notes, float target_rms_db) {
    Report report = audit(state, notes);
    for (int pass = 0; pass < 3 && report.playable(); ++pass) {
      float change = target_rms_db - report.rms_db;
      if (std::fabs(change) < 0.5f)
        break;

      change = std::max(-kMaxGainChangeDb, std::min(kMaxGainChangeDb, change));
      json& settings = state["settings"];
      double current_value = settings.count("volume") ? settings["volume"].get<double>() :
                             vital::Parameters::getDetails("volume").default_value;
      double current_db = volumeValueToDb(current_value);
      double new_db = std::max(-30.0, std::min((double)kMaxVolumeDb, current_db + change));
      settings["volume"] = PatchGenerator::volumeDbToValue(new_db);

      // The volume knob tops out at +6 dB; make up the rest with oscillator levels.
      double remaining_db = current_db + change - new_db;
      if (remaining_db > 0.5) {
        double level_scale = std::sqrt(std::pow(10.0, remaining_db / 20.0));
        for (int i = 1; i <= vital::kNumOscillators; ++i) {
          std::string name = "osc_" + std::to_string(i) + "_level";
          if (settings.count(name))
            settings[name] = std::min(1.0, settings[name].get<double>() * level_scale);
        }
      }
      report = audit(state, notes);
    }
    return report;
  }

  std::vector<int> PatchAuditor::testNotesForType(const std::string& type) {
    if (type == "Bass")
      return { 36, 48 };
    if (type == "Pad")
      return { 48, 60 };
    return { 60, 72 };
  }

  float PatchAuditor::targetRmsForConfig(const json* config) {
    if (config && config->count("loudness") && (*config)["loudness"].count("target_rms_db"))
      return (*config)["loudness"]["target_rms_db"].get<float>();
    return kDefaultTargetRmsDb;
  }

  PatchGenerator::Result PatchAuditor::generatePlayable(const PatchGenerator& generator, const std::string& type,
                                                        const std::string& genre, uint32_t seed,
                                                        const json& base_state, Report* report) {
    std::vector<int> notes = testNotesForType(type);
    float target = targetRmsForConfig(generator.getConfig(type, genre));

    PatchGenerator::Result result;
    Report current;
    uint32_t attempt_seed = seed;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      result = generator.generate(type, genre, attempt_seed, base_state.is_null() ? init_state_ : base_state);
      current = audit(result.state, notes);
      if (current.playable())
        break;
      attempt_seed = attempt_seed * 1664525u + 1013904223u;
    }

    if (current.playable())
      current = normalize(result.state, notes, target);

    if (report)
      *report = current;
    return result;
  }

} // namespace draco
