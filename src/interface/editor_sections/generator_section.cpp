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

#include "generator_section.h"

#include "fonts.h"
#include "load_save.h"
#include "open_gl_image_component.h"
#include "patch_auditor.h"
#include "skin.h"
#include "synth_base.h"
#include "synth_button.h"
#include "synth_gui_interface.h"

GeneratorSection::GeneratorSection(const String& name) : SynthSection(name), worker_(1), busy_(false) {
  std::vector<std::string> types = generator_.getTypes();
  type_ = std::find(types.begin(), types.end(), "Bass") != types.end() ? "Bass" : (types.empty() ? "" : types[0]);
  std::vector<std::string> genres = generator_.getGenresForType(type_);
  genre_ = std::find(genres.begin(), genres.end(), "Tearout") != genres.end() ? "Tearout" :
           (genres.empty() ? "" : genres[0]);

  title_ = std::make_unique<PlainTextComponent>("generator title", "GENERATOR");
  addOpenGlComponent(title_.get());
  title_->setFontType(PlainTextComponent::kRegular);
  title_->setJustification(Justification::centredLeft);

  auto makeButton = [this](std::unique_ptr<OpenGlToggleButton>& button, const String& name, const String& text,
                           bool primary) {
    button = std::make_unique<OpenGlToggleButton>(name);
    button->setUiButton(primary);
    button->setText(text);
    button->setClickingTogglesState(false);
    addButton(button.get());
  };

  makeButton(type_button_, "generator_type", "", false);
  makeButton(genre_button_, "generator_genre", "", false);
  makeButton(previous_button_, "generator_previous", "<", false);
  makeButton(generate_button_, "generator_generate", "GENERATE", true);
  makeButton(next_button_, "generator_next", ">", false);
  makeButton(save_button_, "generator_save", "SAVE", false);

  status_text_ = std::make_unique<PlainTextComponent>("generator status", "");
  addOpenGlComponent(status_text_.get());
  status_text_->setFontType(PlainTextComponent::kLight);
  status_text_->setJustification(Justification::centredLeft);

  status_ = "Pick a type and genre, then hit Generate.";
  setSkinOverride(Skin::kKeyboard);
  updateText();
}

GeneratorSection::~GeneratorSection() {
  worker_.removeAllJobs(true, 5000);
}

void GeneratorSection::paintBackground(Graphics& g) {
  paintBody(g);
  paintBorder(g);
  paintOpenGlChildrenBackgrounds(g);
}

void GeneratorSection::resized() {
  int height = getHeight();
  int margin = getWidgetMargin();
  int padding = getPadding();
  int button_height = height - 2 * margin;
  float ratio = getSizeRatio();

  int x = margin + padding;
  int title_width = 92 * ratio;
  title_->setBounds(x, 0, title_width, height);
  title_->setTextSize(13.0f * ratio);
  x += title_width + margin;

  int selector_width = 150 * ratio;
  type_button_->setBounds(x, margin, selector_width, button_height);
  x += selector_width + margin;
  genre_button_->setBounds(x, margin, selector_width + 20 * ratio, button_height);
  x += selector_width + 20 * ratio + 2 * margin;

  int arrow_width = 34 * ratio;
  int generate_width = 120 * ratio;
  previous_button_->setBounds(x, margin, arrow_width, button_height);
  x += arrow_width + margin;
  generate_button_->setBounds(x, margin, generate_width, button_height);
  x += generate_width + margin;
  next_button_->setBounds(x, margin, arrow_width, button_height);
  x += arrow_width + 2 * margin;

  int save_width = 80 * ratio;
  save_button_->setBounds(x, margin, save_width, button_height);
  x += save_width + 2 * margin;

  status_text_->setBounds(x, 0, std::max(0, getWidth() - x - margin), height);
  status_text_->setTextSize(12.0f * ratio);

  SynthSection::resized();
}

void GeneratorSection::buttonClicked(Button* clicked_button) {
  if (clicked_button == type_button_.get())
    showTypeMenu();
  else if (clicked_button == genre_button_.get())
    showGenreMenu();
  else if (clicked_button == generate_button_.get())
    generate();
  else if (clicked_button == previous_button_.get())
    showPrevious();
  else if (clicked_button == next_button_.get())
    showNext();
  else if (clicked_button == save_button_.get())
    saveCurrent();
}

void GeneratorSection::setTypeAndGenre(const std::string& type, const std::string& genre) {
  type_ = type;
  std::vector<std::string> genres = generator_.getGenresForType(type_);
  if (std::find(genres.begin(), genres.end(), genre) != genres.end())
    genre_ = genre;
  else if (std::find(genres.begin(), genres.end(), genre_) == genres.end())
    genre_ = genres.empty() ? "" : genres[0];
  updateText();
}

