# Changelog

Notable changes to Glass76. This project follows [semantic versioning](https://semver.org),
with the caveat that the plug-in's class IDs never change — a saved project
made with any version will always find the plug-in again.

## [Unreleased]

### Packaging

- **macOS build.** `.github/workflows/ci.yml` now also builds Glass76 on
  `macos-latest` (Xcode generator), runs the SDK validator and the offline
  DSP host against it, and packages `Glass76-vst3-bundle-macos.zip` alongside
  the Windows installer on tagged releases. The Windows-only font-loading
  workaround and the offline test host's platform module loader in
  `source/ui/macdraw.cpp` / `CMakeLists.txt` are now correctly scoped behind
  `WINDOWS` / `SMTG_MAC` / `SMTG_LINUX` instead of being compiled
  unconditionally, and the plug-in target picks up a bundle identifier via
  `smtg_target_set_bundle`. Unsigned and not notarized — there is no Apple
  Developer Program membership behind this project, so Gatekeeper blocks the
  bundle on first launch; the release notes carry the `xattr` workaround.

## [1.1.1] — 2026-09-07

### Plug-in

- **Signature** recalibrated against the CLA-76 sweep-test comparison
  (`SWEEP_ANALYSIS.md`): the detector now runs through a ~45 Hz sidechain
  highpass and a short smoother ahead of the gain computer (was riding
  individual bass cycles, 2-4x the CLA-76's low-frequency gain ripple), the
  output saturator's symmetric quadratic pre-term is replaced with an
  asymmetric quadratic/quartic shape blended with a touch of hard clipping
  (closer to the CLA-76's measured even-harmonic profile), and the
  saturator's drive now tracks the pre-gain-reduction signal level instead
  of the applied gain reduction, so distortion actually responds to the
  Input knob instead of staying constant. CLEAN is unaffected — its
  detector and output stage were already unconditioned/unsaturated by
  design.
- **Input/Output attenuator taper corrected below −18 dB.** The nine printed
  marks were uniform 6 dB apart from launch, justified only by a single
  reference point (the real CLA-76 reads "-30.0 dB" at mark 3). A nonlinear
  fit of Glass76's own gain-computer shape against the CLA-76's actual
  measured response (`SWEEP_ANALYSIS.md`, Finding 7), validated first
  against Glass76 CLEAN's own known table, shows marks 1 and 4 were off by a
  real amount: −48 dB → −43 dB and −24 dB → −19.5 dB. Marks above −18 dB are
  unchanged — the same measurement shows the real hardware compresses harder
  than its nominal 20:1 ratio at that much drive, which a taper table alone
  cannot fix, so that part is left as documented open work rather than
  guessed at.

### Verified

- SDK validator 47/47, offline DSP host 15/15, from a clean rebuild. Three
  offline-host checks that had the old `-24 dB` mark-4 value baked in as a
  magic number now read `kGainStepsDb` directly instead.

## [1.1.0] — 2026-09-07

### Plug-in

- **Glass76 CLEAN / Glass76 Signature** model switch, a new automatable
  parameter. CLEAN is a mathematically transparent compressor: the attenuator
  dB is applied as printed with no hidden hardware drive, there is no FET
  saturation stage, release uses a single time constant instead of the
  programme-dependent dual one, **All buttons in** behaves as plain 20:1 with
  none of the threshold/knee/drive quirks, and Analog's mains hum stays off
  no matter what it is set to. Signature is the unchanged CLA-76-calibrated
  build from 1.0.0. Projects saved before this version load as Signature, so
  nothing already mixed changes sound.

### Interface

- The model switch is drawn as a glass slider in the top-left of the
  toolbar — "Glass76 CLEAN" in the interface's own bold text, "Glass76
  Signature" in Allura, a bundled cursive script face (with a system-font
  substitute chain for the unlikely case the bundle failed to load, the same
  pattern `macdraw.cpp` already uses for Inter/SF Pro).
- **Dark is now the default appearance** on first launch.
- Warm beige/creme palette in both appearances, replacing the cold blue
  accent and white surfaces: sliders, switches, the gauge arc and focus rings
  are now a tan/beige, and window and glass surfaces lean creme instead of
  stark white.
- **Settings panel**, opened from a new gear button in the toolbar: choose an
  image file to show beneath the glass panels as the background (persisted
  per-instance in controller state, same mechanism as the appearance
  preference), and credits, signed in the script face. The accent family —
  sliders, switches, the gauge arc, focus rings — re-tints to the chosen
  image's own dominant hue, computed as a brightness-weighted circular mean
  rather than persisted, so it always matches the current picture.
- The Analog control dims and stops responding to clicks while Glass76 CLEAN
  is selected, since it has no effect there.

### Packaging

- Vendor renamed from "Jaxson" to **jxxnmade** in the plug-in factory info
  and the Windows file version resource.

### Verified

- SDK validator 47/47, offline DSP host all checks passed, from a clean
  rebuild — the default (Signature) model measures identically to 1.0.0.

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

[1.1.1]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/jxxnmade/Glass76_github_repo/releases/tag/v1.0.0
