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

#include "patch_history.h"

#include "load_save.h"
#include "synth_constants.h"

namespace draco {

  namespace {
    String safeFolderName(const std::string& name) {
      return File::createLegalFileName(String(name).replace("/", "-"));
    }
  } // namespace

  File PatchSaver::defaultFolder() {
    return LoadSave::getUserPresetDirectory().getChildFile("Draco Generated");
  }

  File PatchSaver::save(const PatchGenerator::Result& result, const File& folder) {
    File directory = folder.getChildFile(safeFolderName(result.type)).getChildFile(safeFolderName(result.genre));
    if (!directory.createDirectory())
      return File();

    String base_name = File::createLegalFileName(result.name);
    if (base_name.isEmpty())
      base_name = "Generated";

    File file = directory.getChildFile(base_name).withFileExtension(vital::kPresetExtension);
    for (int i = 2; file.exists(); ++i)
      file = directory.getChildFile(base_name + " " + String(i)).withFileExtension(vital::kPresetExtension);

    json state = result.state;
    state["preset_name"] = file.getFileNameWithoutExtension().toStdString();
    if (!file.replaceWithText(state.dump()))
      return File();
    return file;
  }

} // namespace draco
