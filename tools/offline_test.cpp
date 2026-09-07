//------------------------------------------------------------------------
// Copyright (c) 2026 Jaxson
//
// Offline test host for Glass76.
//
// Loads the built .vst3 bundle, feeds it known signals, and checks the
// things the SDK validator cannot: that the gain staging is calibrated
// where params.h says it is, that the compressor actually compresses,
// that mix and trim do what they claim, and that the meter parameters
// come back through outputParameterChanges.
//
// Build with -DGLASS76_BUILD_TESTS=ON, then run:
//   build\bin\Release\glass76_test.exe build\VST3\Release\Glass76.vst3
//------------------------------------------------------------------------

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include "../source/params.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Jaxson;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int32 kBlock = 512;

int gFailures = 0;

void check (bool ok, const char* what, const std::string& detail = {})
{
	std::printf ("  [%s] %s%s%s\n", ok ? "ok  " : "FAIL", what,
	             detail.empty () ? "" : " -- ", detail.c_str ());
	if (!ok)
		gFailures++;
}

std::string f2 (double v)
{
	char b[64];
	std::snprintf (b, sizeof (b), "%.2f", v);
	return b;
}

//------------------------------------------------------------------------
// A single run: set some parameters, push `seconds` of a test signal
// through, and report the output level plus whatever the plug-in wrote
// back into its meter parameters.
//------------------------------------------------------------------------
struct Result
{
	double outPeak {0.0};
	double outRms {0.0};
	double meterGrDb {0.0};
	double meterInDb {kMeterLevelMinDb};
	double meterOutDb {kMeterLevelMinDb};
};

class Runner
{
public:
	/** className empty = take the first audio effect in the bundle. Shells
	    (Waves, Slate, ...) hold hundreds, so those need a name. */
	bool open (const std::string& path, std::string& error,
	           const std::string& className = {})
	{
		mModule = VST3::Hosting::Module::create (path, error);
		if (!mModule)
			return false;

		const auto& factory = mModule->getFactory ();
		for (auto& info : factory.classInfos ())
		{
			if (info.category () != kVstAudioEffectClass)
				continue;
			if (!className.empty () && info.name () != className)
				continue;
			mProvider = owned (new PlugProvider (factory, info, true));
			if (!mProvider->initialize ())
			{
				error = "PlugProvider::initialize failed for " + info.name ();
				return false;
			}
			break;
		}
		if (!mProvider)
		{
			error = className.empty () ? "no audio effect class in the bundle"
			                           : "class not found: " + className;
			return false;
		}

		mComponent = mProvider->getComponentPtr ();
		mProcessor = FUnknownPtr<IAudioProcessor> (mComponent);
		if (!mProcessor)
		{
			error = "component does not implement IAudioProcessor";
			return false;
		}

		ProcessSetup setup {};
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = kSample32;
		setup.maxSamplesPerBlock = kBlock;
		setup.sampleRate = kSampleRate;
		if (mProcessor->setupProcessing (setup) != kResultOk)
		{
			error = "setupProcessing failed";
			return false;
		}
		mComponent->setActive (true);
		mProcessor->setProcessing (true);
		return true;
	}

	/** Print every parameter the plug-in exposes, with its current value. */
	void dumpParameters ()
	{
		auto controller = mProvider ? mProvider->getControllerPtr () : nullptr;
		if (!controller)
		{
			std::printf ("no edit controller\n");
			return;
		}
		const int32 n = controller->getParameterCount ();
		std::printf ("%d parameters\n", n);
		for (int32 i = 0; i < n; i++)
		{
			ParameterInfo info {};
			if (controller->getParameterInfo (i, info) != kResultOk)
				continue;
			const ParamValue norm = controller->getParamNormalized (info.id);
			String128 display {};
			controller->getParamStringByValue (info.id, norm, display);
			std::printf ("  id=%-6u steps=%-4d norm=%.4f  %-28s = %s\n",
			             (unsigned) info.id, (int) info.stepCount, norm,
			             VST3::StringConvert::convert (info.title).c_str (),
			             VST3::StringConvert::convert (display).c_str ());
		}
	}

	/** Set a parameter on the controller side too, so plug-ins that read
	    their state from the controller stay in sync. */
	IPtr<IEditController> controller () const
	{
		return mProvider ? mProvider->getControllerPtr () : nullptr;
	}

