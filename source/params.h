//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// Glass76 -- FET compressor. Shared parameter contract between the
// processor (audio thread) and the controller (UI thread). Both sides read
// their ranges and conversions from here so they can never drift apart.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <algorithm>
#include <cmath>

namespace Jaxson {

//------------------------------------------------------------------------
// Parameter IDs. These are persisted in host projects and automation --
// treat them as permanent. Add new ones, never renumber existing ones.
//------------------------------------------------------------------------
enum Glass76ParamID : Steinberg::Vst::ParamID
{
	kParamInputId      = 0,   // stepped attenuator, 9 detents
	kParamOutputId     = 1,   // stepped attenuator, 9 detents
	kParamAutoMakeupId = 2,   // on / off
	kParamAttackId     = 3,   // continuous, knob position 1..7
	kParamReleaseId    = 4,   // continuous, knob position 1..7
	kParamRatioId      = 5,   // 20:1 12:1 8:1 4:1 ALL
	kParamMeterId      = 6,   // GR / IN / OUT
	kParamCompOffId    = 7,   // on == compression disabled
	kParamAnalogId     = 8,   // 50 Hz / 60 Hz / Off
	kParamMixId        = 9,   // 0..100 %
	kParamTrimId       = 10,  // -18..+18 dB
	kParamModelId      = 11,  // 0 = Glass76 CLEAN, 1 = Glass76 Signature

	// Read-only meter feedback, pushed by the processor through
	// data.outputParameterChanges so the editor can draw a live needle
	// without ever touching processor memory.
	kParamMeterGrId     = 100,
	kParamMeterInId     = 101,
	kParamMeterOutId    = 102,
	kParamMeterMakeupId = 103,   // dB auto make-up is currently adding

