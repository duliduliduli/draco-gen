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
#include "synth_section.h"
#include "patch_generator.h"
#include "patch_history.h"

#include <atomic>

class OpenGlToggleButton;
class PlainTextComponent;

namespace draco {
  class PatchAuditor;
}

// The "Generator" strip: pick a sound type and genre, then Generate / Previous /
// Next through generated patches and Save the one you like as a tagged preset.
class GeneratorSection : public SynthSection {
  public:
    static constexpr int kDefaultHeight = 40;

    GeneratorSection(const String& name);
    virtual ~GeneratorSection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { paintTabShadow(g); }
    void resized() override;
    void buttonClicked(Button* clicked_button) override;

    void generate();
    void showPrevious();
    void showNext();
    void saveCurrent();

    const std::string& getType() const { return type_; }
    const std::string& getGenre() const { return genre_; }
    void setTypeAndGenre(const std::string& type, const std::string& genre);

  private:
    void showTypeMenu();
    void showGenreMenu();
    void loadResult(const draco::PatchGenerator::Result& result);
    void finishGeneration(draco::PatchGenerator::Result result);
    void updateText();
    void setStatus(const String& status);

    draco::PatchGenerator generator_;
    draco::PatchHistory history_;
    std::unique_ptr<draco::PatchAuditor> auditor_;
    ThreadPool worker_;
    std::atomic<bool> busy_;

    std::string type_;
    std::string genre_;
    String status_;

    std::unique_ptr<PlainTextComponent> title_;
    std::unique_ptr<OpenGlToggleButton> type_button_;
    std::unique_ptr<OpenGlToggleButton> genre_button_;
    std::unique_ptr<OpenGlToggleButton> previous_button_;
    std::unique_ptr<OpenGlToggleButton> generate_button_;
    std::unique_ptr<OpenGlToggleButton> next_button_;
    std::unique_ptr<OpenGlToggleButton> save_button_;
    std::unique_ptr<PlainTextComponent> status_text_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneratorSection)
};