	~Runner ()
	{
		if (mProcessor)
			mProcessor->setProcessing (false);
		if (mComponent)
			mComponent->setActive (false);

		// Order matters. The module owns the DLL; releasing it while the
		// component and processor pointers are still live leaves their
		// destructors calling Release() on unmapped code.
		mProcessor = nullptr;
		mComponent = nullptr;
		mProvider = nullptr;
		mModule = nullptr;
	}

	/** amplitude is linear; frequency in Hz; params are (id, normalized).

	    preRollSeconds pushes silence through first, with the parameters
	    already applied. That lets one instance be reused for many
	    measurements: a compressor carries envelope state, and without the
	    pre-roll the previous run's release tail biases the next reading.
	    Reloading a 729-class plug-in shell per measurement is not viable. */
	Result run (double amplitude, double frequency, double seconds,
	            const std::vector<std::pair<ParamID, ParamValue>>& params,
	            double preRollSeconds = 0.0, double burstHz = 0.0)
	{
		Result result;

		// Push the settings through the controller as well. Plug-ins that
		// keep their real state controller-side (wrappers and shells do)
		// ignore inputParameterChanges alone.
		if (auto ctrl = controller ())
		{
			for (const auto& p : params)
				ctrl->setParamNormalized (p.first, p.second);
		}

		const int32 preRollBlocks =
		    static_cast<int32> (kSampleRate * preRollSeconds / kBlock);
		const int32 blocks = static_cast<int32> (kSampleRate * seconds / kBlock);

		// HostProcessData allocates the bus buffers from the component's own
		// bus arrangement, which is a lot safer than hand-rolling
		// AudioBusBuffers and getting a channel count subtly wrong.
		HostProcessData data;
		if (!data.prepare (*mComponent, kBlock, kSample32))
		{
			std::printf ("  HostProcessData::prepare failed\n");
			gFailures++;
			return result;
		}
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = kBlock;

		// Capacity matters: a ParameterChanges built with the default 0 has
		// no queues to hand out.
		ParameterChanges inChanges (16), outChanges (16);
		data.inputParameterChanges = &inChanges;
		data.outputParameterChanges = &outChanges;

		float* inL = data.inputs[0].channelBuffers32[0];
		float* inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
		float* outL = data.outputs[0].channelBuffers32[0];

		double phase = 0.0;
		int64 sampleCounter = 0;
		const double inc = 2.0 * 3.14159265358979323846 * frequency / kSampleRate;

		// Measure only the second half, once the envelope has settled.
		const int32 measureFrom = blocks / 2;
		double sumSquares = 0.0;
		int64 sumCount = 0;

		for (int32 b = -preRollBlocks; b < blocks; b++)
		{
			inChanges.clearQueue ();
			outChanges.clearQueue ();

			// Resend every parameter on every block. A host does exactly this
			// while a control is being held, and it removes any dependence on
			// a plug-in latching a one-shot change -- which the Waves shell
			// demonstrably does not do reliably.
			for (const auto& p : params)
			{
				int32 index = 0;
				if (auto* q = inChanges.addParameterData (p.first, index))
				{
					int32 point = 0;
					q->addPoint (0, p.second, point);
				}
			}

			const double amp = (b < 0) ? 0.0 : amplitude;
			for (int32 i = 0; i < kBlock; i++)
			{
				double a = amp;
				if (burstHz > 0.0 && b >= 0)
				{
					// Square-gated tone: loud half, then 20 dB down. A steady
					// sine cannot show how differently two compressors handle
					// an attack transient, which is where a clone usually
					// diverges audibly from its reference.
					const double t = static_cast<double> (sampleCounter) / kSampleRate;
					const double ph = t * burstHz;
					a *= ((ph - std::floor (ph)) < 0.5) ? 1.0 : 0.1;
				}
				const float s = static_cast<float> (a * std::sin (phase));
				if (b >= 0)
				{
					phase += inc;
					sampleCounter++;
				}
				inL[i] = s;
				inR[i] = s;
			}
			data.inputs[0].silenceFlags = 0;
			data.outputs[0].silenceFlags = 0;

			if (mProcessor->process (data) != kResultOk)
			{
				std::printf ("  process() failed\n");
				gFailures++;
				break;
			}

			if (b >= measureFrom)
			{
				for (int32 i = 0; i < kBlock; i++)
				{
					const double v = outL[i];
					result.outPeak = std::max (result.outPeak, std::fabs (v));
					sumSquares += v * v;
					sumCount++;
				}
			}

			// Drain the meter parameters the plug-in wrote back.
			for (int32 qi = 0; qi < outChanges.getParameterCount (); qi++)
			{
				auto* q = outChanges.getParameterData (qi);
				if (!q || q->getPointCount () <= 0)
					continue;
				int32 offset = 0;
				ParamValue value = 0.0;
				if (q->getPoint (q->getPointCount () - 1, offset, value) != kResultTrue)
					continue;
				switch (q->getParameterId ())
				{
					case kParamMeterGrId:  result.meterGrDb = normalizedToGrDb (value); break;
					case kParamMeterInId:  result.meterInDb = normalizedToLevelDb (value); break;
					case kParamMeterOutId: result.meterOutDb = normalizedToLevelDb (value); break;
					default: break;
				}
			}
		}

		result.outRms = sumCount > 0 ? std::sqrt (sumSquares / static_cast<double> (sumCount)) : 0.0;
		return result;
	}

private:
	VST3::Hosting::Module::Ptr mModule;
	IPtr<PlugProvider> mProvider;
	IPtr<IComponent> mComponent;
	IPtr<IAudioProcessor> mProcessor;
};

