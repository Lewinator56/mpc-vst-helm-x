# Helm-X MPC VST

## Project Overview
Helm-X (`HelmX`) is an extended synthesizer project for MPC OS standalone devices (MPC Live, MPC One, MPC X, MPC Key, Force), branched from the Helm engine port (`mpc-vst-helm`) to introduce new features, enhancements, and extended DSP/UI capabilities while maintaining the core Helm synthesizer architecture.
Author / Vendor: **Lewinator56**

## Status & Progress
- **Project Setup & Git**: Initialized from `mpc-vst-helm` base codebase into `mpc-vst-helm-x`. Remote connected to `https://github.com/Lewinator56/mpc-vst-helm-x.git` (`master` branch).
- **Minimal Stereo Signal Path Step**:
  - `HelmOscillators`: Configured dual outputs (`Processor(kNumInputs, 2)`). Split unison accumulators into dual left/right totals (`oscillator1_left_totals_`, `oscillator1_right_totals_`, `oscillator2_left_totals_`, `oscillator2_right_totals_`), routed to `output(0)` (Left) and `output(1)` (Right) using the unison spread and pan gains.
  - `HelmVoiceHandler`: Registered `output_left_` and `output_right_` in constructor so output indices 0 and 1 represent accumulated Left and Right audio. Routed sub-oscillator and noise into dual stereo adders, dual feedback delays (`osc_feedback_left_`, `osc_feedback_right_`), and dual `StateVariableFilter` instances (`filter_left_`, `filter_right_`) directly into `output_left_` and `output_right_`.

- **Stereo Master FX Implementation**:
  - `Distortion`: Dual instances (`distortion_left`, `distortion_right`) processing Left and Right voice audio independently with shared controls (`distortion_on`, `distortion_type`, `distortion_drive`, `distortion_mix`).
  - `Delay`: Dual instances (`delay_left`, `delay_right`) wrapped in separate standard `BypassRouter` containers (`delay_container_left`, `delay_container_right`), preserving full stereo panning while maintaining tempo sync and feedback settings.
  - `DcFilter`: Dual DC blocker filters (`dc_filter_left`, `dc_filter_right`).
  - `Reverb`: Updated `Reverb` to take `kAudioLeft` and `kAudioRight`. Comb/all-pass network receives a sum `(left + right) * 0.5` for decorrelated diffusion, while dry audio maintains full Left/Right stereo separation. Wrapped in dual `BypassRouter` containers (`reverb_container_left`, `reverb_container_right`) to cleanly bypass when inactive without summing to mono.
  - Volume scaling (`scaled_audio_left`, `scaled_audio_right`) and clipping (`clamp_left`, `clamp_right`) preserve stereo separation all the way to audio output.


- **Touch UI Layout Redesign (`layout.conf` [tab OSC])**:
  - Moved **Cross Mod** frame & knob (`cross_modulation`) to the bottom-right corner next to Sub and Noise.
  - Expanded **OSC 1** and **OSC 2** frames to full width (920px).
  - Arranged 3 knobs per row:
    - Top row: `VOICES` (`osc_x_unison_voices`), `DETUNE` (`osc_x_unison_detune`), `SPREAD` (`osc_x_unison_spread`).
    - Bottom row: `TRANSPOSE` (`osc_x_transpose`), `TUNE` (`osc_x_tune`), `PAN` (`osc_x_pan`).
- **Branding & Package Metadata**:
  - Vendor / Author: `Lewinator56`
  - Name: `HelmX`
  - Plugin UID: `HlmX`
  - Release Package: `dist/HelmX-1.0.0-mpc-armv7.zip` containing `Lewinator56 - VST - HelmX` synth bundle.
- **Offline Testing & Bug Fixes**:
  - Identified and resolved load-time crash / segfault caused by `BypassRouter` output initialization:
    - `BypassRouter` instances (`stutter_container`, `formant_container_`, `delay_container`, `reverb_container`) were allocating empty outputs in constructor and then registering additional outputs, resulting in null/unconnected audio output index lookups.
    - Output registrations in `HelmVoiceHandler` constructor were properly aligned.
  - Resolved mono retrigger / voice envelope reset issue: `helm_create` previously loaded `g_patches[0]` on startup (`COA Insane Gamer.helm`, which is a monophonic patch with `polyphony: 1, legato: 1`), causing voice envelopes to restart when notes overlap/release. Updated `helm_adapter.cpp` to explicitly find and load the `"Init"` patch (`polyphony: 6, legato: 0`) as the startup preset.
  - Aligned panning law to 0dB center gain (`left = 1 - max(0, pan), right = 1 + min(0, pan)`) ensuring center-panned oscillators maintain 100% full volume parity with original Helm presets.
  - Aligned `MOD_DESTS` and `params.json` modulation destination tables (appended new destinations 55..58 at the end) so all legacy preset modulation slot mappings 0..54 remain completely undisturbed.
  - Verified ARMv7 build and regenerated release ZIP bundle.