void GeneratorSection::showTypeMenu() {
  std::vector<std::string> types = generator_.getTypes();
  PopupItems options;
  for (int i = 0; i < (int)types.size(); ++i)
    options.addItem(i, types[i], types[i] == type_);

  showPopupSelector(this, type_button_->getBounds().getBottomLeft(), options, [=](int selection) {
    if (selection >= 0 && selection < (int)types.size())
      setTypeAndGenre(types[selection], genre_);
  });
}

void GeneratorSection::showGenreMenu() {
  std::vector<std::string> genres = generator_.getGenresForType(type_);
  PopupItems options;
  for (int i = 0; i < (int)genres.size(); ++i)
    options.addItem(i, genres[i], genres[i] == genre_);

  showPopupSelector(this, genre_button_->getBounds().getBottomLeft(), options, [=](int selection) {
    if (selection >= 0 && selection < (int)genres.size())
      setTypeAndGenre(type_, genres[selection]);
  });
}

void GeneratorSection::generate() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr || busy_ || type_.empty() || genre_.empty())
    return;

  busy_ = true;
  setStatus("Generating " + String(genre_) + " " + String(type_) + "...");

  // Only the sample data of the current patch is reused (see PatchGenerator::generate).
  json base_state = parent->getSynth()->saveToJson();
  uint32_t seed = (uint32_t)Random::getSystemRandom().nextInt();
  std::string type = type_;
  std::string genre = genre_;
  Component::SafePointer<GeneratorSection> safe_this(this);

  worker_.addJob([this, safe_this, base_state, seed, type, genre] {
    if (auditor_ == nullptr)
      auditor_ = std::make_unique<draco::PatchAuditor>();
    draco::PatchGenerator::Result result = auditor_->generatePlayable(generator_, type, genre, seed, base_state);
    MessageManager::callAsync([safe_this, result] {
      if (GeneratorSection* section = safe_this.getComponent())
        section->finishGeneration(result);
    });
  });
}

void GeneratorSection::finishGeneration(draco::PatchGenerator::Result result) {
  busy_ = false;
  history_.push(std::move(result));
  loadResult(*history_.current());
}

void GeneratorSection::showPrevious() {
  if (busy_)
    return;
  const draco::PatchGenerator::Result* result = history_.previous();
  if (result)
    loadResult(*result);
}

void GeneratorSection::showNext() {
  if (busy_)
    return;
  const draco::PatchGenerator::Result* result = history_.next();
  if (result)
    loadResult(*result);
  else
    generate();
}

void GeneratorSection::loadResult(const draco::PatchGenerator::Result& result) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  SynthBase* synth = parent->getSynth();
  synth->loadFromJson(result.state);
  synth->clearActiveFile();
  parent->updateFullGui();
  parent->notifyFresh();

  String position = String(history_.index() + 1) + "/" + String(history_.size());
  String variant = result.variant.empty() ? "" : String(result.variant) + "  ";
  setStatus(position + "   " + String(result.name) + "   " + variant + "seed " + String(result.seed));
}

void GeneratorSection::saveCurrent() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;

  // Save what is playing now, including any tweaks made after generating.
  SynthBase* synth = parent->getSynth();
  draco::PatchGenerator::Result to_save;
  const draco::PatchGenerator::Result* current = history_.current();
  if (current) {
    to_save = *current;
  }
  else {
    to_save.type = type_;
    to_save.genre = genre_;
    to_save.name = synth->getPresetName().isEmpty() ? genre_ + " " + type_ : synth->getPresetName().toStdString();
    to_save.tags = { type_, genre_ };
  }

  json state = synth->saveToJson();
  state["author"] = "Draco Gen";
  state["preset_style"] = to_save.type == "FX/Zap" ? "SFX" : to_save.type;
  if (current) {
    state["comments"] = current->state.value("comments", "");
    state["draco"] = current->state.value("draco", json::object());
    for (int i = 1; i <= vital::kNumMacros; ++i) {
      std::string key = "macro" + std::to_string(i);
      if (current->state.count(key))
        state[key] = current->state[key];
    }
  }
  else {
    state["draco"] = { { "type", to_save.type }, { "genre", to_save.genre }, { "tags", to_save.tags } };
  }
  to_save.state = state;

  File file = draco::PatchSaver::save(to_save, draco::PatchSaver::defaultFolder());
  if (file == File()) {
    setStatus("Could not save the preset.");
    return;
  }

  synth->setPresetName(file.getFileNameWithoutExtension());
  parent->externalPresetLoaded(file);
  setStatus("Saved " + file.getParentDirectory().getParentDirectory().getFileName() + "/" +
            file.getParentDirectory().getFileName() + "/" + file.getFileName());
}

void GeneratorSection::updateText() {
  type_button_->setText("TYPE:  " + String(type_).toUpperCase());
  genre_button_->setText("GENRE:  " + String(genre_).toUpperCase());
  status_text_->setText(status_);
}

void GeneratorSection::setStatus(const String& status) {
  status_ = status;
  status_text_->setText(status_);
}
