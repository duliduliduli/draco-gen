# Generator configs

Each `*.json` file in this folder is embedded in the plugin and defines one
**type + genre** pair for the patch generator. The Type and Genre menus list
whatever configs exist, so adding a file adds an option.

```json
{
  "format": "draco-generator-config",
  "version": 1,
  "type": "Bass",
  "genre": "Tearout",
  "preset_style": "Bass",
  "tags": ["mono"],
  "loudness": { "target_rms_db": -13.0 },
  "base": { ...sections... },
  "variants": [ { "name": "Growl", "weight": 3, "tags": ["growl"], ...overrides... } ]
}
```

Generating picks one variant by weight and applies its overrides on top of
`base` with JSON merge-patch rules (objects merge, `null` removes an entry).
Every random choice comes from the seed, so a seed always rebuilds the same
patch.

## Value specs

- **Range**: a number, or `[min, max]` (uniform). Integer parameters round.
- **Choice**: a string, or a list of strings (repeat an entry to weight it).
- **chance**: probability (0-1) that a section is used at all.

Units are in the key name: `_s` seconds, `_hz` Hz, `_db` dB, `_percent`
percent. Everything else is the raw Vital parameter value.

## Sections

| Section | Keys |
| --- | --- |
| `voice` | `polyphony`, `legato`, `glide_chance`, `glide_s`, `glide_slope`, `glide_always`, `pitch_bend_range`, `velocity_track`, `volume_db`, `stereo_routing`, `oversampling` |
| `oscillators` `"1"`..`"3"` | `chance`, `wavetables` (choice of names below), `level`, `transpose`, `tune`, `pan`, `unison_voices`, `unison_detune_percent`, `unison_blend`, `stereo_spread`, `detune_power`, `wave_frame` (0-256), `frame_spread`, `random_phase`, `phase`, `stack_style`, `spectral_morph`, `spectral_morph_amount`, `distortion`, `distortion_amount`, `destination` |
| `filters` `"1"`, `"2"` | `chance`, `model`, `style` (names or a numeric range), `cutoff_hz`, `resonance`, `drive_db`, `blend`, `keytrack`, `mix`, `formant_x`, `formant_y`, `formant_resonance`, `blend_transpose` |
| `envelopes` `"1"`..`"6"` | `delay_s`, `attack_s`, `hold_s`, `decay_s`, `sustain`, `release_s`, `attack_power`, `decay_power`, `release_power`. Envelope 1 is the amp envelope. |
| `lfos` `"1"`..`"8"` | `chance`, `shape`, `tempo` (`"1/16"`, `"1/8T"` triplet, `"1/4D"` dotted) or `rate_hz`, `sync_type`, `phase`, `fade_s`, `delay_s`, `smooth_mode`, `stereo` |
| `modulations` `{ "name": {...} }` | `chance`, `source` (`lfo_1`, `env_2`, `velocity`, `macro_control_1`, ...), `destination` (any Vital parameter), `amount` (fraction of the destination's range, -1..1), `bipolar`, `power`, `stereo` |
| `macros` `"1"`..`"4"` | `name`, `value` |
| `raw` | any Vital parameter name to a range of raw values |

A modulation is skipped when its LFO, oscillator or filter is not active, and
FM/RM warps fall back to Bend when the oscillator they read from is off.

### Names

- Wavetables (`WavetableFactory`): `dragon_saw`, `fold_sine`, `fm_growl`,
  `sync_screech`, `vowel_growl`, `pwm_hollow`, `harsh_digital`, `stack_saw`,
  `soft_lead`, `sub_sine`, `warm_saw`. All are computed from math in
  `src/generator/wavetable_factory.cpp`.
- LFO shapes: `triangle`, `sine`, `saw_down`, `saw_up`, `square`, `pluck`,
  `wobble`, `yoi`, `stepped`, `chop`.
- Distortion (warp): `None`, `Sync`, `Formant`, `Quantize`, `Bend`, `Squeeze`,
  `Pulse`, `FM A`, `FM B`, `RM A`, `RM B` (A/B = the next/previous oscillator).
- Spectral morph, filter model/style, stack style and sync type use the names
  shown in the synth's UI.
- Destinations: `Filter 1`, `Filter 2`, `Filter 1+2`, `Output`, `Direct Out`.

## Playability

`PatchAuditor` renders each generated patch offline at two test notes. Patches
that are silent, produce non-finite output, or differ by more than 18 dB
between the test notes are regenerated from a derived seed. The master volume
(and oscillator levels when needed) is then adjusted so the loudest 50 ms of
each note averages `loudness.target_rms_db`. The output limiter keeps every
patch at or below -1 dBFS.

Check every config with:

```sh
draco_render --audit --count 40
```
