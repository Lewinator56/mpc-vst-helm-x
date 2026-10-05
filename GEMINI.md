# Helm MPC VST

## Status & Progress

- **Engine & Port**: 32-voice polyphonic synthesizer (Helm DSP engine by Matt Tytel) cross-compiled for ARM32 MPC OS standalone devices.
- **Packaging**: Packaged as `dist/Helm-1.0.0-mpc-armv7.zip` with manifest configured for catalog publishing (`id`: `helm`, `source_repo`: `Lewinator56/mpc-vst-helm`, `license`: `GPL-3.0-only`).
- **Validation**: Verified with `tools/catalog_check.py --catalog` (0 warnings, passes all catalog checks).
- **Catalog Entry**: Added `catalog/plugins/helm.json` in `mpc-vst-plugins`.
- **Next Steps**:
  - Update GitHub Release `1.0.0` on `Lewinator56/mpc-vst-helm` with the verified `Helm-1.0.0-mpc-armv7.zip`.
  - Submit Pull Request to `sd88me/mpc-vst-plugins` containing `catalog/plugins/helm.json`.
