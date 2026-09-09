//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#include "processor.h"
#include "cids.h"
#include "params.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Jaxson {

namespace {

/** One-pole coefficient for a given time constant. Guards against a zero
    or negative time producing a NaN. */
inline double onePoleCoef (double seconds, double sampleRate)
{
	if (seconds <= 1e-9)
		return 1.0;
	const double c = 1.0 - std::exp (-1.0 / (seconds * sampleRate));
	return c > 1.0 ? 1.0 : c;
}

/** Flush denormals without needing an FTZ mode we do not control. */
inline double flush (double x)
{
	return (x > -1e-25 && x < 1e-25) ? 0.0 : x;
}

//------------------------------------------------------------------------
// Signature's output-stage saturator.
//
// Tuned offline (FFT against a synthetic sine, not against the plug-in
// itself) to approximate the CLA-76's measured 193 Hz harmonic profile from
// SWEEP_ANALYSIS.md Finding 4: H2 ~-47 dB, H3 ~-43, H4 ~-62, H5 ~-50,
// H6 ~-66, H7 ~-55 (relative to the fundamental). The old symmetric
// `d + 0.10*d*d` pre-term only ever produced H2 and DC -- that is why the
// previous build's H2 sat 7 dB hot and H4 11 dB shy of the CLA-76 with
// nothing between them. Splitting the quadratic term across the two halves
// of the waveform, adding a quartic term, and blending in a touch of hard
// clipping between them gets all six harmonics within ~2 dB of the target
// instead. It is still a memoryless shaper standing in for a real
// FET/transformer stage, so this is an approximation, not an exact match --
// re-verify against a dedicated per-Input-position harmonic sweep (see the
// analysis doc's "Plan") before trusting it past a couple of dB.
constexpr double kSatQuadPos = 0.10;
constexpr double kSatQuadNeg = -0.10;
constexpr double kSatQuartic = 0.5;
constexpr double kSatHardBlend = 0.20;
constexpr double kSatHardLimit = 0.3;

inline double signatureSaturate (double d)
{
	const double quad = (d >= 0.0 ? kSatQuadPos : kSatQuadNeg) * d * d;
	const double a = d + quad + kSatQuartic * d * d * d * d;
	const double soft = a / std::sqrt (1.0 + a * a);
	// Not divided by kSatHardLimit: both branches must have unit slope at
	// a=0, or blending them shifts the small-signal gain away from unity
	// and throws off the calibration in params.h.
	const double hard = std::clamp (a, -kSatHardLimit, kSatHardLimit);
	return (1.0 - kSatHardBlend) * soft + kSatHardBlend * hard;
}

} // anonymous namespace

//------------------------------------------------------------------------
Glass76Processor::Glass76Processor ()
{
	setControllerClass (kGlass76ControllerUID);

	mInputNorm = stepToNormalized (kInputDefaultStep, kGainStepCount);
	mOutputNorm = stepToNormalized (kOutputDefaultStep, kGainStepCount);
	mAttackNorm = kAttackDefaultNormalized;
	mReleaseNorm = kReleaseDefaultNormalized;
	mRatioNorm = stepToNormalized (kRatioDefaultStep, kRatioStepCount);
	mMeterNorm = stepToNormalized (kMeterDefaultStep, kMeterStepCount);
	mAnalogNorm = stepToNormalized (kAnalogDefaultStep, kAnalogStepCount);
	mMixNorm = kMixDefaultPercent / 100.0;
	mTrimNorm = trimDbToNormalized (kTrimDefaultDb);
	mModelNorm = stepToNormalized (kModelDefaultStep, kModelStepCount);
}

