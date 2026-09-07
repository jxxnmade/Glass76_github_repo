# Building Glass76

Windows x64 only. There is no macOS or Linux build — the editor is drawn
through VSTGUI's Direct2D/DirectWrite backend and the font workaround in
`source/ui/macdraw.cpp` is Win32-specific.

## What you need

| | |
|---|---|
| **Visual Studio 2022 or newer** | The *Desktop development with C++* workload. Build Tools alone is enough — the full IDE is not required. |
| **CMake 3.25+** | https://cmake.org/download — tick "add to PATH". |
| **Git** | Only to clone the VST 3 SDK. |
| **NSIS 3** | Only to build the installer. `winget install NSIS.NSIS` |
| **Python 3 + Pillow** | Only to regenerate the installer icon. |

## The VST 3 SDK

The SDK is not vendored here: it is a large superproject of seven submodules
with its own release cadence, and pinning a copy inside this repository would
make it much harder to track. Glass76 is built and tested against
**v3.8.1_build_84**.

`CMakeLists.txt` looks for it in this order:

1. `-Dvst3sdk_SOURCE_DIR=<path>`
2. `extern/vst3sdk` inside this repository
3. `$env:VST3_SDK_DIR`
4. `-DGLASS76_FETCH_SDK=ON`, which clones the pinned tag into `extern/vst3sdk`

A directory counts as an SDK when it contains `pluginterfaces/`, `public.sdk/`,
`base/` and `vstgui4/` — which means it was cloned **recursively**. A
non-recursive clone gives you empty submodule directories and a confusing wall
of missing-header errors.

Getting it yourself:

```bash
git clone --recursive --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3sdk extern/vst3sdk
```

`extern/` is gitignored. The clone is about 1 GB and takes a few minutes.

## The short way

```powershell
.\scripts\build.ps1 -FetchSdk -Validate -Test
```

Drop `-FetchSdk` once you have an SDK. Add `-Install` to copy the bundle into
`C:\Program Files\Common Files\VST3` — that one needs an elevated shell.

## By hand

```powershell
cmake -S . -B build -A x64 -DGLASS76_BUILD_TESTS=ON -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON
cmake --build build --config Release --target Glass76
```

The bundle lands at `build\VST3\Release\Glass76.vst3`. It is a *folder*, not a
file — Windows shows it with a plug-in icon because of the `desktop.ini` the
SDK writes into it.

### CMake options

| Option | Default | |
|---|---|---|
| `vst3sdk_SOURCE_DIR` | — | Path to an SDK checkout. |
| `GLASS76_FETCH_SDK` | `OFF` | Clone the SDK into `extern/vst3sdk` if none is found. |
| `GLASS76_SDK_TAG` | `v3.8.1_build_84` | Tag used by `GLASS76_FETCH_SDK`. |
| `GLASS76_BUILD_TESTS` | `OFF` | Build `tools/offline_test.cpp` into `glass76_test.exe`. |
| `SMTG_ENABLE_VST3_HOSTING_EXAMPLES` | `OFF` | Also builds the SDK's `validator`. |
| `SMTG_CREATE_PLUGIN_LINK` | `OFF` | SDK default is ON. It symlinks the bundle into the user VST3 folder after every build, which needs elevation or Developer Mode on Windows — so the build fails at the last step without them. Off here; install with `-Install` or the installer instead. |

`CMAKE_POLICY_VERSION_MINIMUM` is forced to 3.5 at the top of
`CMakeLists.txt`. VSTGUI bundles a copy of tiny-js that declares an ancient
`cmake_minimum_required`, which CMake 4.x rejects outright. This is the
documented escape hatch and it only affects that third-party subdirectory.

## Testing

Two layers, and they check different things:

```powershell
# the plug-in contract — parameters, state, bus arrangements, the editor
cmake --build build --config Release --target validator
build\bin\Release\validator.exe build\VST3\Release\Glass76.vst3

# the DSP — gain staging, that it actually compresses, mix/trim, the meters
cmake --build build --config Release --target glass76_test
build\bin\Release\glass76_test.exe build\VST3\Release\Glass76.vst3
```

The offline host loads the *built bundle*, not the source, so it catches
packaging mistakes — a missing `.uidesc`, fonts that did not get copied — that
a unit test linked against the same objects would sail straight past.

Expected on a clean rebuild: **validator 47/47, offline host 17/17**.

## The installer

```powershell
.\installer\build_installer.ps1 -Build
```

Writes `build\installer\Glass76-<version>-win64.exe`. `-Build` compiles the
plug-in first; leave it off to package a bundle you already have.

The version comes from `project(Glass76 VERSION ...)` in `CMakeLists.txt` and
nowhere else — it reaches the binary through the generated `projectversion.h`
and reaches the installer through `build_installer.ps1`, so the two cannot
drift. To cut a release, change it there, tag `v<version>`, and push.

The installer is **unsigned**, so SmartScreen shows "Windows protected your
PC" on first run. Signing needs a code-signing certificate you own:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /f cert.pfx build\installer\Glass76-1.0.0-win64.exe
```

### What it installs

| | |
|---|---|
| `%CommonProgramFiles%\VST3\Glass76.vst3` | The bundle. The folder is selectable on the directory page. |
| `%ProgramFiles%\Jaxson\Glass76\` | README, licence, notices, `Uninstall.exe`. |
| `HKLM\Software\Jaxson\Glass76` | The chosen VST3 folder, so upgrades and the uninstaller find it. |
| `HKLM\...\Uninstall\Glass76` | The Apps & features entry. |

It refuses to run over a bundle a DAW still has loaded, rather than replacing
half of it and leaving something broken behind.

## Rebuilding, and FL Studio

FL Studio holds the DLL open for as long as it is running, so **close it before
rebuilding** or the install copy fails. After installing:

> Options → Manage plugins → **Find installed plugins**, with *Rescan
> previously verified plugins* ticked.

## Continuous integration

`.github/workflows/ci.yml` runs the whole thing on `windows-latest` — SDK
clone (cached), build, validator, offline tests, installer — on every push and
pull request. Pushing a `v*` tag additionally uploads the installer and a
zipped bundle to a draft GitHub release.

## Troubleshooting

**`The VST 3 SDK was not found`** — the path you gave is missing one of the four
required directories. Almost always a non-recursive clone:
`git -C extern/vst3sdk submodule update --init --recursive`.

**`cmake_minimum_required` errors from `tiny-js` or `vstgui`** — you are on
CMake 4.x and configuring a subdirectory directly instead of the repository
root. Configure from the root.

**The plug-in builds but the editor is blank or refuses to open** —
`resource/glass76.uidesc` did not make it into `Contents/Resources`. Check that
`smtg_target_add_plugin_resources` ran; a stale `build/` after moving files is
the usual cause. `cmake --build build --target Glass76 --clean-first`.

**Text renders in the wrong typeface** — the bundled Inter faces did not load.
The plug-in logs which font families actually resolved at start-up; see the
VSTGUI resource-path note in the README.

**FL Studio still shows the old version** — it caches verified plug-ins.
Rescan with *Rescan previously verified plugins* ticked, not without.
