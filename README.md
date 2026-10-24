# Draco Gen

AI-assisted wavetable synth + MIDI generator for hybrid trap, tearout and dark
bass producers. VST3 / AU / Standalone. Based on the open-source
[Vital](https://github.com/mtytel/vital) synthesizer. GPL-3.0.

## Building

Requirements: CMake 3.15+, a C++17 compiler (Xcode 15, Visual Studio 2022, GCC 11+ or Clang 14+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

macOS universal: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
Linux needs `libasound2-dev libfreetype-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxext-dev libgl1-mesa-dev libcurl4-openssl-dev libgtk-3-dev`.

Outputs land in `build/DracoGen_artefacts/Release/{VST3,AU,Standalone}` and the
headless tool in `build/draco_render_artefacts/Release/draco_render`.

### Headless renderer

```sh
draco_render preset.draco -o out.wav -m C1,C3,C5 -l 3   # render a preset
```

## CI

`.github/workflows/build.yml` builds Linux, macOS (universal) and Windows on
every push and uploads the bundles as artifacts. macOS signing/notarization
turns on when these repository secrets exist:

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE_P12` | base64 of a Developer ID Application `.p12` |
| `MACOS_CERTIFICATE_PASSWORD` | password of that `.p12` |
| `MACOS_SIGNING_IDENTITY` | e.g. `Developer ID Application: Name (TEAMID)` |
| `NOTARY_APPLE_ID` / `NOTARY_PASSWORD` / `NOTARY_TEAM_ID` | Apple ID, app-specific password, team ID |

Without them the macOS bundles are ad-hoc signed.

## License

GPL-3.0 — see `LICENSE` and `NOTICE.md`. Draco Gen is not affiliated with
Vital Audio.
