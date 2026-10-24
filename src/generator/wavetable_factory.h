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
#include <string>
#include <vector>

using json = nlohmann::json;

namespace draco {

  // Builds Draco Gen's own procedural wavetables. Every table is computed from
  // math (additive, wavefolding, FM, hard sync, ...) so no third party wave data
  // is involved. The output is the Vital-format wavetable JSON stored in presets.
  class WavetableFactory {
    public:
      static constexpr int kFrameSize = 2048;
      static constexpr int kMaxPosition = 255;

      static const std::vector<std::string>& names();
      static bool exists(const std::string& name);

      // Returns a wavetable JSON object. The seed only varies the details of a
      // family (formant order, FM ratio, ...), so the same name always gives a
      // recognisably similar table.
      static json create(const std::string& name, uint32_t seed);

      // Renders a single frame of a family at morph position t in [0, 1].
      // Exposed for tests.
      static std::vector<float> renderFrame(const std::string& name, float t, uint32_t seed);
  };

} // namespace draco
