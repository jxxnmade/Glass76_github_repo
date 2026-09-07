# Contributing

Bug reports, DSP corrections and FL Studio field reports are all welcome. This
is a small project — there is no CLA and no committee.

## Reporting a bug

Include your **host and version**, **Windows version**, **display scaling**,
and the **Glass76 version** (Apps & features, or the file properties of
`Contents\x86_64-win\Glass76.vst3`). For audio problems, say what the controls
were set to; for UI problems, a screenshot is worth more than a description.

Two known gaps that do not need a new issue: behaviour inside FL Studio has not
been verified end to end, and neither has rendering at 125 % / 150 % scaling.
Concrete reports on either are genuinely useful.

## Before you open a pull request

```powershell
.\scripts\build.ps1 -Validate -Test
```

Both must pass — **validator 47/47, offline host 17/17**. CI runs the same two
on `windows-latest`, so a green local run is a good predictor.

If you change DSP behaviour, add a case to `tools/offline_test.cpp`. It loads
the built bundle rather than linking against the sources, which is what makes
it able to catch packaging regressions as well as maths ones.

## House style

Match what is already there rather than reformatting around your change.

- 4 spaces, no tabs. Lines wrap at 100 columns; comments at 80.
- `PascalCase` types, `camelCase` functions and variables, `k`-prefixed
  constants (`kInputMakeupDb`), everything inside `namespace Jaxson`.
- Comments explain **why**, not what. The existing ones are the calibration:
  they document decisions, hardware behaviour being modelled, and upstream bugs
  being worked around. Do not narrate the code.

## Two rules that are not style

**Never change the class IDs** in `source/cids.h`. Hosts store them in saved
projects; changing one silently breaks every session anybody has ever saved.

**Nothing on the audio thread may allocate, lock, or log.** `process()` and
everything it calls run under a hard deadline. Meters reach the editor through
`data.outputParameterChanges` for exactly this reason — no `IMessage`, no
shared pointers, no `new`. If you need to get data out of the processor, use
that mechanism.

## Where things live

Parameter tables and every plain↔normalized conversion belong in
`source/params.h`, shared by processor and controller — never duplicated. The
macOS 27 tokens belong in `source/ui/theme.h`; the drawing primitives that
consume them in `source/ui/macdraw.*`; the layout in `source/ui/editor.cpp`.
A hard-coded colour or radius anywhere else is a bug.

Every UI value is traceable to a measurement of Apple's macOS 27 UI kit. If you
change one, say in the PR which metric it comes from. "Looks better" is a fine
argument for a change that is deliberately *not* macOS 27 — just label it as
one, the way the faked wallpaper and absent traffic lights are labelled in the
README.

## Releases

The version lives in `project(Glass76 VERSION ...)` in `CMakeLists.txt` and
nowhere else. Bump it there, add a `CHANGELOG.md` entry, tag `v<version>`, and
push the tag — CI builds the installer and opens a draft release.

## Licence

Contributions are accepted under the [MIT licence](LICENSE) that covers the
rest of the project.
