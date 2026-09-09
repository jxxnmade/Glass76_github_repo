# Changelog

Notable changes to Glass76. This project follows [semantic versioning](https://semver.org),
with the caveat that the plug-in's class IDs never change — a saved project
made with any version will always find the plug-in again.

## [Unreleased]

## [2.0.0-alpha] — 2026-09-09

Alpha: the five items below are all new this release and have had
comparatively little real-world use yet, hence alpha rather than a finished
2.0.0. Nothing here changes a saved project's automation lanes or class IDs.

### Plug-in

- **Hardware skin**: a real 1176-style rack faceplate, not the placeholder
  that reused Glass's own layout. One continuous brushed-metal panel (no
  group boxes) at its own 1240×470 window size: large Input/Output knobs,
  stacked Attack/Release knobs, a vertical Ratio button stack, an analog VU
  meter with a needle sweeping off the same smoothed meter state Glass's own
  circular gauge reads, a vertical Meter-select stack, Comp Off set apart in
  red, and the Auto Makeup/Analog/Mix/Trim bottom row. Adapted from the
  classic 1176/CLA-76 hardware layout — the knob/meter/ratio-stack
  arrangement is generic vintage-compressor language, not anything belonging
  to a specific other plug-in — and branded as Glass76 throughout, not
  copying any other product's chrome, logo or names. The existing
  light/dark appearance toggle doubles as the two classic metal finishes.
  Procedural, like Glass: no bitmap assets, built from the same VSTGUI
  gradient/path primitives. New `WidgetKind::Knob` (relative vertical drag,
  the way every real and virtual knob works, unlike the existing `Slider`'s
  absolute x-position mapping) and a per-skin design canvas size
  (`RootView::designSize`) needed adding to support it — see
  `source/ui/skin_hardware.h/.cpp` and `source/ui/widget.h`.
- **Skin picker**: the Settings panel's old single "tap to switch to the
  other skin" pill is a real picker now, one selectable option per
  `skins::all()` entry — the mechanism that lets a third skin just add
  another pill later rather than needing new code.
- **Window scale**, 25/50/100/150/200 %, in Settings below Refresh rate,
  persisted like every other UI preference and applied via
  `VSTGUI::VST3Editor::setZoomFactor`. Also reachable from the host's own
  native "Zoom" context-menu item where the host supports it
  (`setAllowedZoomFactors`), which stays in sync with the Settings panel
  either way. Fixed one real bug in the underlying window-sizing work along
  the way: `RootView::ensureChrome()`'s cached backdrop bitmap was created at
  sub-1.0 scale for zoom below 100%, a code path VSTGUI's Direct2D backend
  does not handle correctly (real content-scale factors are always ≥1 —
  zoom was the first thing in this project to legitimately drive it below
  that), which cropped most of the panel off the visible window at low zoom
  instead of scaling it down. Clamped to a minimum of 1.0. A second, separate
  bug hit switching skins while zoomed: `setEditorSizeConstrains()` was only
  ever called *before* `exchangeView()` built the new skin's view, so its
  resize request raced whatever the host's frame size happened to be at that
  instant rather than the size the new skin actually needed — at a big zoom
  mismatch (Hardware's 1240-wide canvas vs. Glass's 880-wide, both scaled
  down together) this could leave the window at the old skin's size while the
  new skin's full design-space content rendered into it. Fixed by
  re-asserting the size constraint in `createCustomView()`, once the new
  view actually exists.
- **Continuous Attack and Release.** The printed 1/3/5/7 positions were
  always four samples off a continuous exponential curve
  (`attackPositionToSeconds`/`releasePositionToSeconds` in `source/params.h`
  already took a continuous position); only the parameter and the widget on
  top of it forced it to those four detents. Both are a genuinely continuous
  `Slider` (Glass) or `Knob` (Hardware) now — full drag range, not clamped to
  the old four stops. Glass's sliders keep tick marks at 1/3/5/7 (which land
  exactly on evenly-spaced normalized positions) so a setting can still be
  eyeballed against the hardware's own panel; the ticks are a reference, not
  a stop. 12 new pure-math tests (`glass76_test --param-test`) check the
  curve against all four known points and confirm strict monotonicity across
  a 200-point sweep of the whole range.
- **Transparent background**, a Settings toggle that skips the opaque
  window-background/toolbar/card fills so the host's own window shows
  through. Partially working: background and toolbar transparency is
  confirmed live against a real host window, not just visually plausible,
  but card fills currently stay opaque instead of showing through too — a
  Direct2D-specific compositing quirk with gradient/path-clip fills against a
  layered-transparent window that wasn't resolved before this release
  shipped. Off by default.

### Plug-in (earlier work this release)

- **Global preferences file**, `~/Documents/Glass76/preferences.json`
  (`%USERPROFILE%\Documents\Glass76\` on Windows, via `SHGetKnownFolderPath`
  so a OneDrive-relocated Documents folder still resolves correctly).
  Appearance, background image and refresh rate now follow the user across
  every project and instance instead of resetting to per-project state;
  writes are debounced (~500 ms off the existing UI timer) and flushed
  unconditionally when the editor closes. See `source/prefs.h`.
- **Skin preference.** A `skin` field ("hardware" | "glass") now round-trips
  through both project state and the preferences file. Landed as plumbing
  only, ahead of the picker and the Hardware layout above that actually use
  it — see this release's own Hardware skin and skin picker entries.

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
- **macOS installer app.** `installer/mac/build_installer.sh` assembles
  `Install Glass76.app` — no Xcode project, just a folder with a shell
  script as its `CFBundleExecutable` (see `installer/mac/install.sh`), which
  copies `Glass76.vst3` into `~/Library/Audio/Plug-Ins/VST3` and clears the
  quarantine flag on double-click. CI builds and uploads it on every push and
  attaches it to tagged releases alongside the bare bundle zip.

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

[1.1.2]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/jxxnmade/Glass76_github_repo/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/jxxnmade/Glass76_github_repo/releases/tag/v1.0.0
