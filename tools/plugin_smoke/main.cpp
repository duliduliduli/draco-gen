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

// Loads a built Draco Gen plugin (VST3 or AU) in a minimal JUCE host, plays a
// note and checks that it makes sound and survives a state round trip.
//
//   draco_plugin_smoke "<path to Draco Gen.vst3 or .component>"

#include "JuceHeader.h"

#include <iostream>

int main(int argc, char* argv[]) {
  ScopedJuceInitialiser_GUI juce_init;
  if (argc < 2) {
    std::cout << "usage: draco_plugin_smoke <plugin path>" << std::endl;
    return 2;
  }

  AudioPluginFormatManager formats;
  formats.addDefaultFormats();

  String path = File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName();
  OwnedArray<PluginDescription> found;
  for (AudioPluginFormat* format : formats.getFormats())
    format->findAllTypesForFile(found, path);

  if (found.isEmpty()) {
    std::cout << "FAIL: no plugin found in " << path << std::endl;
    return 1;
  }

  const PluginDescription& description = *found[0];
  std::cout << "found: " << description.name << " (" << description.pluginFormatName << ", "
            << description.manufacturerName << ", synth=" << (description.isInstrument ? "yes" : "no") << ")"
            << std::endl;

  constexpr double kSampleRate = 44100.0;
  constexpr int kBlockSize = 512;
  String error;
  std::unique_ptr<AudioPluginInstance> plugin = formats.createPluginInstance(description, kSampleRate,
                                                                            kBlockSize, error);
  if (plugin == nullptr) {
    std::cout << "FAIL: could not create instance: " << error << std::endl;
    return 1;
  }

  plugin->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
  plugin->prepareToPlay(kSampleRate, kBlockSize);
  std::cout << "parameters: " << plugin->getParameters().size() << std::endl;

  AudioBuffer<float> buffer(2, kBlockSize);
  double sum = 0.0;
  float peak = 0.0f;
  int total = 0;
  for (int block = 0; block < 100; ++block) {
    MidiBuffer midi;
    if (block == 2)
      midi.addEvent(MidiMessage::noteOn(1, 48, (uint8)100), 0);
    if (block == 60)
      midi.addEvent(MidiMessage::noteOff(1, 48), 0);
    buffer.clear();
    plugin->processBlock(buffer, midi);
    for (int channel = 0; channel < 2; ++channel) {
      const float* data = buffer.getReadPointer(channel);
      for (int i = 0; i < kBlockSize; ++i) {
        sum += data[i] * data[i];
        peak = std::max(peak, std::abs(data[i]));
      }
    }
    total += 2 * kBlockSize;
  }
  double rms = std::sqrt(sum / total);
  std::cout << "rms: " << rms << "  peak: " << peak << std::endl;

  MemoryBlock state;
  plugin->getStateInformation(state);
  plugin->setStateInformation(state.getData(), (int)state.getSize());
  std::cout << "state bytes: " << state.getSize() << std::endl;

  bool has_editor = plugin->hasEditor();
  plugin->releaseResources();
  plugin = nullptr;

  if (rms < 1e-4 || !std::isfinite(rms)) {
    std::cout << "FAIL: plugin is silent" << std::endl;
    return 1;
  }
  if (peak > 0.9f) {
    std::cout << "FAIL: output above the limiter ceiling" << std::endl;
    return 1;
  }
  if (!has_editor) {
    std::cout << "FAIL: plugin has no editor" << std::endl;
    return 1;
  }
  std::cout << "PASS" << std::endl;
  return 0;
}
