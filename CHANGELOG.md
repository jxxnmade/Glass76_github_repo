# Changelog

Notable changes to Glass76. This project follows [semantic versioning](https://semver.org),
with the caveat that the plug-in's class IDs never change — a saved project
made with any version will always find the plug-in again.

## [Unreleased]

## [1.0.0] — 2026-09-06

First public release.

### Plug-in

- Feed-forward peak-sensing FET compressor modelled on the 1176: fixed
  −18 dBFS threshold with attenuator-style Input and Output controls, so the
  −24 dB detent on both is unity gain.
- Ratio 20:1 / 12:1 / 8:1 / 4:1 and **All buttons in** (threshold dropped
  8 dB, 12 dB knee, lagged attack, faster release, FET driven 2.2× harder).
- Attack and Release as hardware knob positions 1/3/5/7, mapped
  logarithmically to 800–20 µs and 1100–50 ms, with the resolved time shown
  next to the control.
- Auto make-up, Mix (0–100 %), Trim (−18…+18 dB), Comp Off (gain reduction
  only — everything else in the chain still applies).
- Analog mode: −78 dBFS mains hum at 50 or 60 Hz with 2nd and 3rd harmonics,
  plus a −96 dBFS noise floor, injected *before* the detector so they compress
  with the signal.
- GR / IN / OUT metering, pushed to the editor as read-only VST 3 parameters
  through `data.outputParameterChanges` — allocation-free on the audio thread.
- Host `Bypass` parameter.

### Interface

- macOS 27 ("Liquid Glass") editor drawn with VSTGUI, traced to Apple's
  published UI kit metrics rather than to Aqua.
- Light and dark appearance, toggled in the toolbar and stored in controller
  state rather than exposed as an automatable parameter.
- Inter and Inter Display ship inside the bundle and load from a private
  DirectWrite collection, so nothing has to be installed system-wide.
- Works around an upstream VSTGUI bug that stops *any* VST 3 plug-in from
  loading its bundled fonts — `Win32Factory::setResourceBasePath()` hands
  `D2DFont::initialize()` an unnormalised path, so the font scan runs against
  a directory that does not exist. See `source/ui/macdraw.cpp`.

### Packaging

- Windows installer (NSIS): per-machine install into the shared VST3 folder,
  reuses the folder an earlier version chose, refuses to overwrite a bundle a
  DAW still has loaded, registers in Apps & features, and uninstalls cleanly.
- `scripts/build.ps1` builds, validates, tests and installs from a clean
  checkout; the VST 3 SDK is located or cloned rather than vendored.

### Verified

- SDK validator 47/47, offline DSP host 17/17, from a clean rebuild.
- Not yet verified: behaviour inside FL Studio itself, and rendering at 125 %
  and 150 % display scaling.

[Unreleased]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/jxxnmade/Glass76_github_repo/releases/tag/v1.0.0