/** Normalized value for a stepped parameter, by step index. */
ParamValue step (int index, int count)
{
	return stepToNormalized (index, count);
}

//------------------------------------------------------------------------
// Reference comparison against the Waves CLA-76.
//------------------------------------------------------------------------
namespace waves {
// Parameter IDs read straight off the plug-in with --params.
constexpr ParamID kInput = 0, kOutput = 1, kAttack = 2, kRelease = 3;
constexpr ParamID kRatio = 4, kAnalog = 5, kRevision = 6, kMeter = 7;
constexpr ParamID kCompOff = 8, kMix = 9, kTrim = 10, kAutoMakeup = 11;

/** Its Input/Output are continuous, with the nine printed marks landing on
    even eighths of the normalized range -- norm 0.375 reads "-30.0 dB",
    which is mark index 3 of 8. */
inline ParamValue gainMark (int markIndex) { return markIndex / 8.0; }
/** Attack and release are continuous 1..7, linear in the knob number. */
inline ParamValue timePos (double position) { return (position - 1.0) / 6.0; }
} // namespace waves

int runComparison (const char* glassPath, const char* wavesPath)
{
	Runner glass, waves;
	std::string err;
	if (!glass.open (glassPath, err))
	{
		std::printf ("Glass76: %s\n", err.c_str ());
		return 1;
	}
	if (!waves.open (wavesPath, err, "CLA-76 Stereo"))
	{
		std::printf ("CLA-76: %s\n", err.c_str ());
		return 1;
	}

	std::printf ("\nGlass76 vs Waves CLA-76 -- 220 Hz sine, output level in dBFS RMS\n");
	std::printf ("%-34s %10s %10s %9s\n", "condition", "Glass76", "CLA-76", "delta");
	std::printf ("%s\n", std::string (66, '-').c_str ());

	auto line = [] (const std::string& label, double g, double w) {
		std::printf ("%-34s %10s %10s %+9.2f\n", label.c_str (),
		             f2 (g).c_str (), f2 (w).c_str (), g - w);
	};

	// Both plug-ins get: analog off, mix 100 %, trim 0, comp on/off as asked.
	auto glassParams = [] (int inMark, int outMark, int ratioStep, bool compOff) {
		return std::vector<std::pair<ParamID, ParamValue>> {
			{kParamInputId, inMark / 8.0},
			{kParamOutputId, outMark / 8.0},
			{kParamRatioId, step (ratioStep, kRatioStepCount)},
			{kParamAttackId, step (1, kTimeStepCount)},    // position 3
			{kParamReleaseId, step (2, kTimeStepCount)},   // position 5
			{kParamCompOffId, compOff ? 1.0 : 0.0},
			{kParamAutoMakeupId, 0.0},
			{kParamAnalogId, step (kAnalogOff, kAnalogStepCount)},
			{kParamMixId, 1.0},
			{kParamTrimId, trimDbToNormalized (0.0)},
		};
	};
	auto wavesParams = [] (int inMark, int outMark, int ratioStep, bool compOff) {
		return std::vector<std::pair<ParamID, ParamValue>> {
			{waves::kInput, waves::gainMark (inMark)},
			{waves::kOutput, waves::gainMark (outMark)},
			{waves::kRatio, step (ratioStep, kRatioStepCount)},
			{waves::kAttack, waves::timePos (3.0)},
			{waves::kRelease, waves::timePos (5.0)},
			{waves::kCompOff, compOff ? 1.0 : 0.0},
			{waves::kAutoMakeup, 0.0},
			{waves::kAnalog, step (2, 3)},                 // Off
			{waves::kMix, 1.0},
			{waves::kTrim, 0.5},
			{waves::kRevision, 1.0},                       // Blacky
		};
	};

	auto rms = [] (Runner& r, double amp, const std::vector<std::pair<ParamID, ParamValue>>& p) {
		// 0.6 s of silence with the settings applied resets the envelope, then
		// 2.5 s of tone; only the tail of that is measured.
		return linearToDb (r.run (amp, 220.0, 2.5, p, 1.5).outRms);
	};

	//--- 1. pure gain staging, compressor disabled --------------------
	std::printf ("\n[Comp Off -- pure gain staging]\n");
	for (int mark : {2, 3, 4, 5, 6})
	{
		const double amp = dbToLinear (-20.0);
		const double g = rms (glass, amp, glassParams (mark, mark, 3, true));
		const double w = rms (waves, amp, wavesParams (mark, mark, 3, true));
		line ("in=out=mark " + std::to_string (mark) + "  (-20 dBFS in)", g, w);
	}

	//--- 2. compressing, 4:1 ------------------------------------------
	std::printf ("\n[4:1, attack 3, release 5]\n");
	for (int inMark : {4, 5, 6, 7})
	{
		const double amp = dbToLinear (-20.0);
		const double g = rms (glass, amp, glassParams (inMark, 4, 3, false));
		const double w = rms (waves, amp, wavesParams (inMark, 4, 3, false));
		line ("input mark " + std::to_string (inMark) + ", output mark 4", g, w);
	}

	//--- 3. gain reduction -------------------------------------------
	// The CLA-76's Comp Off parameter does not respond through this host, so
	// GR cannot be taken as (bypassed - compressed). Instead: measure the
	// linear gain of each setting with a signal far below the threshold,
	// then GR at any level is (input + that linear gain) - measured output.
	// That needs nothing from either plug-in but its audio.
	auto linearGain = [&] (Runner& r, const std::vector<std::pair<ParamID, ParamValue>>& p) {
		const double probeDb = -50.0;
		return rms (r, dbToLinear (probeDb), p) - (probeDb - 3.01);
	};

	std::printf ("\n[gain reduction at 4:1, input mark 6, output mark 4]\n");
	{
		const double gGain = linearGain (glass, glassParams (6, 4, 3, false));
		const double wGain = linearGain (waves, wavesParams (6, 4, 3, false));
		std::printf ("  (linear gain below threshold: Glass76 %s dB, CLA-76 %s dB)\n",
		             f2 (gGain).c_str (), f2 (wGain).c_str ());
		for (double inDb : {-30.0, -24.0, -18.0, -12.0, -6.0})
		{
			const double amp = dbToLinear (inDb);
			const double inRms = inDb - 3.01;
			const double g = (inRms + gGain) - rms (glass, amp, glassParams (6, 4, 3, false));
			const double w = (inRms + wGain) - rms (waves, amp, wavesParams (6, 4, 3, false));
			line ("input " + f2 (inDb) + " dBFS -> GR", g, w);
		}
	}

	//--- 4. ratio sweep ------------------------------------------------
	std::printf ("\n[gain reduction by ratio, -12 dBFS in, input mark 6]\n");
	static const char* ratioNames[] = {"20:1", "12:1", "8:1", "4:1", "All"};
	for (int rs = 0; rs < kRatioStepCount; rs++)
	{
		const double amp = dbToLinear (-12.0);
		const double inRms = -12.0 - 3.01;
		const double gGain = linearGain (glass, glassParams (6, 4, rs, false));
		const double wGain = linearGain (waves, wavesParams (6, 4, rs, false));
		const double g = (inRms + gGain) - rms (glass, amp, glassParams (6, 4, rs, false));
		const double w = (inRms + wGain) - rms (waves, amp, wavesParams (6, 4, rs, false));
		line (std::string ("ratio ") + ratioNames[rs] + " -> GR", g, w);
	}

	//--- 5. transient material ----------------------------------------
	// The question that matters for "it sounds louder": a steady tone lets
	// both compressors settle, so it only tests the gain computer. A gated
	// tone repeatedly slams the attack, which is where two envelope
	// implementations actually diverge.
	std::printf ("\n[gated tone, 4 bursts/sec, output level -- input mark 6, output mark 4]\n");
	{
		auto burst = [&] (Runner& r, double amp,
		                  const std::vector<std::pair<ParamID, ParamValue>>& p) {
			return linearToDb (r.run (amp, 220.0, 3.0, p, 1.5, 4.0).outRms);
		};
		for (double inDb : {-18.0, -12.0, -6.0})
		{
			const double amp = dbToLinear (inDb);
			const double g = burst (glass, amp, glassParams (6, 4, 3, false));
			const double w = burst (waves, amp, wavesParams (6, 4, 3, false));
			line ("bursts at " + f2 (inDb) + " dBFS", g, w);
		}
		for (double pos : {1.0, 7.0})
		{
			auto gp = glassParams (6, 4, 3, false);
			auto wp = wavesParams (6, 4, 3, false);
			const int gStep = (pos <= 1.0) ? 0 : 3;   // position 1 or 7
			for (auto& e : gp)
			{
				if (e.first == kParamAttackId)
					e.second = step (gStep, kTimeStepCount);
			}
			for (auto& e : wp)
			{
				if (e.first == waves::kAttack)
					e.second = waves::timePos (pos);
			}
			const double amp = dbToLinear (-12.0);
			line ("bursts, attack position " + f2 (pos), burst (glass, amp, gp),
			      burst (waves, amp, wp));
		}
	}

	std::printf ("\nA positive delta means Glass76 is louder / reducing more.\n");
	return 0;
}

} // anonymous namespace

