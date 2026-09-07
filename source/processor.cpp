//------------------------------------------------------------------------
// Copyright (c) 2026 Jaxson
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

} // anonymous namespace

//------------------------------------------------------------------------
Glass76Processor::Glass76Processor ()
{
	setControllerClass (kGlass76ControllerUID);

	mInputNorm = stepToNormalized (kInputDefaultStep, kGainStepCount);
	mOutputNorm = stepToNormalized (kOutputDefaultStep, kGainStepCount);
	mAttackNorm = stepToNormalized (kAttackDefaultStep, kTimeStepCount);
	mReleaseNorm = stepToNormalized (kReleaseDefaultStep, kTimeStepCount);
	mRatioNorm = stepToNormalized (kRatioDefaultStep, kRatioStepCount);
	mMeterNorm = stepToNormalized (kMeterDefaultStep, kMeterStepCount);
	mAnalogNorm = stepToNormalized (kAnalogDefaultStep, kAnalogStepCount);
	mMixNorm = kMixDefaultPercent / 100.0;
	mTrimNorm = trimDbToNormalized (kTrimDefaultDb);
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
		mMeterGrDb = 0.0;
		mMeterInDb = kMeterLevelMinDb;
		mMeterOutDb = kMeterLevelMinDb;
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
	// Continuous, not detented: the printed marks are just a scale.
	mInputGain = dbToLinear (inputGainDb (mInputNorm));
	mOutputGain = dbToLinear (outputGainDb (mOutputNorm));

	const int ratioStep = normalizedToStep (mRatioNorm, kRatioStepCount);
	mAllButtonsIn = (ratioStep == kRatioAllStep);
	const double ratio = kRatioValues[clampIndex (ratioStep, kRatioStepCount)];
	mRatioK = 1.0 - 1.0 / ratio;

	// All-buttons-in: same nominal ratio, but the threshold drops, the knee
	// widens and the FET is driven far harder. That combination is what the
	// mode actually sounds like.
	mThresholdDb = kThresholdDb - (mAllButtonsIn ? 8.0 : 0.0);
	mKneeDb = mAllButtonsIn ? 12.0 : 6.0;

	const int attackStep = normalizedToStep (mAttackNorm, kTimeStepCount);
	const int releaseStep = normalizedToStep (mReleaseNorm, kTimeStepCount);
	double attackSeconds = attackStepToSeconds (attackStep);
	double releaseSeconds = releaseStepToSeconds (releaseStep);
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
	mAnalogOn = (analogStep != kAnalogOff);
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

	// Peak trackers for the meters, taken over the whole block.
	double blockInPeak = 0.0;
	double blockOutPeak = 0.0;
	double blockGrPeak = 0.0;

	for (int32 i = 0; i < numSamples; i++)
	{
		//--- read the frame, stereo-linked -----------------------------
		double x[2] = {0.0, 0.0};
		for (int32 ch = 0; ch < numChannels; ch++)
			x[ch] = static_cast<double> (in[ch][i]);

		double inPeak = std::fabs (x[0]);
		if (numChannels > 1)
			inPeak = std::max (inPeak, std::fabs (x[1]));
		blockInPeak = std::max (blockInPeak, inPeak);

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

		//--- gain computer ----------------------------------------------
		double targetGr = 0.0;
		if (!mCompOff)
		{
			double det = std::fabs (s[0]);
			if (numChannels > 1)
				det = std::max (det, std::fabs (s[1]));
			const double detDb = linearToDb (det);
			const double over = detDb - mThresholdDb;
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

		//--- ballistics, with programme-dependent release ----------------
		if (targetGr > grDb)
		{
			grDb += (targetGr - grDb) * mAttackCoef;
		}
		else
		{
			// The longer the compressor has been working, the slower it
			// lets go -- the 1176's dual time constant.
			const double w = std::clamp (grSustain / 12.0, 0.0, 1.0);
			const double coef = mReleaseFastCoef + (mReleaseSlowCoef - mReleaseFastCoef) * w;
			grDb += (targetGr - grDb) * coef;
		}
		grDb = flush (grDb);
		grSustain += (grDb - grSustain) * mSustainCoef;
		blockGrPeak = std::max (blockGrPeak, grDb);

		//--- auto make-up ------------------------------------------------
		makeup += (grDb - makeup) * mMakeupCoef;
		const double makeupLin = mAutoMakeup ? dbToLinear (std::clamp (makeup, 0.0, 30.0)) : 1.0;

		const double gr = dbToLinear (-grDb);
		// The FET is driven harder the harder it is working.
		const double drive = mSatDrive * (1.0 + grDb * 0.020);

		//--- output stage ------------------------------------------------
		double y[2] = {0.0, 0.0};
		for (int32 ch = 0; ch < numChannels; ch++)
		{
			double v = s[ch] * gr;

			// Asymmetric soft clip: second harmonic then a rational fold.
			const double d = v * drive;
			const double a = d + 0.10 * d * d;
			double sat = (a / std::sqrt (1.0 + a * a)) / drive;

			// The squared term adds DC; a 5 Hz one-pole high-pass removes it.
			const double hp = sat - mDcPrevIn[ch] + dcR * mDcState[ch];
			mDcPrevIn[ch] = sat;
			mDcState[ch] = flush (hp);
			sat = mDcState[ch];

			v = sat * mOutputGain * makeupLin;
			y[ch] = (dry * x[ch] + mix * v) * trim;
		}

		double outPeak = std::fabs (y[0]);
		if (numChannels > 1)
			outPeak = std::max (outPeak, std::fabs (y[1]));
		blockOutPeak = std::max (blockOutPeak, outPeak);

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

	const double inDb = linearToDb (blockInPeak);
	const double outDb = linearToDb (blockOutPeak);
	mMeterInDb += (inDb - mMeterInDb) * (inDb > mMeterInDb ? blockCoef : blockRelCoef);
	mMeterOutDb += (outDb - mMeterOutDb) * (outDb > mMeterOutDb ? blockCoef : blockRelCoef);
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

	const ParamID ids[3] = {kParamMeterGrId, kParamMeterInId, kParamMeterOutId};
	const ParamValue values[3] = {
		grDbToNormalized (mMeterGrDb),
		levelDbToNormalized (mMeterInDb),
		levelDbToNormalized (mMeterOutDb)
	};

	for (int i = 0; i < 3; i++)
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
	mAutoMakeup = autoMakeup;
	mCompOff = compOff;
	mBypass = bypass;

	updateDerived ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Jaxson
