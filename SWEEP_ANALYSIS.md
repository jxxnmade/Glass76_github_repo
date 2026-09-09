# Glass76 vs CLA-76 — sweep test analysis

Source data: `C:\Users\jaxson\Desktop\second chance\sweep tester\` — `dry.wav`,
`CLA.wav`, `glass clean.wav`, `glass signature.wav` (96 kHz, 32-bit float,
stereo, 41.15 s, L/R identical).

Analysis date: 2026-09-07. Findings below reflect the pre-change code.

---

## Update 2026-09-07 — Signature items 1-3 implemented

Applied, in `source/processor.{h,cpp}` (and mirrored into
`Glass76_github_repo/source/`):

1. **Sidechain conditioning** (Finding 5). Signature's detector now runs
   through a ~45 Hz one-pole highpass plus a 150 us smoother before the gain
   computer; CLEAN's detector is untouched (raw, instantaneous `|s|`), by
   design, since it has no sidechain shaping of its own to fix.
2. **Saturator reshaped** (Finding 4). The symmetric `d + 0.10*d*d` pre-term
   is replaced with an asymmetric quadratic (opposite sign each half) plus a
   quartic term and a 20% blend of hard clipping (`processor.cpp`, the
   `signatureSaturate` helper). Tuned offline against a synthetic 193 Hz sine
   via FFT to approximate this doc's CLA-76 harmonic table -- all of H2-H7
   land within about 1-3 dB of target, versus 7-11 dB off before. This is a
   memoryless shaper standing in for a real FET/transformer stage, so treat
   it as a directional match, not a verified one, until it's checked against
   real audio.
3. **Drive moved off gain reduction** (Finding 4's "H2 constant at every
   Input position"). The saturator's drive now scales with the
   pre-gain-reduction level (`over`, the detector's dB-over-threshold),
   which tracks the Input knob directly, instead of `grDb`, which the
   compressor's own feedback loop pins to roughly the same value regardless
   of Input.

Verified: `glass76_test.exe` (the existing gain-staging / compression /
meter checks) passes clean against the rebuilt plug-in -- no regression in
the static gain calibration, ratio behaviour, or meters. The harmonic and
ripple targets themselves have **not** been re-verified against real audio;
that needs the dedicated re-test below.

Not touched, per the caveats already in this doc:

- **Taper shape** -- needs the fixed-tone Input ramp this render doesn't
  have. See Finding 1's revision below: this, not a ratio mismatch, is now
  the leading suspect for the original "effective ratio" gap, so it moved up
  in the Plan's priority order.
- **Output level match** and **Input calibration** -- both confounded by the
  taper and by the contaminated 20-40 Hz knee measurement; re-measure after
  the taper is settled.

CLEAN was not changed beyond leaving its detector alone as noted above: it
already had no dual release, no saturation, no hum, and no all-buttons-in
quirks (see `updateDerived` / `processAudio`'s `mSignatureModel` branches),
which is the "most basic compression, no CLA emulation" spec.

---

## Update 2026-09-07 (2) — Taper shape reconstructed from this same data

The Plan below said the taper needed a dedicated fixed-tone Input ramp this
sweep test doesn't have. Before requesting that render, this pass tried to
extract the taper from the *existing* sweep audio anyway, since the Input
automation in sweeps 1-2 already is an Input ramp, just confounded with the
sweep's own frequency change (see "What the test actually is" below). It
partly worked; full detail below, filed as Finding 7 in the findings list.

**Method.** Glass76's own gain computer is a known, exact function of one
unknown curve, atten(p) -- the thing kGainStepsDb encodes -- plus one unknown
scalar (everything else additive: source level, input make-up, output
make-up, folded together since a render never varies them separately). Given
a measured output-level-vs-p curve, both can be recovered by a joint
nonlinear least-squares fit: parametrize atten(p) as a monotonic PCHIP
spline over 17 knots and fit the knot heights plus the one scalar to
reproduce the measured curve through Glass's exact threshold/knee/ratio
formula (`source/processor.cpp`'s gain computer, reused verbatim in Python).
Script and data are not checked in (scratch-only); the method is reproducible
from this description plus `params.h`'s gain computer.

**Validation, before touching anything real.** Run the fit on `glass
clean.wav`, whose true atten(p) is exactly `kGainStepsDb` -- no saturation,
no hardware drive, nothing to confound it. Fit residual: 0.08 dB RMS against
the measured curve. Reconstruction vs the *known* table, after anchoring:
matches within 0.7 dB from p=0.25 to p=0.75, but drifts to +4 dB at p=0.875
and +16 dB at p=1.0. That is exactly the doc's own p>0.9 caveat showing up
independently: deep in 20:1 compression the output curve goes nearly flat,
so a given measurement error there implies a huge atten(p) error once
divided back through the ratio. The method is trustworthy in the middle of
the travel and knowingly is not near the top.

**Applied to CLA.wav.** Fit residual: 0.11 dB RMS -- as tight as the CLEAN
validation, meaning a fixed threshold/ratio/knee (Glass's own shape) genuinely
explains the real CLA-76's behaviour well in the region the fit can see. The
reconstructed taper, anchored at the p=0.375 -> -30.0 dB reference point that
was already the only hard anchor in this file, and cross-checked as stable
under three different trust windows (p up to 0.62/0.70/0.80/0.96, which
should not agree if this were fit noise, and did, within 1-2 dB per point):

| p (mark) | old table | reconstructed | delta |
|---|---|---|---|
| 0.125 (mark 1) | -48.0 | ~-43 | +5 |
| 0.25 (mark 2) | -36.0 | ~-36 | ~0 (confirms the old value) |
| 0.375 (mark 3) | -30.0 | -30.0 | 0 (the anchor itself) |
| 0.5 (mark 4) | -24.0 | ~-19.5 | +4.5 |

Applied to `kGainStepsDb`: mark 1 -48 -> -43, mark 4 -24 -> -19.5. Marks 2
and 3 confirmed, not changed.

**Marks 5-7 (-18, -12, -6) were not touched, and the reason is itself a
finding.** Past p~0.6 the fit stops converging on a believable curve --
robustness-checked the same way, the reconstruction gives mark 5 (p=0.625)
a small *positive* value (~+3 dB) and mark 6 (p=0.75) an unphysical ~+72 dB,
consistently across every trust window that includes that data, so it is not
noise. What is happening: from p~0.5 to p~0.85 the measured CLA output is
still climbing at roughly 25-40 dB/unit (this is Finding 3's original
"25-43 dB/unit at p 0.67-0.85" number, now corroborated independently) --
far faster than a fixed 20:1 ratio can produce from *any* finite taper slope,
since 20:1 caps the output response at 1/20th of whatever the taper does.
The only way the real hardware can still be climbing that fast is if its
*effective* ratio is substantially harder than the nominal 20:1 button once
the FET is driven this hard -- a real, well-known 1176-family trait (see
also the "British mode" / all-buttons-in behaviour this plug-in already
models as a *separate* button precisely because a fixed ratio undersells
how hard the real thing compresses at extremes). A taper-table fix cannot
correct that; it needs either a ratio that itself varies with drive, or the
dedicated fixed-tone re-test the Plan calls for to characterize it properly.
Left as documented, open work.

**Not touched: kInputMakeupDb / kOutputMakeupDb.** This render's CLA-76
Output setting is not recoverable in Glass's terms (the fit's one scalar
folds source level, input make-up and output make-up together, and this
test never held Input still to separate them), so the existing calibration
from the dedicated FL real-track render (see the README, 0.16 dB error
against a real render) remains authoritative. Items 5 and 6 in the Plan are
still open.

**Verified:** `glass76_test.exe` and the SDK validator both pass unchanged
(15/15, 47/47) after the table edit -- three of its checks encode the old
`-24 dB` mark-4 value as a magic number and were rewritten to read
`kGainStepsDb[kInputDefaultStep]` instead, so they no longer silently drift
from the table if it moves again.

---

## What the test actually is

`sweep tester.flp` contains two automation clips:
**"Glass76 - hyperpop chain - Input"** and
**"CLA-76 Stereo - hyperpop chain - Input"**. So the automated parameter is
each plug-in's **Input knob**, not the source level.

The source is three consolidated 3xOsc clips, each one a 6.86 s log sweep
20 Hz → 30 kHz, arranged flat / flat / ramp-up / ramp-up / ramp-down /
ramp-down. The Input automation is a triangle with a 2-sweep period, so it
alternates min→max, max→min every sweep.

Consequence: **only sweeps 1–2 (0–13.72 s) are clean data.** There the source
is constant at −3 dBFS RMS, so output level is a pure function of the Input
knob. Sweeps 3–6 have source level *and* Input moving at once and are unusable
for a transfer curve.

All four renders are sample-aligned (cross-correlation lag 0 at every probe
point, no PDC offset), so direct differencing is valid.

Residual confound even in sweeps 1–2: frequency is sweeping too, so the ends
of each curve (p < 0.1 and p > 0.9) mix in the 20 Hz start and the 20 kHz+
rolloff. Checked by overlaying up-ramp against down-ramp at matched Input —
they agree within 0.2 dB from p = 0.15 to 0.70, so the middle is trustworthy
and the edges are not.

---

## Finding 1 — revised: ratio was matched, the "effective ratio" gap is a taper artifact

**Correction, 2026-09-07:** the original version of this finding read the
project's *current* saved state to conclude Glass and CLA were on different
ratios at render time. That state was edited after the render, before this
analysis was written, so it no longer reflected what actually produced the
four .wav files. Recovered the true render-time state from FL's autosave
history (`Backup\sweep tester (autosaved at 12h09)_3.flp`, timestamped to
the minute of the `glass signature.wav` / `CLA.wav` renders, and `...12h10...`
for `glass clean.wav`): Glass76's own state chunk decodes cleanly (it's our
own `getState` format) to **Ratio = 20:1** at both. The Waves CLA-76 preset
chunk (`Start Me Up`, readable XML) is byte-identical across *every*
autosave in the session including the current file, so it was never touched
-- and per direct confirmation, **it was also on 20:1**. Ratio was matched.
That retracts the rest of this finding as originally written.

So the headline numbers below are real, at matched settings, and need a
different explanation:

In the compressed region Glass's output moves **2.1 dB per unit of Input
travel** (CLEAN) and **1.8** (Signature). The attenuator table in
`source/params.h` is 6 dB per eighth = 48 dB/unit there -- a uniform taper.
Naively dividing that out gives an "effective ratio" of ~20-23:1, i.e.
consistent with the dial.

CLA over the same span moves 8.6 dB, which by the same naive division would
read as ~3.5-4:1 -- despite being on 20:1.

That naive division is the bug, not the plug-ins: it assumes a knob click
feeds the same number of dB into the detector on both sides, which Finding 3
already shows is false. Glass's Input taper is uniform (48 dB/unit,
constant); CLA's is not (~4 dB/unit at p 0.3-0.5, ~25-43 at p 0.67-0.85 --
see Finding 3). At a real, matched 20:1 ratio, a shallower taper mechanically
produces a shallower "output dB per knob-travel unit" even with identical
gain-computer math, because less dB is reaching the detector per click in
the first place. The two point-samples of CLA's taper slope aren't enough to
integrate an exact prediction over the measured span, but the direction and
rough scale both point at the taper, not the ratio, as the dominant cause.
**Do not use this doc's "effective ratio" numbers as evidence of a ratio bug
in either plug-in.** Whether there is any *residual* gap once the taper is
accounted for is exactly what the fixed-tone Input ramp in the "Plan" below
would settle -- Finding 3's taper mismatch is now the higher-priority item of
the two, not the lower one.

Findings 2 and 4-6 don't depend on this and are unaffected.

---

## Finding 2 — CLEAN's knee sits ~10 dB late, Signature's ~3–5 dB late

Measuring where d(out)/dp departs from the 1:1 taper:

| | knee at Input ≈ |
|---|---|
| CLA-76 | 0.10 |
| Glass Signature | 0.125 |
| Glass CLEAN | 0.21 |

Below the knee Glass tracks 1:1 at 182–185 dB/unit against the table's 192 —
the taper implementation is correct.

The CLEAN↔Signature gap measures ~9–10 dB, which matches
`kInputMakeupDb 34.85` − `kCleanMakeupDb 24.0` = 10.85 exactly. That
cross-check confirms the whole reading of the experiment.

Caveat: the knee region falls where the sweep is at 20–40 Hz, so the absolute
CLA number is soft. The CLEAN-vs-Signature gap is solid; the Signature-vs-CLA
~3–5 dB is indicative only.

---

## Finding 3 — the Input taper shape differs from Waves'

Glass's table is uniform 6 dB per eighth above −36 dB. CLA is not: its output
slope is ~4 dB/unit at p 0.3–0.5 and ~25–43 at p 0.67–0.85. At a fixed 4:1
that can only mean the CLA's dB-per-p is compressed in the middle and expanded
at the top.

The comment in `params.h` claims the taper was verified from one point
(−30.0 dB at norm 0.375); one point can't constrain a curve.

---

## Finding 4 — harmonic structure

At 193 Hz, dB relative to fundamental:

| | H2 | H3 | H4 | H5 | H6 | H7 | THD |
|---|---|---|---|---|---|---|---|
| CLA-76 | −47.2 | −43.3 | −61.9 | −50.1 | −66.1 | −55.4 | 0.89% |
| CLEAN | −62.2 | −42.3 | −77.4 | −51.4 | −77.9 | −55.2 | 0.83% |
| Signature | −40.3 | −44.2 | −73.3 | −58.7 | −78.7 | −61.7 | 1.15% |

The interesting result: **CLEAN's odd series already matches the CLA within
~1 dB** (H3 −42.3 vs −43.3, H5 −51.4 vs −50.1, H7 −55.2 vs −55.4). It's purely
missing the even series. CLEAN has no waveshaper, so that odd content is
gain-reduction ripple — which happens to land in the right place.

Signature gets the even order wrong in shape, not just amount: H2 is 7 dB hot
and H4 is 11 dB *shy*, so H2 sits 33 dB above H4 where the CLA's sits 15 dB
above. That is the signature of the pure quadratic pre-term
`a = d + 0.10*d*d` at `source/processor.cpp:405` — a squared term makes H2 and
DC and essentially nothing else. Signature also loses the CLA's upper odd tail
(H5 −58.7 vs −50.1, H7 −61.7 vs −55.4).

Signature's H2 also measures −38 to −40 dB at *every* Input position and every
frequency tested. That's structural: the saturator sits after the gain
reduction, which pins its input level, and the `/drive` normalization cancels
most of the `1 + grDb*0.020` compensation.

---

## Finding 5 — low-frequency detector ripple, 2–4× the CLA's

Cycle-synchronous gain ripple, peak-to-peak, measured in the settled plateau
(not the knee):

| | 60 Hz | 80 Hz | 110 Hz |
|---|---|---|---|
| CLA-76 | 0.50 dB | 0.59 | 0.67 |
| CLEAN | 0.91 | 0.76 | 0.54 |
| Signature | **2.01** | **1.70** | **1.57** |

The gain computer at `source/processor.cpp:349` feeds instantaneous `|s|`
straight in with no sidechain smoothing or highpass, and attack position 3 is
274 µs — fast enough to ride individual bass cycles. Signature is worse than
CLEAN because its program-dependent release holds it in the fast branch.

---

## Finding 6 — output staging

Mid-plateau output level: CLA −12.3, CLEAN −14.4 (−2.1), Signature −16.8
(−4.5). Peak across the file: CLA 1.097, CLEAN 0.513, Signature 0.313.

Signature is roughly 4.5 dB quiet at equivalent settings, which will make it
lose every A/B on loudness alone.

---

## Finding 7 — the taper is recoverable from this data below p~0.6, not above it

Full method, validation and numbers are in "Update 2026-09-07 (2)" near the
top of this file. Summary: fitting Glass's own gain-computer shape against
the measured CLA.wav output curve (validated first against Glass CLEAN's
*known* table, 0.08 dB residual, <1 dB reconstruction error over p 0.25-0.75)
recovers a stable, non-uniform taper for p up to ~0.6 -- applied to
`kGainStepsDb` marks 1 and 4. Past that point the same fit needs the CLA-76
to be attenuating on the order of tens to a hundred-plus extra dB to explain
how fast its output keeps climbing, which is not a taper a physical pot can
have; the real explanation is almost certainly that the CLA-76's effective
ratio exceeds its nominal 20:1 at that much drive, which corroborates this
finding's own original "25-43 dB/unit" number at p 0.67-0.85 rather than
contradicting it. Marks 5-7 are left alone pending the dedicated re-test.

---

## What is *not* wrong

- **Time alignment** — 0 samples for all three renders.
- **Aliasing** — no artifact above the sweep's own skirt at 96 kHz
  (all three measure ≈ −50 dB below-fundamental energy, i.e. the skirt itself).
- **DC** — Signature's blocker works; overall mean is 1e-13.
- **Asymmetry direction** — Signature +2.2% vs CLA +1.3% at 908 Hz. Right
  sign, ~1.7× too much.

---

## Plan

Items 1-3 below are done (see "Update 2026-09-07" at the top). Ratio is
confirmed matched (20:1 on both, per the FLP autosave forensics and direct
confirmation -- see Finding 1's revision), so the "match ratio" step below is
no longer needed for the re-run; everything else in it still is.

### First — re-run the test properly

The current data can't settle the taper or the exact knee offset.

1. Match attack, release, and Output between the two plug-ins explicitly
   (ratio is already known-matched at 20:1), and confirm Auto-Makeup is off
   on Glass.
2. Split the frequency sweep from the level sweep. Two separate renders:
   - steady 1 kHz sine at −20 dBFS, Input ramped 0→1 over 30 s — gives the
     transfer curve and taper;
   - fixed Input, source level ramped — gives ratio and knee.
3. Add a third render at 40 Hz for the ripple measurement, and one at 1 kHz at
   several fixed Input positions for the harmonic-vs-drive curve.
4. A separate transient render (drum one-shots). Attack and release are
   completely unmeasurable from these ramps, and they're half of what makes an
   1176 an 1176.

### Then, in priority order

1. ~~Sidechain conditioning~~ — done. `source/processor.cpp`'s detector now
   highpasses (~45 Hz) and smooths (150 us) ahead of the gain computer for
   Signature. Ripple target not yet re-verified against real audio.

2. ~~Reshape Signature's saturator~~ — done. Asymmetric quadratic + quartic +
   hard-clip blend (`signatureSaturate` in `processor.cpp`), tuned offline to
   approximate H2 ≈ −47, H4 ≈ −62, H6 ≈ −66, H5/H7 ≈ −50/−55. Not yet
   re-verified against real audio.

3. ~~Move or rescale the drive~~ — done. Drive now scales with the
   pre-gain-reduction over-threshold amount instead of `grDb`.

4. **Taper shape** — `kGainStepsDb`. ~~Partially done~~ — see "Update
   2026-09-07 (2)" and Finding 7: marks 1 and 4 (-48→-43, -24→-19.5) were
   recovered from this same sweep data by fitting Glass's own gain-computer
   shape against it, validated to <1 dB against Glass CLEAN's known table
   over the same range first. Marks 5-7 could not be recovered this way —
   the real CLA-76 appears to compress harder than its nominal 20:1 at that
   much drive, which no taper curve can reproduce on its own. Still needs
   the fixed-tone Input ramp (or a level sweep at several fixed high-Input
   settings, to characterize the apparent ratio increase directly) before
   those three marks can be touched.

5. **Output level match** — `kOutputMakeupDb` 21.29 is ~4.5 dB shy of a CLA
   level match at equivalent settings. Fix only after the taper is settled
   (item 4), since the current number is confounded by it, not by ratio.

6. **Input calibration** — `kInputMakeupDb` / `kThresholdDb`. Signature's knee
   looks ~3–5 dB late but the measurement sits in the contaminated 20–40 Hz
   region. Re-measure at 1 kHz before touching it.

CLEAN's ~10 dB knee offset from Signature is by design (`kCleanMakeupDb`), so
leave it unless CLEAN's knob should land in the same place as Signature's.
