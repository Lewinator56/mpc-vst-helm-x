# Helm-X MPC VST

## Project Overview
Helm-X (`HelmX`) is an extended synthesizer project for MPC OS standalone devices (MPC Live, MPC One, MPC X, MPC Key, Force), branched from the Helm engine port (`mpc-vst-helm`) to introduce new features, enhancements, and extended DSP/UI capabilities while maintaining the core Helm synthesizer architecture.
Author / Vendor: **Lewinator56**

## Status & Progress
- **Project Setup & Git**: Initialized from `mpc-vst-helm` base codebase into `mpc-vst-helm-x`. Remote connected to `https://github.com/Lewinator56/mpc-vst-helm-x.git` (`master` branch).
- **DSP Engine Upgrades**:
  - **Stereo Unison Spread & Oscillator Panning**:
    - Enhanced `HelmOscillators` to generate dual-channel stereo output (`left_output`, `right_output`).
    - Added per-oscillator equal-power stereo panning (`osc_1_pan`, `osc_2_pan`).
    - Added unison voice stereo spread (`osc_1_unison_spread`, `osc_2_unison_spread`) spreading detuned unison voices across left and right channels based on their detune offset.
    - Updated `HelmVoiceHandler` voice processing chain to true stereo (stereo sub/noise sum, dual feedback delays, dual state-variable filters, dual stutter instances, dual formant filters, dual voice amp multiplier).
    - Updated `HelmEngine` engine master effects to true stereo (dual distortion, dual delays, dual DC blockers, stereo-input reverb).
    - Added new parameter definitions and modulation destinations (`osc_1_pan`, `osc_2_pan`, `osc_1_unison_spread`, `osc_2_unison_spread`) in `helm_common.cpp`, `params.json`, and `helm_adapter.cpp`.
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

