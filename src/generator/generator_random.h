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

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace draco {

  // Deterministic random source. std::mt19937's output sequence is fixed by the
  // standard, but the <random> distributions are not, so values are derived here
  // by hand to make a seed produce the same patch on every platform.
  class GeneratorRandom {
    public:
      explicit GeneratorRandom(uint32_t seed) : engine_(seed) { }

      // Uniform in [0, 1).
      double unit() {
        return (engine_() >> 5) * (1.0 / 134217728.0);
      }

      double uniform(double min, double max) {
        return min + (max - min) * unit();
      }

      int integer(int min, int max) {
        if (max <= min)
          return min;
        int value = min + static_cast<int>(unit() * (max - min + 1));
        return value > max ? max : value;
      }

      bool chance(double probability) {
        return unit() < probability;
      }

      template<typename T>
      const T& pick(const std::vector<T>& options) {
        return options[integer(0, static_cast<int>(options.size()) - 1)];
      }

      uint32_t next() { return engine_(); }

    private:
      std::mt19937 engine_;
  };

} // namespace draco
