//------------------------------------------------------------------------
// Copyright (c) 2026 Jaxson
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include <cstdint>

namespace Jaxson {

//------------------------------------------------------------------------
// Glass76Processor -- the audio side. Runs on the realtime thread.
//
// Rules for everything below process():
//   no allocation, no locks, no file or GUI access, no exceptions.
// Allocate in setActive(true), free in setActive(false).
//
// Topology: a feed-forward peak-sensing FET compressor with a fixed
// threshold, stepped attenuators either side of it, program-dependent
// release, and a soft-clipping output stage whose drive tracks gain
// reduction. That last part is what makes it sound like an 1176 rather
// than like a clean compressor.
//------------------------------------------------------------------------
class Glass76Processor : public Steinberg::Vst::AudioEffect
{
public:
	Glass76Processor ();
	~Glass76Processor () SMTG_OVERRIDE;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return (Steinberg::Vst::IAudioProcessor*)new Glass76Processor;
	}

	//--- AudioEffect overrides ------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setBusArrangements (
	    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
	    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setupProcessing (
	    Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API canProcessSampleSize (
	    Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	/** No lookahead, so no reported latency. */
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return 0; }

	/** The release envelope runs out to 1.1 s at its slowest setting, and
	    auto make-up keeps lifting the noise floor while it does. Report a
	    real tail so FL Studio's Smart Disable does not cut the recovery
	    off mid-release. */
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE
	{
		return static_cast<Steinberg::uint32> (mSampleRate * 2.0);
	}

	//--- State ----------------------------------------------------------
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

//------------------------------------------------------------------------
protected:
	/** Pull the newest value of every automated parameter out of the block. */
	void handleParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	/** Recompute everything derived from the stepped parameters. Cheap, and
	    only called when something actually changed. */
	void updateDerived ();

	/** Push the three meter values back to the controller as read-only
	    output parameters. RT-safe: the host owns the queues. */
	void publishMeters (Steinberg::Vst::IParameterChanges* outChanges);

	template <typename SampleType>
	void processAudio (Steinberg::Vst::ProcessData& data);

	//--- parameter state, normalized 0..1, owned by the audio thread -----
	Steinberg::Vst::ParamValue mInputNorm {0.0};
	Steinberg::Vst::ParamValue mOutputNorm {0.0};
	Steinberg::Vst::ParamValue mAttackNorm {0.0};
	Steinberg::Vst::ParamValue mReleaseNorm {0.0};
	Steinberg::Vst::ParamValue mRatioNorm {0.0};
	Steinberg::Vst::ParamValue mMeterNorm {0.0};
	Steinberg::Vst::ParamValue mAnalogNorm {0.0};
	Steinberg::Vst::ParamValue mMixNorm {1.0};
	Steinberg::Vst::ParamValue mTrimNorm {0.5};
	bool mAutoMakeup {false};
	bool mCompOff {false};
	bool mBypass {false};

	//--- derived from the above, recomputed on change -------------------
	double mSampleRate {44100.0};
	double mInputGain {1.0};      // linear, includes the fixed make-up
	double mOutputGain {1.0};     // linear, includes the fixed make-up
	double mRatioK {3.0};         // (1 - 1/R), the gain-computer slope
	double mKneeDb {6.0};
	double mThresholdDb {-18.0};
	double mAttackCoef {0.5};
	double mReleaseFastCoef {0.001};
	double mReleaseSlowCoef {0.0003};
	double mSatDrive {1.0};
	double mHumPhaseInc {0.0};
	double mHumGain {0.0};
	double mNoiseGain {0.0};
	bool mAnalogOn {false};
	bool mAllButtonsIn {false};

	//--- running state, audio thread only --------------------------------
	double mGrDb {0.0};           // current gain reduction, dB, positive
	double mGrSustainDb {0.0};    // slow follower, drives program dependency
	double mMakeupDb {0.0};       // slow follower, drives auto make-up
	double mMakeupCoef {0.0};
	double mSustainCoef {0.0};
	double mHumPhase {0.0};
	double mDcState[2] {0.0, 0.0};
	double mDcPrevIn[2] {0.0, 0.0};
	uint32_t mNoiseSeed {0x9E3779B9u};

	//--- meter ballistics ------------------------------------------------
	double mMeterGrDb {0.0};
	double mMeterInDb {-60.0};
	double mMeterOutDb {-60.0};
	double mVuCoef {0.0};
	double mVuReleaseCoef {0.0};
};

//------------------------------------------------------------------------
} // namespace Jaxson