	// Kept far away so normal parameters can keep growing.
	kParamBypassId     = 1000
};

//------------------------------------------------------------------------
// Stepped value tables. Index 0 is always the first entry.
//------------------------------------------------------------------------

/** Input / output attenuator detents, in dB. kGainMinusInfDb means silence.
    Marks 1 and 4 (previously the uniform -48 / -24) were corrected in
    2026-09-07 from a joint nonlinear fit of the real CLA-76's gain law,
    reconstructed from the sweep-tester audio in SWEEP_ANALYSIS.md -- see
    "Finding 7" there for the method and its validation against Glass76
    CLEAN's own (exactly known) table. Marks 5-7 are left at their old
    uniform values: the same reconstruction shows the real CLA-76 compresses
    harder than its nominal 20:1 ratio at that much drive, which a taper
    table alone cannot fix (needs the dedicated fixed-tone re-test in the
    Plan there before touching those marks). Marks 0, 2, 3 and 8 measured
    unchanged from the previous table (3 sits at the README's -30.0 dB
    reference anchor exactly). */
static constexpr double kGainMinusInfDb = -1000.0;
static constexpr int kGainStepCount = 9;
static constexpr double kGainStepsDb[kGainStepCount] = {
	kGainMinusInfDb, -43.0, -36.0, -30.0, -19.5, -18.0, -12.0, -6.0, 0.0
};
/** Detent 4 (-19.5 dB) is Glass76's default Input/Output position -- not a
    literal unity point (see kInputMakeupDb/kOutputMakeupDb below for the
    actual net static gain there). */
static constexpr int kInputDefaultStep = 4;
static constexpr int kOutputDefaultStep = 4;

/** Attack / release are continuous now -- the printed 1/3/5/7 positions
    were never anything but four samples off a smooth knob-position curve
    (see attackPositionToSeconds/releasePositionToSeconds below, which
    already took a continuous position; only the parameter and the widget
    on top of them were artificially stepped to 4 detents). Position ranges
    1..7 the same way the panel prints it; normalized 0..1 maps onto it
    linearly, so norm 0 = position 1, norm 1 = position 7. The two defaults
    below are exactly the old step defaults (position 3 and position 5)
    expressed as normalized values, so nothing that depended on the default
    sound changes. */
static constexpr double kAttackDefaultNormalized = 1.0 / 3.0;   // position 3 (234 us)
static constexpr double kReleaseDefaultNormalized = 2.0 / 3.0;  // position 5 (140 ms)

/** Ratio buttons. The last one is the all-buttons-in "British" mode. */
static constexpr int kRatioStepCount = 5;
static constexpr int kRatioAllStep = 4;
static constexpr double kRatioValues[kRatioStepCount] = {20.0, 12.0, 8.0, 4.0, 20.0};
static constexpr int kRatioDefaultStep = 3;    // 4:1

/** Meter source. */
static constexpr int kMeterStepCount = 3;
enum MeterMode { kMeterGR = 0, kMeterIn = 1, kMeterOut = 2 };
static constexpr int kMeterDefaultStep = kMeterGR;

/** Mains emulation. */
static constexpr int kAnalogStepCount = 3;
enum AnalogMode { kAnalog50 = 0, kAnalog60 = 1, kAnalogOff = 2 };
static constexpr int kAnalogDefaultStep = kAnalogOff;

/** Voicing model. CLEAN is the mathematically transparent compressor this
    plug-in started as: threshold, ratio, knee, one release time, nothing
    else. Signature is the CLA-76-calibrated build everything else in this
    file documents -- the hardware drive, the dual release, the FET
    saturation, the all-buttons-in quirks, the mains hum. Selecting CLEAN
    switches all of that off in the processor; it does not attenuate it. */
static constexpr int kModelStepCount = 2;
enum ModelMode { kModelClean = 0, kModelSignature = 1 };
static constexpr int kModelDefaultStep = kModelSignature;   // preserve existing behaviour

//------------------------------------------------------------------------
// Continuous ranges.
//------------------------------------------------------------------------
static constexpr double kMixMinPercent = 0.0;
static constexpr double kMixMaxPercent = 100.0;
static constexpr double kMixDefaultPercent = 100.0;

static constexpr double kTrimMinDb = -18.0;
static constexpr double kTrimMaxDb = 18.0;
static constexpr double kTrimDefaultDb = 0.0;

//------------------------------------------------------------------------
// Fixed internal gain staging. The front-panel controls are attenuators,
// exactly like the hardware: the amplifier gain that follows them is fixed
// and lives here.
//
// These two numbers are calibrated against the Waves CLA-76, from a render
// of real material made inside FL Studio (input -30, output -18, 4:1,
// attack 3, release 5). Measuring the short-time gain of both plug-ins
// against the untouched original gave:
//
//   - at levels below the knee, where gain reduction is zero, the CLA-76
//     applied +8.14 dB where Glass76 applied 0.00 dB;
//   - above the knee the two agreed to within 0.35 dB.
//
// Both can only be true at once if the CLA-76 drives its detector harder
// and makes up less. Solving the pair (extra static gain = D + M, extra
// reduction at 4:1 = 0.75 D, net difference when loud = 0) gives
// D = +10.85 dB of input drive and M = -2.71 dB of output make-up.
//
// The earlier symmetric 24/24 was a plausible guess, not a measurement,
// and it under-compressed by roughly 8 dB at any given setting.
//------------------------------------------------------------------------
static constexpr double kInputMakeupDb = 34.85;
static constexpr double kOutputMakeupDb = 21.29;
static constexpr double kThresholdDb = -18.0;

// CLEAN keeps the same attenuator-plus-fixed-amp topology as the hardware --
// it just skips the CLA-76-specific recalibration above, the saturation, and
// every other bit of character. Without a fixed make-up stage at all, its
// -24 dB detents would attenuate by 48 dB net, which is not "transparent",
// just quiet. The original symmetric 24/24 (see the comment above) is the
// flat, uncoloured version of the same amplifier stage.
static constexpr double kCleanMakeupDb = 24.0;

//------------------------------------------------------------------------
// Meter encoding. Normalized 0..1 on the wire, plain units at both ends.
//------------------------------------------------------------------------
static constexpr double kMeterGrMaxDb = 30.0;   // 0..30 dB of reduction

// IN and OUT are a VU meter, not a peak meter: RMS averaged over 300 ms,
// on the hardware's -20..+3 VU scale. 0 VU = -18 dBFS RMS, which is the
// digital reference Waves calibrates the CLA-76 to. Reading peak dBFS
// against a VU needle compares two different quantities and the numbers
// will never agree.
static constexpr double kVuReferenceDbFs = -18.0;
static constexpr double kMeterLevelMinDb = -20.0;   // VU
static constexpr double kMeterLevelMaxDb = 3.0;     // VU

/** RMS level in dBFS -> VU. */
inline double dbFsToVu (double dbFs) { return dbFs - kVuReferenceDbFs; }

//------------------------------------------------------------------------
// Helpers. All of these are pure and safe to call from either thread.
//------------------------------------------------------------------------

inline int clampIndex (int i, int count)
{
	return i < 0 ? 0 : (i >= count ? count - 1 : i);
}

/** Normalized 0..1 -> step index, matching StringListParameter's mapping. */
inline int normalizedToStep (double n, int count)
{
	if (count <= 1)
		return 0;
	return clampIndex (static_cast<int> (n * (count - 1) + 0.5), count);
}

/** Step index -> normalized 0..1. */
inline double stepToNormalized (int index, int count)
{
	if (count <= 1)
		return 0.0;
	return static_cast<double> (clampIndex (index, count)) / (count - 1);
}

/** dB -> linear amplitude, with the -inf detent treated as true silence. */
inline double dbToLinear (double db)
{
	if (db <= -600.0)
		return 0.0;
	return std::pow (10.0, db * 0.05);
}

inline double linearToDb (double lin)
{
	return 20.0 * std::log10 (lin < 1e-9 ? 1e-9 : lin);
}

/** Normalized 0..1 -> attenuator dB, continuously.

    The nine printed marks sit on even eighths of the travel, with the dB
    value interpolated linearly between them. Keeping the marks on eighths
    means every value a previous stepped build saved still lands on the same
    dB it did before -- only the values at those eighths, in kGainStepsDb,
    have moved as better measurements came in. The eighths were originally
    all 6 dB apart; they no longer are (see kGainStepsDb's own comment). The
    one anchor that is not a guess: the Waves CLA-76's own Input parameter is
    continuous and reads exactly "-30.0 dB" at norm 0.375, mark index 3 of 8,
    which is why that mark has never moved. */
inline double normalizedToAttenuatorDb (double n)
{
	n = std::clamp (n, 0.0, 1.0);
	if (n <= 0.0)
		return kGainMinusInfDb;

	const double pos = n * (kGainStepCount - 1);              // 0 .. 8
	int i = static_cast<int> (pos);
	if (i > kGainStepCount - 2)
		i = kGainStepCount - 2;
	const double frac = pos - i;

	double lo = kGainStepsDb[i];
	const double hi = kGainStepsDb[i + 1];
	// The bottom segment runs to -inf. Ramping steeply into a very low
	// value keeps the taper smooth; only norm 0 itself is true silence.
	if (lo <= -600.0)
		lo = -72.0;
	return lo + (hi - lo) * frac;
}

/** Attenuator position -> the dB actually applied, fixed make-up included. */
inline double inputGainDb (double normalized)
{
	const double db = normalizedToAttenuatorDb (normalized);
	return db <= -600.0 ? kGainMinusInfDb : db + kInputMakeupDb;
}

inline double outputGainDb (double normalized)
{
	const double db = normalizedToAttenuatorDb (normalized);
	return db <= -600.0 ? kGainMinusInfDb : db + kOutputMakeupDb;
}

/** Attack knob position -> seconds, continuously. Position 1 is slowest
    (800 us), position 7 fastest (20 us), an exponential (geometric) curve
    between those two anchors -- a straight line in log space, matching how
    a real pot's stepped detents actually sit on this kind of taper. Also
    lands within measurement noise of the two printed intermediate
    positions this was fit against: 234 us at 3, 68 us at 5 (see
    SWEEP_ANALYSIS.md for where those four numbers came from). `position`
    was always meant to be continuous here; only the parameter and widget
    above it used to force it to {1, 3, 5, 7}. */
inline double attackPositionToSeconds (double position)
{
	return 800e-6 * std::pow (20.0 / 800.0, (position - 1.0) / 6.0);
}

/** Release knob position -> seconds, continuously. Position 1 = 1100 ms,
    7 = 50 ms; same exponential curve as attack, same provenance. */
inline double releasePositionToSeconds (double position)
{
	return 1.1 * std::pow (0.050 / 1.1, (position - 1.0) / 6.0);
}

/** Normalized 0..1 -> attack seconds. norm 0 is position 1, norm 1 is
    position 7, linear in between -- so the printed position itself is
    `1.0 + normalized * 6.0`, which TimeConstantParameter's toString()
    prints alongside the resolved time. */
inline double attackNormalizedToSeconds (double normalized)
{
	return attackPositionToSeconds (1.0 + std::clamp (normalized, 0.0, 1.0) * 6.0);
}

inline double releaseNormalizedToSeconds (double normalized)
{
	return releasePositionToSeconds (1.0 + std::clamp (normalized, 0.0, 1.0) * 6.0);
}

/** Normalized -> plain, for the two continuous parameters. */
inline double normalizedToMixPercent (double n)
{
	return kMixMinPercent + n * (kMixMaxPercent - kMixMinPercent);
}

inline double normalizedToTrimDb (double n)
{
	return kTrimMinDb + n * (kTrimMaxDb - kTrimMinDb);
}

inline double trimDbToNormalized (double db)
{
	return (db - kTrimMinDb) / (kTrimMaxDb - kTrimMinDb);
}

/** Meter encode / decode. Both ends must agree, so both live here. */
inline double grDbToNormalized (double grDb)
{
	return std::clamp (grDb / kMeterGrMaxDb, 0.0, 1.0);
}

inline double normalizedToGrDb (double n)
{
	return std::clamp (n, 0.0, 1.0) * kMeterGrMaxDb;
}

inline double levelDbToNormalized (double db)
{
	return std::clamp ((db - kMeterLevelMinDb) / (kMeterLevelMaxDb - kMeterLevelMinDb), 0.0, 1.0);
}

inline double normalizedToLevelDb (double n)
{
	return kMeterLevelMinDb + std::clamp (n, 0.0, 1.0) * (kMeterLevelMaxDb - kMeterLevelMinDb);
}

//------------------------------------------------------------------------
// State format version. Bump it whenever the byte layout changes, and
// handle the older versions in setState so old projects still open.
//
// v2 adds the model (CLEAN / Signature) double, written right after trim
// and before the three bools. Version-1 streams end where they always did;
// setState only reads it when version >= 2 and otherwise defaults to
// Signature, which is what every version-1 project already sounds like.
//------------------------------------------------------------------------
static constexpr Steinberg::int32 kGlass76StateVersion = 2;

//------------------------------------------------------------------------
} // namespace Jaxson
