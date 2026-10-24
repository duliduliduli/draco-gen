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

#include <deque>

namespace draco {

  // Generated patches in the order they were made, so Previous/Next can walk
  // back and forth through them.
  class PatchHistory {
    public:
      static constexpr int kMaxEntries = 200;

      void push(PatchGenerator::Result result) {
        entries_.push_back(std::move(result));
        while ((int)entries_.size() > kMaxEntries)
          entries_.pop_front();
        index_ = (int)entries_.size() - 1;
      }

      bool empty() const { return entries_.empty(); }
      int size() const { return (int)entries_.size(); }
      int index() const { return index_; }
      bool hasPrevious() const { return index_ > 0; }
      bool hasNext() const { return index_ >= 0 && index_ < size() - 1; }

      const PatchGenerator::Result* current() const {
        return index_ >= 0 && index_ < size() ? &entries_[index_] : nullptr;
      }

      const PatchGenerator::Result* previous() {
        if (!hasPrevious())
          return nullptr;
        return &entries_[--index_];
      }

      const PatchGenerator::Result* next() {
        if (!hasNext())
          return nullptr;
        return &entries_[++index_];
      }

    private:
      std::deque<PatchGenerator::Result> entries_;
      int index_ = -1;
  };

  // Writes generated patches as normal presets with type and genre tags.
  class PatchSaver {
    public:
      // Saves into <folder>/<Type>/<Genre>/ and returns the written file
      // (or File() on failure). The preset name is made unique in that folder.
      static File save(const PatchGenerator::Result& result, const File& folder);
      static File defaultFolder();
  };

} // namespace draco
