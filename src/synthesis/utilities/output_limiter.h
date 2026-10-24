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

#include "processor.h"

namespace vital {

  // Stereo-linked safety limiter on the master output. Gain drops instantly when a
  // sample would exceed the ceiling and recovers smoothly, so the output never goes
  // above the ceiling no matter what the generator or the user dials in.
  class OutputLimiter : public Processor {
    public:
      static constexpr mono_float kCeiling = 0.891f; // -1 dBFS
      static constexpr mono_float kReleaseSeconds = 0.15f;

      OutputLimiter();

      virtual Processor* clone() const override { return new OutputLimiter(*this); }
      void process(int num_samples) override;
      void hardReset() override { gain_ = 1.0f; }

      mono_float gain() const { return gain_; }

    private:
      mono_float gain_;

      JUCE_LEAK_DETECTOR(OutputLimiter)
  };
} // namespace vital
