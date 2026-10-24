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

#pragma once

#include "JuceHeader.h"
#include "patch_generator.h"

#include <memory>

class HeadlessSynth;

namespace draco {

  // Renders patches offline with a private engine to catch silent or broken
  // patches and to set the output volume so generated patches land at a
  // consistent loudness.
  class PatchAuditor {
    public:
      static constexpr int kSampleRate = 44100;
      static constexpr float kSilenceDb = -50.0f;
      static constexpr float kDefaultTargetRmsDb = -15.0f;
      static constexpr float kMaxGainChangeDb = 18.0f;
      static constexpr float kMaxNoteSpreadDb = 18.0f;
      static constexpr float kMaxVolumeDb = 6.0f;

      struct Report {
        float peak_db = -120.0f;
        // Loudness is the loudest 50 ms RMS window of each note, averaged in dB over
        // the test notes, so plucks and sustained sounds compare fairly.
        float rms_db = -120.0f;
        float quietest_note_db = -120.0f;
        float loudest_note_db = -120.0f;
        bool non_finite = false;

        bool silent() const { return quietest_note_db < kSilenceDb; }
        bool balanced() const { return loudest_note_db - quietest_note_db <= kMaxNoteSpreadDb; }
        bool playable() const { return !silent() && !non_finite && balanced(); }
      };

      PatchAuditor();
      ~PatchAuditor();

      // Renders each note on its own. A patch only passes if every test note
      // sounds and the notes are within kMaxNoteSpreadDb of each other.
      Report audit(const json& state, const std::vector<int>& notes, float note_seconds = 0.45f);

      // Adjusts the patch's master volume toward target_rms_db. Returns the new report.
      Report normalize(json& state, const std::vector<int>& notes, float target_rms_db);

      // Audio of the last rendered note, for writing demo files.
      void renderToBuffer(const json& state, int note, float note_seconds, AudioSampleBuffer& buffer);

      // Generates a patch, retrying with derived seeds until it is playable, then
      // normalizes its loudness.
      PatchGenerator::Result generatePlayable(const PatchGenerator& generator, const std::string& type,
                                              const std::string& genre, uint32_t seed,
                                              const json& base_state, Report* report = nullptr);

      static std::vector<int> testNotesForType(const std::string& type);
      static float targetRmsForConfig(const json* config);

    private:
      void load(const json& state);

      std::unique_ptr<HeadlessSynth> synth_;
      json init_state_;
  };

} // namespace draco
