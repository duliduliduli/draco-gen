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

#include "json/json.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace draco {

  // Rule-based patch generator. Each (type, genre) pair has a JSON config listing
  // the allowed range of every parameter it touches, plus weighted "variants"
  // that override parts of the base ranges. See resources/generator/README.md.
  class PatchGenerator {
    public:
      static constexpr int kGeneratorVersion = 1;

      struct Result {
        json state;
        std::string type;
        std::string genre;
        std::string variant;
        std::string name;
        uint32_t seed = 0;
        std::vector<std::string> tags;
      };

      // Loads the configs embedded in the binary.
      PatchGenerator();

      // Adds (or replaces) a config. Returns false and fills error if it is invalid.
      bool addConfig(const json& config, std::string& error);

      std::vector<std::string> getTypes() const;
      std::vector<std::string> getGenres() const;
      std::vector<std::string> getGenresForType(const std::string& type) const;
      bool hasConfig(const std::string& type, const std::string& genre) const;
      const json* getConfig(const std::string& type, const std::string& genre) const;

      // Builds a complete preset (Vital preset JSON). base_state should be a
      // preset JSON from the running synth; only its sample data is reused so
      // the generated patch does not depend on what was loaded before.
      Result generate(const std::string& type, const std::string& genre, uint32_t seed,
                      const json& base_state = json()) const;

      static std::string makeKey(const std::string& type, const std::string& genre);

      // Unit conversions from the config's human units to Vital parameter values.
      static float envelopeSecondsToValue(double seconds);
      static float glideSecondsToValue(double seconds);
      static float lfoHzToValue(double hz);
      static float cutoffHzToValue(double hz);
      static float volumeDbToValue(double db);
      static float detunePercentToValue(double percent);

    private:
      std::vector<std::string> type_order_;
      std::vector<std::string> genre_order_;
      std::map<std::string, json> configs_;
  };

} // namespace draco