//------------------------------------------------------------------------
int main (int argc, char* argv[])
{
	if (argc < 2)
	{
		std::printf ("usage: glass76_test <path to Glass76.vst3>\n");
		std::printf ("       glass76_test --list <path to any .vst3>\n");
		return 2;
	}

	// Some plug-ins query the host application during initialize(); without a
	// context they get a null and fall over. Ours does not, but the shells we
	// probe with --list do.
	static HostApplication hostContext;
	PluginContextFactory::instance ().setPluginContext (&hostContext);

	// --list <module>: dump every audio-effect class in a bundle. Waves and
	// other vendors ship a "shell" holding hundreds of plug-ins, so the class
	// name is the only way to address one of them.
	if (std::string (argv[1]) == "--list" && argc >= 3)
	{
		std::string listError;
		auto module = VST3::Hosting::Module::create (argv[2], listError);
		if (!module)
		{
			std::printf ("could not load: %s\n", listError.c_str ());
			return 1;
		}
		int count = 0;
		for (auto& info : module->getFactory ().classInfos ())
		{
			if (info.category () != kVstAudioEffectClass)
				continue;
			std::printf ("%s\n", info.name ().c_str ());
			count++;
		}
		std::printf ("(%d audio effect classes)\n", count);
		return 0;
	}

	// --params <module> [class]: dump the parameter list of a plug-in, so a
	// foreign plug-in can be driven by ID without guessing.
	if (std::string (argv[1]) == "--params" && argc >= 3)
	{
		Runner runner;
		std::string perr;
		if (!runner.open (argv[2], perr, argc >= 4 ? argv[3] : std::string {}))
		{
			std::printf ("could not load: %s\n", perr.c_str ());
			return 1;
		}
		runner.dumpParameters ();
		return 0;
	}

	// --compare <glass76.vst3> <waveshell.vst3>
	//
	// Drives Glass76 and the Waves CLA-76 with identical signals and matched
	// settings and prints their output levels side by side. Calibrating a
	// clone against screenshots of its meters is guesswork; this measures the
	// reference directly.
	if (std::string (argv[1]) == "--compare" && argc >= 4)
		return runComparison (argv[2], argv[3]);

	std::string error;

	// Every run gets a fresh instance: a compressor carries envelope state,
	// and reusing one instance would leak the previous test's release tail
	// into the next measurement.
	auto measure = [&] (double amp, double freq, double seconds,
	                    const std::vector<std::pair<ParamID, ParamValue>>& params,
	                    Result& out) -> bool {
		Runner runner;
		if (!runner.open (argv[1], error))
		{
			std::printf ("could not load plug-in: %s\n", error.c_str ());
			return false;
		}
		out = runner.run (amp, freq, seconds, params);
		return true;
	};

	// -24 dB detent on both attenuators is the documented unity setting.
	const ParamValue unityGain = step (kInputDefaultStep, kGainStepCount);
	const ParamValue ratio4 = step (kRatioDefaultStep, kRatioStepCount);
	const ParamValue compOn = 0.0;
	const auto baseline = std::vector<std::pair<ParamID, ParamValue>> {
		{kParamInputId, unityGain},
		{kParamOutputId, unityGain},
		{kParamRatioId, ratio4},
		{kParamCompOffId, compOn},
		{kParamAutoMakeupId, 0.0},
		{kParamMixId, 1.0},
		{kParamTrimId, trimDbToNormalized (0.0)},
		{kParamAnalogId, step (kAnalogOff, kAnalogStepCount)},
	};

	auto withParam = [&] (ParamID id, ParamValue v) {
		auto p = baseline;
		for (auto& entry : p)
		{
			if (entry.first == id)
			{
				entry.second = v;
				return p;
			}
		}
		p.push_back ({id, v});
		return p;
	};

	//--- 1. Unity gain well below the threshold -----------------------
	// -40 dBFS is 22 dB under the -18 dBFS threshold, so nothing should
	// happen to it beyond the FET stage's (negligible) softening.
	std::printf ("\nGain staging\n");
	{
		const double amp = dbToLinear (-40.0);
		Result r;
		if (!measure (amp, 220.0, 1.0, baseline, r))
			return 1;
		const double gainDb = linearToDb (r.outPeak) - (-40.0);
		check (std::fabs (gainDb) < 0.5,
		       "-24 dB on both attenuators is unity below threshold",
		       f2 (gainDb) + " dB of error");
		check (r.meterGrDb < 0.5, "no gain reduction below threshold",
		       f2 (r.meterGrDb) + " dB");
	}

	//--- 2. Compression above the threshold ---------------------------
	std::printf ("\nCompression\n");
	{
		// -6 dBFS is 12 dB over the threshold. At 4:1 the gain computer
		// should give 12 * (1 - 1/4) = 9 dB of reduction.
		const double amp = dbToLinear (-6.0);
		Result r;
		if (!measure (amp, 220.0, 1.5, baseline, r))
			return 1;
		check (r.meterGrDb > 6.0 && r.meterGrDb < 11.0,
		       "4:1 at 12 dB over threshold gives ~9 dB reduction",
		       f2 (r.meterGrDb) + " dB");

		const double outDb = linearToDb (r.outPeak);
		check (outDb < -10.0, "output is pulled down accordingly",
		       f2 (outDb) + " dBFS");
	}
	{
		// 20:1 must reduce more than 4:1 on the same signal.
		const double amp = dbToLinear (-6.0);
		Result r4, r20;
		if (!measure (amp, 220.0, 1.5, baseline, r4))
			return 1;
		if (!measure (amp, 220.0, 1.5, withParam (kParamRatioId, step (0, kRatioStepCount)), r20))
			return 1;
		check (r20.meterGrDb > r4.meterGrDb + 2.0,
		       "20:1 reduces more than 4:1",
		       f2 (r4.meterGrDb) + " -> " + f2 (r20.meterGrDb) + " dB");
	}
	{
		// Comp Off must stop the gain reduction without muting anything.
		const double amp = dbToLinear (-6.0);
		Result r;
		if (!measure (amp, 220.0, 1.0, withParam (kParamCompOffId, 1.0), r))
			return 1;
		check (r.meterGrDb < 0.5, "Comp Off disables gain reduction",
		       f2 (r.meterGrDb) + " dB");
		check (r.outPeak > dbToLinear (-8.0), "Comp Off still passes audio",
		       f2 (linearToDb (r.outPeak)) + " dBFS");
	}

	//--- 3. Auto make-up ----------------------------------------------
	std::printf ("\nAuto make-up\n");
	{
		const double amp = dbToLinear (-6.0);
		Result off, on;
		if (!measure (amp, 220.0, 2.0, baseline, off))
			return 1;
		if (!measure (amp, 220.0, 2.0, withParam (kParamAutoMakeupId, 1.0), on))
			return 1;
		const double lift = linearToDb (on.outPeak) - linearToDb (off.outPeak);
		check (lift > 4.0,
		       "auto make-up puts the reduced level back",
		       f2 (lift) + " dB of lift");
	}

	//--- 4. Mix and Trim ----------------------------------------------
	std::printf ("\nMix and trim\n");
	{
		const double amp = dbToLinear (-6.0);
		Result dry;
		if (!measure (amp, 220.0, 1.0, withParam (kParamMixId, 0.0), dry))
			return 1;
		const double err = linearToDb (dry.outPeak) - (-6.0);
		check (std::fabs (err) < 0.3, "Mix at 0% is the dry signal untouched",
		       f2 (err) + " dB of error");
	}
	{
		const double amp = dbToLinear (-40.0);
		Result plus, minus;
		if (!measure (amp, 220.0, 1.0, withParam (kParamTrimId, trimDbToNormalized (6.0)), plus))
			return 1;
		if (!measure (amp, 220.0, 1.0, withParam (kParamTrimId, trimDbToNormalized (-6.0)), minus))
			return 1;
		const double up = linearToDb (plus.outPeak) - (-40.0);
		const double down = linearToDb (minus.outPeak) - (-40.0);
		check (std::fabs (up - 6.0) < 0.4, "Trim +6 dB", f2 (up) + " dB");
		check (std::fabs (down + 6.0) < 0.4, "Trim -6 dB", f2 (down) + " dB");
	}

	//--- 5. Attenuators -----------------------------------------------
	std::printf ("\nAttenuators\n");
	{
		const double amp = dbToLinear (-40.0);
		Result r;
		if (!measure (amp, 220.0, 0.5, withParam (kParamInputId, step (0, kGainStepCount)), r))
			return 1;
		check (r.outPeak < 1e-6, "Input at -inf is silence",
		       f2 (linearToDb (r.outPeak)) + " dBFS");
	}
	{
		// Output detent 8 (0 dB) is 24 dB above the unity detent.
		const double amp = dbToLinear (-40.0);
		Result r;
		if (!measure (amp, 220.0, 0.5, withParam (kParamOutputId, step (8, kGainStepCount)), r))
			return 1;
		const double lift = linearToDb (r.outPeak) - (-40.0);
		check (std::fabs (lift - 24.0) < 0.6, "Output detent 0 dB is +24 dB of make-up",
		       f2 (lift) + " dB");
	}

	//--- 6. Analog ----------------------------------------------------
	std::printf ("\nAnalog\n");
	{
		// With no input at all, ANALOG should put a small, quiet, non-zero
		// hum into the output -- and OFF should leave true silence.
		Result off, on50;
		if (!measure (0.0, 220.0, 0.5, baseline, off))
			return 1;
		if (!measure (0.0, 220.0, 0.5,
		              withParam (kParamAnalogId, step (kAnalog50, kAnalogStepCount)), on50))
			return 1;
		check (off.outPeak < 1e-7, "Analog off is silent",
		       f2 (linearToDb (off.outPeak)) + " dBFS");
		const double humDb = linearToDb (on50.outPeak);
		check (on50.outPeak > 0.0 && humDb < -60.0,
		       "Analog 50 Hz adds an audible-floor-level hum, well below signal",
		       f2 (humDb) + " dBFS");
	}

	//--- 7. Meters ----------------------------------------------------
	std::printf ("\nMeters\n");
	{
		const double amp = dbToLinear (-12.0);
		Result r;
		if (!measure (amp, 220.0, 2.0, baseline, r))
			return 1;
		check (std::fabs (r.meterInDb - (-12.0)) < 1.5, "IN meter tracks the input",
		       f2 (r.meterInDb) + " dBFS");
		check (r.meterOutDb < r.meterInDb + 0.5, "OUT meter is at or below IN while compressing",
		       f2 (r.meterOutDb) + " dBFS");
	}

	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
