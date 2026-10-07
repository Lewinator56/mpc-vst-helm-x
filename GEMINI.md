# Helm-X MPC VST

## Project Overview
Helm-X (`HelmX`) is an extended synthesizer project for MPC OS standalone devices (MPC Live, MPC One, MPC X, MPC Key, Force), branched from the Helm engine port (`mpc-vst-helm`) to introduce new features, enhancements, and extended DSP/UI capabilities while maintaining the core Helm synthesizer architecture.
Author / Vendor: **Lewinator56**

## Status & Progress
- **Project Setup & Git**: Initialized from `mpc-vst-helm` base codebase into `mpc-vst-helm-x`. Remote connected to `https://github.com/Lewinator56/mpc-vst-helm-x.git` (`master` branch).
- **Minimal Stereo Signal Path Step**:
  - `HelmOscillators`: Configured dual outputs (`Processor(kNumInputs, 2)`). Split unison accumulators into dual left/right totals (`oscillator1_left_totals_`, `oscillator1_right_totals_`, `oscillator2_left_totals_`, `oscillator2_right_totals_`), routed to `output(0)` (Left) and `output(1)` (Right) using the unison spread and pan gains.
  - `HelmVoiceHandler`: Registered `output_left_` and `output_right_` in constructor so output indices 0 and 1 represent accumulated Left and Right audio. Routed sub-oscillator and noise into dual stereo adders, dual feedback delays (`osc_feedback_left_`, `osc_feedback_right_`), and dual `StateVariableFilter` instances (`filter_left_`, `filter_right_`) directly into `output_left_` and `output_right_`.

- **Stereo Master FX Implementation (Commit `84f9123`)**:
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

- **Dual Filter & Routing Architecture Implementation**:
  - **DSP Pipeline (`helm_filter_router.h`, `helm_oscillators.h`, `helm_voice_handler.h`, `helm_voice_handler.cpp`)**:
    - `HelmOscillators`: Separated outputs into 4 audio channels: `output(0)`: Osc 1 Left, `output(1)`: Osc 1 Right, `output(2)`: Osc 2 Left, `output(3)`: Osc 2 Right.
    - `Filter1InputRouter`: Routes appropriate audio signals (Osc 1, Osc 2, Sub, Noise) to Filter 1 based on `filter_routing` mode (Series 1 > 2, Parallel, Split 1, Split 2) and target selectors (`sub_filter_target`, `noise_filter_target`).
    - `Filter2InputRouter`: Takes Filter 1 stereo output (with `filter_1_pan` applied) and/or direct oscillator components (with Sub/Noise targets) into Filter 2.
    - `FilterOutputMixer`: Applies `filter_2_pan` and mixes Filter 1 and Filter 2 according to routing mode (summed in Parallel; Filter 2 in Series/Split modes) directly into voice amplitude stages.
    - Zero circular dependency DAG: Monotonic processing sequence prevents DSP feedback loops and scheduling conflicts.
    - Fully independent Filter 2 parameters added (`filter_2_on`, `filter_2_style`, `filter_2_shelf`, `filter_2_cutoff`, `filter_2_resonance`, `filter_2_drive`, `filter_2_blend`, `filter_2_env_depth`, `filter_2_keytrack`, `filter_2_pan`).
    - Dedicated pan controls for both filters: `filter_1_pan` and `filter_2_pan`.
  - **Parameters & Modulation (`params.json`, `helm_common.cpp`, `helm_adapter.cpp`)**:
    - Appended new parameters (`filter_1_pan`, `filter_2_*`, `filter_routing`, `sub_filter_target`, `noise_filter_target`) to preserve legacy index alignment.
    - Appended new modulation destinations to `MOD_DESTS` and modulation slot destination dropdowns (indices 58..64).
  - **Touch UI & Signal Path Diagram (`layout.conf`, `tools/gen_routing_diagrams.py`)**:
    - Condensed `[tab FILTER]` into two side-by-side frames: `FILTER 1` (left half, w=624) and `FILTER 2` (right half, w=624), each with cutoff, resonance, drive, blend, env depth, key track, style, shelf, and pan controls.
    - Included `filter_routing` selector directly on `[tab FILTER]`.
    - Removed formant filter controls from the filter page.
    - Added dedicated `[tab ROUTING]` featuring:
      - Fixed enlarged filter positions: Filter 1 (top) and Filter 2 (bottom) enlarged to 140x95px; all generator connection lines enter well inside the filter boxes with uniform 16px pin pitch.
      - Mathematical grid layout: Uniform 20px vertical trunk lane pitch (x=185, 205, 225, 245) and uniform 16px input pin pitch across both filters. Cascade modes (Series, Split 1, Split 2) draw a direct vertical connection DOWN between Filter 1 and Filter 2, while Parallel mode branches symmetrically into a SUM mixer node.
      - Zero wire crossings: sound sources ordered (OSC 1, SUB OSC, NOISE, OSC 2) with dedicated parallel lanes and junction dots, completely eliminating colliding or messy overlapping wires.
      - Master FX chain arranged as a clean vertical stack on the right (`DISTORTION` $\downarrow$ `DELAY` $\downarrow$ `REVERB` $\downarrow$ `STEREO OUT` node), eliminating horizontal overflow.
      - Clean interactive toolbar along the bottom of the diagram panel (`FILTER 1`, `FILTER 2`, `DISTORTION`, `DELAY`, `REVERB`) so buttons never obscure diagram blocks or connections.
  - **Interactive Node Toggle Buttons (`tools/gen_node_buttons.py`, `layout.conf`)**:
    - Replaced generic toggle switches on the ROUTING tab with custom image-based touch buttons matching the exact geometry and dimensions of the diagram nodes:
      - Filter 1: $140 \times 95$ at $cx=662, cy=245$ (`filter_on`)
      - Filter 2: $140 \times 95$ at $cx=662, cy=455$ (`filter_2_on`)
      - Distortion: $140 \times 58$ at $cx=1132, cy=237$ (`distortion_on`)
      - Delay: $140 \times 58$ at $cx=1132, cy=327$ (`delay_on`)
      - Reverb: $140 \times 58$ at $cx=1132, cy=417$ (`reverb_on`)
    - Distinct visual states:
      - **ON (Active)**: Vibrant signature color borders (Cyan for F1, Amber for F2, Red for Distortion, Teal for Delay, Purple for Reverb), bright titles, and illuminated active status badges.
      - **OFF (Bypassed)**: Dark recessed cards, muted gray borders, dimmed typography, and explicit "BYPASS" / "OFF" status indicators.
    - Directly touching any node on the MPC touchscreen or in the skin UI toggles that stage on/off instantly.
    - **Fix for button hex label overwrite**: `tools/helm_paint.py` was previously repainting all `sh_btn_*.png` assets by parsing the filename suffix as a text label, which extracted the 8-character SHA1 look hash (e.g. `CEDECE04`) and drew it over custom button artwork. Updated `helm_paint.py` to skip look-based custom buttons, and updated `shadow_skin.py` to copy image button assets directly into the skin directory.

