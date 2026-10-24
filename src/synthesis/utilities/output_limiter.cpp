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

#include "output_limiter.h"

#include <cmath>

namespace vital {

  OutputLimiter::OutputLimiter() : Processor(1, 1), gain_(1.0f) { }

  void OutputLimiter::process(int num_samples) {
    const poly_float* source = input(0)->source->buffer;
    poly_float* dest = output()->buffer;

    mono_float release_samples = std::max(1.0f, kReleaseSeconds * getSampleRate());
    mono_float release_mult = 1.0f - std::exp(-1.0f / release_samples);
    mono_float gain = gain_;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = source[i];
      mono_float left = sample[0];
      mono_float right = sample[1];
      mono_float peak = std::max(std::fabs(left), std::fabs(right));

      // Non-finite input would poison the whole chain; output silence instead.
      if (!std::isfinite(peak)) {
        dest[i] = 0.0f;
        continue;
      }

      mono_float target = peak > kCeiling ? kCeiling / peak : 1.0f;
      if (target < gain)
        gain = target;
      else
        gain += (target - gain) * release_mult;

      dest[i] = sample * gain;
    }

    gain_ = gain;
  }
} // namespace vital