//------------------------------------------------------------------------
Glass76Processor::~Glass76Processor () = default;

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::initialize (FUnknown* context)
{
	tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::setActive (TBool state)
{
	if (state)
	{
		// Nothing to allocate -- every buffer this plug-in needs is a scalar.
		// Just clear the running state so a fresh playback start does not
		// inherit the envelope from the last one.
		mGrDb = 0.0;
		mGrSustainDb = 0.0;
		mMakeupDb = 0.0;
		mHumPhase = 0.0;
		mDcState[0] = mDcState[1] = 0.0;
		mDcPrevIn[0] = mDcPrevIn[1] = 0.0;
		mDetHpState[0] = mDetHpState[1] = 0.0;
		mDetHpPrevIn[0] = mDetHpPrevIn[1] = 0.0;
		mDetSmooth = 0.0;
		mMeterGrDb = 0.0;
		mVuInMeanSquare = 0.0;
		mVuOutMeanSquare = 0.0;
	}
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
// Accept mono->mono and stereo->stereo. FL Studio always asks for stereo;
// the SDK validator also asks for mono.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                        SpeakerArrangement* outputs, int32 numOuts)
{
	if (numIns == 1 && numOuts == 1 && inputs[0] == outputs[0] &&
	    (inputs[0] == SpeakerArr::kMono || inputs[0] == SpeakerArr::kStereo))
	{
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::setupProcessing (ProcessSetup& newSetup)
{
	mSampleRate = newSetup.sampleRate > 0 ? newSetup.sampleRate : 44100.0;

	// Auto make-up follows the average reduction over ~250 ms so it tracks
	// the programme rather than individual transients.
	mMakeupCoef = onePoleCoef (0.250, mSampleRate);
	// Programme dependency: how "sustained" the reduction has been, over ~1 s.
	mSustainCoef = onePoleCoef (1.000, mSampleRate);
	// A VU meter integrates over 300 ms; give it a slower fall than rise so
	// the needle behaves like the real thing.
	mVuCoef = onePoleCoef (0.300, mSampleRate);
	mVuReleaseCoef = onePoleCoef (0.600, mSampleRate);

	// Signature's sidechain conditioning: a ~45 Hz one-pole highpass ahead of
	// the detector, plus a very short (150 us) smoother on the rectified
	// level. Without this the detector rides individual bass cycles, which
	// showed up as 2-4x the CLA-76's low-frequency gain ripple (see
	// SWEEP_ANALYSIS.md Finding 5). CLEAN's detector never uses these --
	// it stays the raw, instantaneous |s| a basic feed-forward compressor
	// would use.
	mDetHpCoef = 1.0 - (2.0 * 3.14159265358979323846 * 45.0 / mSampleRate);
	mDetSmoothCoef = onePoleCoef (150e-6, mSampleRate);

	updateDerived ();

	return AudioEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
void Glass76Processor::updateDerived ()
{
	const int modelStep = normalizedToStep (mModelNorm, kModelStepCount);
	mSignatureModel = (modelStep == kModelSignature);

	// Continuous, not detented: the printed marks are just a scale. CLEAN
	// reads the attenuator exactly as printed; Signature adds the fixed
	// CLA-76 hardware drive documented in params.h.
	if (mSignatureModel)
	{
		mInputGain = dbToLinear (inputGainDb (mInputNorm));
		mOutputGain = dbToLinear (outputGainDb (mOutputNorm));
	}
	else
	{
		// Same attenuator-plus-fixed-amp topology, just the flat, uncoloured
		// make-up instead of the CLA-76 recalibration -- otherwise the -24 dB
		// defaults attenuate by 48 dB net instead of sitting near unity.
		mInputGain = dbToLinear (normalizedToAttenuatorDb (mInputNorm) + kCleanMakeupDb);
		mOutputGain = dbToLinear (normalizedToAttenuatorDb (mOutputNorm) + kCleanMakeupDb);
	}

	const int ratioStep = normalizedToStep (mRatioNorm, kRatioStepCount);
	// All-buttons-in is a hardware quirk, not a ratio: CLEAN just runs the
	// table's nominal ratio (20:1, same as index 0) with no other change.
	mAllButtonsIn = mSignatureModel && (ratioStep == kRatioAllStep);
	const double ratio = kRatioValues[clampIndex (ratioStep, kRatioStepCount)];
	mRatioK = 1.0 - 1.0 / ratio;

	// All-buttons-in: same nominal ratio, but the threshold drops, the knee
	// widens and the FET is driven far harder. That combination is what the
	// mode actually sounds like.
	mThresholdDb = kThresholdDb - (mAllButtonsIn ? 8.0 : 0.0);
	mKneeDb = mAllButtonsIn ? 12.0 : 6.0;

	double attackSeconds = attackNormalizedToSeconds (mAttackNorm);
	double releaseSeconds = releaseNormalizedToSeconds (mReleaseNorm);
	if (mAllButtonsIn)
	{
		// The mode lags into the attack and lets go faster afterwards.
		attackSeconds *= 1.6;
		releaseSeconds *= 0.55;
	}
	mAttackCoef = onePoleCoef (attackSeconds, mSampleRate);
	mReleaseFastCoef = onePoleCoef (releaseSeconds, mSampleRate);
	mReleaseSlowCoef = onePoleCoef (releaseSeconds * 4.0, mSampleRate);

	mSatDrive = mAllButtonsIn ? 2.2 : 1.0;

	const int analogStep = normalizedToStep (mAnalogNorm, kAnalogStepCount);
	// Mains hum is circuit character, not math -- CLEAN never has it, no
	// matter what the Analog switch is set to.
	mAnalogOn = mSignatureModel && (analogStep != kAnalogOff);
	const double humHz = (analogStep == kAnalog50) ? 50.0 : 60.0;
	mHumPhaseInc = 2.0 * 3.14159265358979323846 * humHz / mSampleRate;
	// Composite hum around -78 dBFS, noise floor around -96 dBFS. Both are
	// injected before the compressor so they behave like real circuit noise.
	mHumGain = mAnalogOn ? dbToLinear (-78.0) : 0.0;
	mNoiseGain = mAnalogOn ? dbToLinear (-96.0) : 0.0;
}

//------------------------------------------------------------------------
void Glass76Processor::handleParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;

	bool derivedDirty = false;

	const int32 numParamsChanged = changes->getParameterCount ();
	for (int32 index = 0; index < numParamsChanged; index++)
	{
		auto* queue = changes->getParameterData (index);
		if (!queue)
			continue;

		const int32 numPoints = queue->getPointCount ();
		if (numPoints <= 0)
			continue;

		// Last point in the block: block-accurate, which is what a stepped
		// compressor needs. Nothing here is smooth enough to justify
		// sample-accurate handling.
		ParamValue value = 0.0;
		int32 sampleOffset = 0;
		if (queue->getPoint (numPoints - 1, sampleOffset, value) != kResultTrue)
			continue;

		switch (queue->getParameterId ())
		{
			case kParamInputId:      mInputNorm = value;   derivedDirty = true; break;
			case kParamOutputId:     mOutputNorm = value;  derivedDirty = true; break;
			case kParamAttackId:     mAttackNorm = value;  derivedDirty = true; break;
			case kParamReleaseId:    mReleaseNorm = value; derivedDirty = true; break;
			case kParamRatioId:      mRatioNorm = value;   derivedDirty = true; break;
			case kParamAnalogId:     mAnalogNorm = value;  derivedDirty = true; break;
			case kParamModelId:      mModelNorm = value;   derivedDirty = true; break;
			case kParamMeterId:      mMeterNorm = value;   break;
			case kParamMixId:        mMixNorm = value;     break;
			case kParamTrimId:       mTrimNorm = value;    break;
			case kParamAutoMakeupId: mAutoMakeup = value >= 0.5; break;
			case kParamCompOffId:    mCompOff = value >= 0.5;    break;
			case kParamBypassId:     mBypass = value >= 0.5;     break;
			default: break;
		}
	}

	if (derivedDirty)
		updateDerived ();
}

//------------------------------------------------------------------------
template <typename SampleType>
void Glass76Processor::processAudio (ProcessData& data)
{
	const int32 numSamples = data.numSamples;
	const int32 numChannels =
	    std::min (data.inputs[0].numChannels, data.outputs[0].numChannels);

	auto** in = (SampleType**)getChannelBuffersPointer (processSetup, data.inputs[0]);
	auto** out = (SampleType**)getChannelBuffersPointer (processSetup, data.outputs[0]);

	if (mBypass)
	{
		for (int32 ch = 0; ch < numChannels; ch++)
		{
			if (in[ch] != out[ch])
				std::memcpy (out[ch], in[ch], numSamples * sizeof (SampleType));
		}
		mGrDb = 0.0;
		mMeterGrDb = 0.0;
		mVuInMeanSquare = 0.0;
		mVuOutMeanSquare = 0.0;
		data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
		return;
	}

	const double mix = mMixNorm;
	const double dry = 1.0 - mix;
	const double trim = dbToLinear (normalizedToTrimDb (mTrimNorm));

	// DC blocker coefficient, ~5 Hz corner.
	const double dcR = 1.0 - (2.0 * 3.14159265358979323846 * 5.0 / mSampleRate);

	double grDb = mGrDb;
	double grSustain = mGrSustainDb;
	double makeup = mMakeupDb;
	double humPhase = mHumPhase;
	uint32_t seed = mNoiseSeed;

	// Meter accumulators for the block. GR is a peak (the needle should show
	// the worst reduction), IN and OUT are sums of squares because a VU
	// meter integrates power, not peaks.
	double blockGrPeak = 0.0;
	double blockInSumSquares = 0.0;
	double blockOutSumSquares = 0.0;

	for (int32 i = 0; i < numSamples; i++)
	{
		//--- read the frame, stereo-linked -----------------------------
		double x[2] = {0.0, 0.0};
		for (int32 ch = 0; ch < numChannels; ch++)
			x[ch] = static_cast<double> (in[ch][i]);

		// Mono sum of the channels for the meter, matching how a single
		// hardware needle is fed.
		{
			double v = x[0];
			if (numChannels > 1)
				v = 0.5 * (x[0] + x[1]);
			blockInSumSquares += v * v;
		}

		//--- input attenuator + fixed make-up ---------------------------
		double s[2] = {x[0] * mInputGain, x[1] * mInputGain};

		//--- mains emulation, injected ahead of the detector -------------
		if (mAnalogOn)
		{
			humPhase += mHumPhaseInc;
			if (humPhase > 6.283185307179586)
				humPhase -= 6.283185307179586;
			const double hum = (std::sin (humPhase) + 0.35 * std::sin (2.0 * humPhase) +
			                    0.18 * std::sin (3.0 * humPhase)) * 0.65 * mHumGain;
			for (int32 ch = 0; ch < 2; ch++)
			{
				seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
				const double noise = (static_cast<double> (seed) * 2.3283064365386963e-10 - 0.5) * 2.0;
				// Small phase offset per channel keeps the hum from being
				// a hard-panned centre tone.
				s[ch] += hum * (ch == 0 ? 1.0 : 0.92) + noise * mNoiseGain;
			}
		}

		//--- detector -----------------------------------------------------
		// Signature highpasses and briefly smooths the sidechain first (see
		// setupProcessing); CLEAN reads the instantaneous rectified level,
		// same as before -- the simplest thing a feed-forward compressor
		// can do.
		double det;
		if (mSignatureModel)
		{
			double hp[2] = {0.0, 0.0};
			for (int32 ch = 0; ch < numChannels; ch++)
			{
				const double h = s[ch] - mDetHpPrevIn[ch] + mDetHpCoef * mDetHpState[ch];
				mDetHpPrevIn[ch] = s[ch];
				mDetHpState[ch] = flush (h);
				hp[ch] = h;
			}
			double raw = std::fabs (hp[0]);
			if (numChannels > 1)
				raw = std::max (raw, std::fabs (hp[1]));
			mDetSmooth += (raw - mDetSmooth) * mDetSmoothCoef;
			det = mDetSmooth;
		}
		else
		{
			det = std::fabs (s[0]);
			if (numChannels > 1)
				det = std::max (det, std::fabs (s[1]));
		}
		const double detDb = linearToDb (det);
		// How far the pre-gain-reduction signal sits over threshold. Used
		// both by the gain computer below and, for Signature, to drive the
		// output saturator -- unlike gain reduction, this tracks the Input
		// knob directly instead of being pinned by the compressor's own
		// feedback loop.
		const double over = detDb - mThresholdDb;

		//--- gain computer ----------------------------------------------
		double targetGr = 0.0;
		if (!mCompOff)
		{
			const double halfKnee = mKneeDb * 0.5;
			if (over >= halfKnee)
				targetGr = over * mRatioK;
			else if (over > -halfKnee)
			{
				const double t = over + halfKnee;
				targetGr = mRatioK * t * t / (2.0 * mKneeDb);
			}
			if (targetGr > 60.0)
				targetGr = 60.0;
		}

		//--- ballistics ---------------------------------------------------
		if (targetGr > grDb)
		{
			grDb += (targetGr - grDb) * mAttackCoef;
		}
		else if (mSignatureModel)
		{
			// The longer the compressor has been working, the slower it
			// lets go -- the 1176's dual time constant. CLEAN uses the
			// release knob's own time with no programme dependency.
			const double w = std::clamp (grSustain / 12.0, 0.0, 1.0);
			const double coef = mReleaseFastCoef + (mReleaseSlowCoef - mReleaseFastCoef) * w;
			grDb += (targetGr - grDb) * coef;
		}
		else
		{
			grDb += (targetGr - grDb) * mReleaseFastCoef;
		}
		grDb = flush (grDb);
		grSustain += (grDb - grSustain) * mSustainCoef;
		blockGrPeak = std::max (blockGrPeak, grDb);

		//--- auto make-up ------------------------------------------------
		makeup += (grDb - makeup) * mMakeupCoef;
		const double makeupLin = mAutoMakeup ? dbToLinear (std::clamp (makeup, 0.0, 30.0)) : 1.0;

		const double gr = dbToLinear (-grDb);
		// The FET is driven harder the hotter the input is -- from the
		// pre-gain-reduction level (`over`), not from the applied gain
		// reduction, which the compressor's own feedback pins to roughly
		// the same value regardless of the Input knob (see
		// SWEEP_ANALYSIS.md Finding 4). Unused in CLEAN, where mSatDrive is
		// always 1 and no saturation is applied.
		const double driveOver = mSignatureModel ? std::clamp (over, 0.0, 40.0) : 0.0;
		const double drive = mSatDrive * (1.0 + driveOver * 0.020);

		//--- output stage ------------------------------------------------
		double y[2] = {0.0, 0.0};
		for (int32 ch = 0; ch < numChannels; ch++)
		{
			double v = s[ch] * gr;

			if (mSignatureModel)
			{
				const double d = v * drive;
				double sat = signatureSaturate (d) / drive;

				// The asymmetric terms add DC; a 5 Hz one-pole high-pass removes it.
				const double hp = sat - mDcPrevIn[ch] + dcR * mDcState[ch];
				mDcPrevIn[ch] = sat;
				mDcState[ch] = flush (hp);
				v = mDcState[ch];
			}
			// CLEAN: the gain reduction above is the only thing that touches
			// the signal here -- no drive, no saturation, no DC blocker.

			v = v * mOutputGain * makeupLin;
			y[ch] = (dry * x[ch] + mix * v) * trim;
		}

		{
			double v = y[0];
			if (numChannels > 1)
				v = 0.5 * (y[0] + y[1]);
			blockOutSumSquares += v * v;
		}

		for (int32 ch = 0; ch < numChannels; ch++)
			out[ch][i] = static_cast<SampleType> (y[ch]);
	}

	mGrDb = grDb;
	mGrSustainDb = grSustain;
	mMakeupDb = makeup;
	mHumPhase = humPhase;
	mNoiseSeed = seed;

	//--- meter ballistics, once per block -------------------------------
	const double blockCoef = std::clamp (mVuCoef * static_cast<double> (numSamples), 0.0, 1.0);
	const double blockRelCoef = std::clamp (mVuReleaseCoef * static_cast<double> (numSamples), 0.0, 1.0);

	// IN and OUT integrate mean square, not peak: that is what a VU meter
	// measures, and it is the only way the numbers can be compared with a
	// hardware-style needle.
	const double invN = 1.0 / static_cast<double> (numSamples);
	mVuInMeanSquare += (blockInSumSquares * invN - mVuInMeanSquare) * blockCoef;
	mVuOutMeanSquare += (blockOutSumSquares * invN - mVuOutMeanSquare) * blockCoef;
	mVuInMeanSquare = flush (mVuInMeanSquare);
	mVuOutMeanSquare = flush (mVuOutMeanSquare);

	mMeterGrDb += (blockGrPeak - mMeterGrDb) * (blockGrPeak > mMeterGrDb ? blockCoef : blockRelCoef);

	// Clear any output channels the input did not supply.
	for (int32 ch = numChannels; ch < data.outputs[0].numChannels; ch++)
		std::memset (out[ch], 0, numSamples * sizeof (SampleType));

	// Never claim silence: with ANALOG engaged the output carries hum even
	// when the input is quiet, and claiming otherwise would let the host
	// mute a signal that is genuinely there.
	data.outputs[0].silenceFlags = 0;
}

//------------------------------------------------------------------------
void Glass76Processor::publishMeters (IParameterChanges* outChanges)
{
	if (!outChanges)
		return;

	const ParamID ids[4] = {kParamMeterGrId, kParamMeterInId, kParamMeterOutId,
	                        kParamMeterMakeupId};
	const double inVu = dbFsToVu (10.0 * std::log10 (std::max (mVuInMeanSquare, 1e-12)));
	const double outVu = dbFsToVu (10.0 * std::log10 (std::max (mVuOutMeanSquare, 1e-12)));

	const ParamValue values[4] = {
		grDbToNormalized (mMeterGrDb),
		levelDbToNormalized (inVu),
		levelDbToNormalized (outVu),
		// Reported whether or not the switch is on, so the UI can show what
		// auto make-up would add the moment it is engaged.
		grDbToNormalized (std::clamp (mMakeupDb, 0.0, 30.0))
	};

	for (int i = 0; i < 4; i++)
	{
		int32 queueIndex = 0;
		if (auto* queue = outChanges->addParameterData (ids[i], queueIndex))
		{
			int32 pointIndex = 0;
			queue->addPoint (0, values[i], pointIndex);
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::process (ProcessData& data)
{
	handleParameterChanges (data.inputParameterChanges);

	// Parameter-only flush: the host sends automation with no audio.
	if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
		return kResultOk;

	if (data.symbolicSampleSize == kSample32)
		processAudio<Sample32> (data);
	else
		processAudio<Sample64> (data);

	publishMeters (data.outputParameterChanges);

	return kResultOk;
}

//------------------------------------------------------------------------
// State. The processor owns the real state; the controller mirrors it in
// setComponentState. Both sides must read and write the same layout in the
// same order.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::getState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);
	if (!streamer.writeInt32 (kGlass76StateVersion))
		return kResultFalse;

	streamer.writeDouble (mInputNorm);
	streamer.writeDouble (mOutputNorm);
	streamer.writeDouble (mAttackNorm);
	streamer.writeDouble (mReleaseNorm);
	streamer.writeDouble (mRatioNorm);
	streamer.writeDouble (mMeterNorm);
	streamer.writeDouble (mAnalogNorm);
	streamer.writeDouble (mMixNorm);
	streamer.writeDouble (mTrimNorm);
	streamer.writeDouble (mModelNorm);
	streamer.writeBool (mAutoMakeup);
	streamer.writeBool (mCompOff);
	streamer.writeBool (mBypass);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Processor::setState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;
	if (version > kGlass76StateVersion)
		return kResultFalse; // written by a newer build; refuse rather than misread

	double inputNorm = 0.0, outputNorm = 0.0, attackNorm = 0.0, releaseNorm = 0.0;
	double ratioNorm = 0.0, meterNorm = 0.0, analogNorm = 0.0, mixNorm = 1.0, trimNorm = 0.5;
	double modelNorm = stepToNormalized (kModelDefaultStep, kModelStepCount);
	bool autoMakeup = false, compOff = false, bypass = false;

	if (!streamer.readDouble (inputNorm))   return kResultFalse;
	if (!streamer.readDouble (outputNorm))  return kResultFalse;
	if (!streamer.readDouble (attackNorm))  return kResultFalse;
	if (!streamer.readDouble (releaseNorm)) return kResultFalse;
	if (!streamer.readDouble (ratioNorm))   return kResultFalse;
	if (!streamer.readDouble (meterNorm))   return kResultFalse;
	if (!streamer.readDouble (analogNorm))  return kResultFalse;
	if (!streamer.readDouble (mixNorm))     return kResultFalse;
	if (!streamer.readDouble (trimNorm))    return kResultFalse;
	// v2: model, written right after trim. A version-1 stream ends here and
	// defaults to Signature -- exactly what it already sounded like.
	if (version >= 2)
	{
		if (!streamer.readDouble (modelNorm)) return kResultFalse;
	}
	if (!streamer.readBool (autoMakeup))    return kResultFalse;
	if (!streamer.readBool (compOff))       return kResultFalse;
	if (!streamer.readBool (bypass))        return kResultFalse;

	mInputNorm = inputNorm;
	mOutputNorm = outputNorm;
	mAttackNorm = attackNorm;
	mReleaseNorm = releaseNorm;
	mRatioNorm = ratioNorm;
	mMeterNorm = meterNorm;
	mAnalogNorm = analogNorm;
	mMixNorm = mixNorm;
	mTrimNorm = trimNorm;
	mModelNorm = modelNorm;
	mAutoMakeup = autoMakeup;
	mCompOff = compOff;
	mBypass = bypass;

	updateDerived ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Jaxson