- **100% Open Filter Fix (Full Bandwidth & Transparent Passthrough)**:
  - **Root Causes Identified**:
    1. Standard Helm clamped `MidiLookup` frequency tables at `MAX_CENTS = 12,800` (MIDI note 128 = 13.2 kHz).
    2. The Cutoff parameter max is `127.0` semitones. Direct linear MIDI note lookup via `cr::MidiScale` capped maximum cutoff frequency at note 127 = 12,543 Hz! Even with the cutoff knob turned all the way to 100%, everything above 12.5 kHz was being rolled off by the lowpass filter.
    3. The bilinear transform in the digital State Variable Filter ($g = \tan(\pi f / f_s)$) introduces a transmission zero at Nyquist frequency ($z = -1$, $f_s / 2$), attenuating top-octave treble.
    4. `StateVariableFilter::tick()` and `tick24db()` apply non-linear saturation (`quickTanh`) on every sample even at nominal 0dB drive, rounding transients and coloring the high end.
  - **DSP Solution Implemented**:
    1. **Extended Frequency Range (`utils.h`)**: Increased `MAX_MIDI_NOTES` to 144 (`MAX_CENTS = 14,400`), expanding lookup capability to 33.5 kHz.
    2. **Cutoff Scaling Curve (`midi_lookup.h`, `operators.h`)**: Added `CutoffScale` (`cr::CutoffScale`). For notes $\le 100$ (up to 2.6 kHz), scaling is 100% identical to classic Helm. For notes $> 100$, a smooth $C^1$ continuous quadratic curve smoothly expands the upper range, reaching note 140 (26.6 kHz) at knob value 127.0.
    3. **Voice Handler Cutoff Routing (`helm_voice_handler.cpp`)**: Replaced `cr::MidiScale` with `cr::CutoffScale` for both `frequency_cutoff_1` and `frequency_cutoff_2`.
    4. **100% Open Transparent Passthrough (`state_variable_filter.cpp`)**:
       - When in Low-Pass mode (`blend <= 0.05`), if cutoff is at maximum ($\ge 22$ kHz or $\ge 0.48 f_s$), the filter engages bit-exact dry audio passthrough (`processAllPass`), producing 0 dB attenuation across the entire audible spectrum, zero phase shift, and zero non-linear compression.
       - A smoothstep crossfade ($16\text{ kHz} \dots 22\text{ kHz}$) ensures seamless, click-free transitions when sweeping the cutoff knob up to 100%.
       - For High-Pass mode (`blend >= 1.95`), the filter smoothly bypasses to dry audio when cutoff reaches minimum ($\le 25$ Hz).
       - Clamped normalized frequency in `computePassCoefficients` and `computeShelfCoefficients` to 0.495 to eliminate tangent singularity near Nyquist.
