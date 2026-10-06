# Helm-X MPC VST

## Project Overview
Helm-X is an extended synthesizer project for MPC OS standalone devices, branched from the Helm engine port (`mpc-vst-helm`) to introduce new features, enhancements, and extended DSP/UI capabilities while maintaining the core Helm synthesizer architecture.

## Status & Progress
- **Project Setup**: Initialized from `mpc-vst-helm` base codebase into `mpc-vst-helm-x`.
- **Stereo Unison Spread & Oscillator Panning**:
  - Enhanced `HelmOscillators` to generate dual-channel stereo output (Left / Right).
  - Implemented per-oscillator equal-power stereo panning (`osc_1_pan`, `osc_2_pan`).
  - Implemented unison voice stereo spread (`osc_1_unison_spread`, `osc_2_unison_spread`) distributing cloned unison voices symmetrically across Left and Right.
  - Upgraded voice signal chain (`HelmVoiceHandler`) to true stereo (stereo sub/noise sum, dual feedback delays, dual state-variable filters, dual stutter instances, dual formant filters, dual voice amp multiplier).
  - Upgraded engine master effects (`HelmEngine`) to true stereo (dual distortion, dual delays, dual DC blockers, stereo-input reverb).
  - Added new parameter definitions and modulation destinations (`osc_1_pan`, `osc_2_pan`, `osc_1_unison_spread`, `osc_2_unison_spread`).
  - Verified ARM32 cross-compilation and package generation.
