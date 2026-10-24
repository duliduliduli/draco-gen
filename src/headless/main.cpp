/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "JuceHeader.h"
#include "load_save.h"
#include "tuning.h"
#include "synth_base.h"
#include "patch_auditor.h"
#include "patch_generator.h"
#include "patch_history.h"

#include <iomanip>

String getArgumentValue(int argc, const char* argv[], const String& flag, const String& full_flag) {
  for (int i = 0; i < argc - 1; ++i) {
    std::string arg = argv[i];
    if (arg == flag || arg == full_flag)
      return argv[i + 1];
  }
  
  return "";
}

bool hasFlag(int argc, const char* argv[], const String& flag, const String& full_flag) {
  for (int i = 0; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == flag || arg == full_flag)
      return true;
  }

  return false;
}

float getRenderLength(int argc, const char* argv[]) {
  static constexpr float kDefaultRenderLength = 5.0f;
  static constexpr float kMaxRenderLength = 15.0f;
  
  String string_length = getArgumentValue(argc, argv, "-l", "--length");
  float length = kDefaultRenderLength;
  if (string_length.isEmpty())
    return kDefaultRenderLength;
  
  float float_val = string_length.getFloatValue();
  if (float_val > 0.0f)
    length = std::min(float_val, kMaxRenderLength);
  
  return length;
}

std::vector<int> getRenderMidiNotes(int argc, const char* argv[]) {
  static constexpr int kDefaultMidiNote = 48;
  
  String string_midi = getArgumentValue(argc, argv, "-m", "--midi");
  std::vector<int> midi_notes;
  if (!string_midi.isEmpty()) {
    StringArray midi_tokens;
    midi_tokens.addTokens(string_midi, ",", "");
    
    for (const String& midi_token : midi_tokens) {
      int midi = Tuning::noteToMidiKey(midi_token);
      if (midi >= 0)
        midi_notes.push_back(midi);
    }
  }
  
  if (midi_notes.empty())
    midi_notes.push_back(kDefaultMidiNote);
  
  return midi_notes;
}

float getRenderBpm(int argc, const char* argv[]) {
  static constexpr float kDefaultBpm = 120.0f;
  static constexpr float kMinBpm = 5.0f;
  static constexpr float kMaxBpm = 900.0f;

  String string_length = getArgumentValue(argc, argv, "-b", "--bpm");
  float bpm = kDefaultBpm;
  if (string_length.isEmpty())
    return kDefaultBpm;

  bpm = std::min(string_length.getFloatValue(), kMaxBpm);
  return std::max(bpm, kMinBpm);
}

void doRenderToFile(HeadlessSynth& headless_synth, int argc, const char* argv[]) {
  String string_output_file = getArgumentValue(argc, argv, "-o", "--output");
  bool render_images = hasFlag(argc, argv, "-i", "--render-images");

  if (string_output_file.isEmpty())
    return;
  
  File output_file = File::getCurrentWorkingDirectory().getChildFile(string_output_file);
  output_file.getParentDirectory().createDirectory();
  if (!output_file.hasWriteAccess()) {
    std::cout << "Error: Don't have permission to write output file." << newLine;
    return;
  }

  float length = getRenderLength(argc, argv);
  float bpm = getRenderBpm(argc, argv);
  std::vector<int> midi_notes = getRenderMidiNotes(argc, argv);
  
  headless_synth.renderAudioToFile(output_file, length, bpm, midi_notes, render_images);
}


namespace {
  // Short phrases used to demo generated patches (D minor, 140 BPM).
  std::vector<SynthBase::RenderNote> demoPhrase(const std::string& type, float bpm) {
    double beat = 60.0 / bpm;
    std::vector<SynthBase::RenderNote> notes;
    if (type == "Bass") {
      // Root hits with a slide up and an octave drop, like a tearout / trap drop riff.
      const int pattern[][3] = {
        // start (1/8ths), length (1/8ths), note
        { 0, 3, 38 }, { 3, 1, 38 }, { 4, 2, 41 }, { 6, 2, 36 },
        { 8, 3, 38 }, { 11, 1, 50 }, { 12, 2, 38 }, { 14, 2, 45 },
      };
      for (const auto& step : pattern)
        notes.push_back({ step[0] * beat * 0.5, step[1] * beat * 0.5 * 0.95, step[2], 0.85f });
    }
    else {
      const int pattern[][3] = {
        { 0, 2, 62 }, { 2, 1, 65 }, { 3, 1, 69 }, { 4, 3, 70 }, { 7, 1, 69 },
        { 8, 2, 67 }, { 10, 2, 65 }, { 12, 4, 62 },
      };
      for (const auto& step : pattern)
        notes.push_back({ step[0] * beat * 0.5, step[1] * beat * 0.5 * 0.95, step[2], 0.8f });
    }
    return notes;
  }

