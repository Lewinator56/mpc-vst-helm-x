# Helm MPC VST

## Status & Progress

- **Engine & Port**: 32-voice polyphonic synthesizer (Helm DSP engine by Matt Tytel) cross-compiled for ARM32 MPC OS standalone devices.
- **Packaging**: Packaged as `dist/Helm-1.0.0-mpc-armv7.zip` with manifest configured for catalog publishing (`id`: `helm`, `source_repo`: `Lewinator56/mpc-vst-helm`, `license`: `GPL-3.0-only`).
- **Release**: Published and verified on GitHub release `1.0.0` on `Lewinator56/mpc-vst-helm` (SHA-256: `cf652d453c66c3ee815c88b2865e227703fe4eb78133b60b0e112bd8f0c94cdc`).
- **Validation**: End-to-end verified with `tools/catalog_check.py --catalog` and `tools/catalog_build.py` (0 warnings, 0 problems).
- **Catalog Entry**: Added `catalog/plugins/helm.json` in `mpc-vst-plugins` on branch `add-helm`.