  bool writeWav(const File& file, const AudioSampleBuffer& buffer, int sample_rate) {
    file.deleteFile();
    std::unique_ptr<FileOutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
      return false;
    WavAudioFormat wav_format;
    std::unique_ptr<AudioFormatWriter> writer(wav_format.createWriterFor(stream.get(), sample_rate, 2, 16, {}, 0));
    if (writer == nullptr)
      return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
  }

  std::string formatDb(float db) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << db;
    return stream.str();
  }

  // draco_render --generate --type Bass --genre Tearout [--count 10] [--seed 1] [--out-dir dir] [--wav]
  int runGenerate(int argc, const char* argv[]) {
    draco::PatchGenerator generator;
    String type = getArgumentValue(argc, argv, "-t", "--type");
    String genre = getArgumentValue(argc, argv, "-g", "--genre");
    int count = std::max(1, getArgumentValue(argc, argv, "-n", "--count").getIntValue());
    String seed_text = getArgumentValue(argc, argv, "-s", "--seed");
    uint32_t seed = seed_text.isEmpty() ? 1u : (uint32_t)seed_text.getLargeIntValue();
    String out_text = getArgumentValue(argc, argv, "-d", "--out-dir");
    File out_dir = File::getCurrentWorkingDirectory().getChildFile(out_text.isEmpty() ? "generated" : out_text);
    bool write_wav = hasFlag(argc, argv, "-w", "--wav");
    float bpm = 140.0f;

    std::vector<std::pair<std::string, std::string>> pairs;
    for (const std::string& t : generator.getTypes()) {
      for (const std::string& g : generator.getGenresForType(t)) {
        bool type_match = type.isEmpty() || type.equalsIgnoreCase(String(t));
        bool genre_match = genre.isEmpty() || genre.equalsIgnoreCase(String(g));
        if (type_match && genre_match)
          pairs.emplace_back(t, g);
      }
    }
    if (pairs.empty()) {
      std::cout << "No generator config matches that type/genre." << std::endl;
      return 1;
    }

    out_dir.createDirectory();
    draco::PatchAuditor auditor;
    HeadlessSynth renderer;
    json manifest = json::array();
    int index = 0;
    for (int i = 0; i < count; ++i) {
      const auto& pair = pairs[i % pairs.size()];
      uint32_t patch_seed = seed + i;
      draco::PatchAuditor::Report report;
      draco::PatchGenerator::Result result = auditor.generatePlayable(generator, pair.first, pair.second,
                                                                       patch_seed, json(), &report);
      ++index;
      String number = String(index).paddedLeft('0', 2);
      String base_name = number + " - " + String(result.genre) + " " + String(result.type) + " - " +
                         String(result.name);
      base_name = File::createLegalFileName(base_name);
      File preset_file = out_dir.getChildFile(base_name).withFileExtension(vital::kPresetExtension);
      json state = result.state;
      state["preset_name"] = result.name;
      preset_file.replaceWithText(state.dump(2));

      json entry = {
        { "file", preset_file.getFileName().toStdString() },
        { "name", result.name }, { "type", result.type }, { "genre", result.genre },
        { "variant", result.variant }, { "seed", result.seed }, { "tags", result.tags },
        { "rms_db", report.rms_db }, { "peak_db", report.peak_db }, { "playable", report.playable() },
      };

      if (write_wav) {
        renderer.loadFromJson(state);
        std::vector<SynthBase::RenderNote> phrase = demoPhrase(result.type, bpm);
        double length = 0.0;
        for (const auto& note : phrase)
          length = std::max(length, note.start_seconds + note.length_seconds);
        AudioSampleBuffer buffer(2, (int)((length + 0.6) * draco::PatchAuditor::kSampleRate));
        buffer.clear();
        renderer.renderSequenceToBuffer(buffer, draco::PatchAuditor::kSampleRate, bpm, phrase);
        File wav_file = out_dir.getChildFile(base_name).withFileExtension("wav");
        writeWav(wav_file, buffer, draco::PatchAuditor::kSampleRate);
        entry["wav"] = wav_file.getFileName().toStdString();
      }

      std::cout << number << "  " << std::left << std::setw(12) << result.genre << std::setw(6) << result.type
                << std::setw(22) << result.name << " variant=" << std::setw(11) << result.variant
                << " seed=" << std::setw(6) << result.seed << " rms=" << formatDb(report.rms_db)
                << "dB peak=" << formatDb(report.peak_db) << "dB" << (report.playable() ? "" : "  NOT PLAYABLE")
                << std::endl;
      manifest.push_back(entry);
    }

    out_dir.getChildFile("manifest.json").replaceWithText(manifest.dump(2));
    return 0;
  }

  // draco_render --audit [--count 25]: checks every config over many seeds.
  int runAudit(int argc, const char* argv[]) {
    draco::PatchGenerator generator;
    draco::PatchAuditor auditor;
    int count = getArgumentValue(argc, argv, "-n", "--count").getIntValue();
    if (count <= 0)
      count = 25;

    int failures = 0;
    for (const std::string& type : generator.getTypes()) {
      for (const std::string& genre : generator.getGenresForType(type)) {
        float min_rms = 0.0f, max_rms = -200.0f, max_peak = -200.0f;
        int not_playable = 0;
        for (int i = 0; i < count; ++i) {
          draco::PatchAuditor::Report report;
          auditor.generatePlayable(generator, type, genre, 1000u + i, json(), &report);
          if (!report.playable())
            not_playable++;
          min_rms = std::min(min_rms, report.rms_db);
          max_rms = std::max(max_rms, report.rms_db);
          max_peak = std::max(max_peak, report.peak_db);
        }
        bool ok = not_playable == 0 && max_peak <= -0.9f;
        failures += ok ? 0 : 1;
        std::cout << (ok ? "PASS " : "FAIL ") << std::left << std::setw(6) << type << std::setw(13) << genre
                  << " patches=" << count << " unplayable=" << not_playable << " rms=[" << formatDb(min_rms)
                  << ", " << formatDb(max_rms) << "] dB  max_peak=" << formatDb(max_peak) << " dB" << std::endl;
      }
    }
    return failures == 0 ? 0 : 1;
  }

  int runList() {
    draco::PatchGenerator generator;
    for (const std::string& type : generator.getTypes()) {
      for (const std::string& genre : generator.getGenresForType(type))
        std::cout << type << " / " << genre << std::endl;
    }
    return 0;
  }
} // namespace

bool loadFromCommandLine(HeadlessSynth& synth, const String& command_line) {
  String file_path = command_line;
  if (file_path[0] == '"' && file_path[file_path.length() - 1] == '"')
    file_path = command_line.substring(1, command_line.length() - 1);
  File file = File::getCurrentWorkingDirectory().getChildFile(file_path);
  if (!file.exists())
    return false;
  
  std::string error;
  synth.loadFromFile(file, error);
  return true;
}

int main(int argc, const char* argv[]) {
  ScopedJuceInitialiser_GUI juce_init;

  if (hasFlag(argc, argv, "--list", "--list"))
    return runList();
  if (hasFlag(argc, argv, "--audit", "--audit"))
    return runAudit(argc, argv);
  if (hasFlag(argc, argv, "--generate", "--generate"))
    return runGenerate(argc, argv);

  HeadlessSynth headless_synth;
  
  bool last_arg_was_option = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg != "" && arg[0] != '-' && !last_arg_was_option && loadFromCommandLine(headless_synth, arg))
      break;

    last_arg_was_option = arg[0] == '-' && arg != "--headless";
  }
  
  doRenderToFile(headless_synth, argc, argv);
  return 0;
}
