//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
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
#include "../source/prefs.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

	/** Push a prepared mono buffer through the plug-in (duplicated to both
	    channels) and return the output RMS in dBFS. The first 25 % is
	    discarded so the envelope has settled before anything is measured. */
	double runBuffer (const std::vector<float>& input,
	                  const std::vector<std::pair<ParamID, ParamValue>>& params)
	{
		if (auto ctrl = controller ())
		{
			for (const auto& p : params)
				ctrl->setParamNormalized (p.first, p.second);
		}

		HostProcessData data;
		if (!data.prepare (*mComponent, kBlock, kSample32))
			return -300.0;
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = kBlock;

		ParameterChanges inChanges (16), outChanges (16);
		data.inputParameterChanges = &inChanges;
		data.outputParameterChanges = &outChanges;

		float* inL = data.inputs[0].channelBuffers32[0];
		float* inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
		float* outL = data.outputs[0].channelBuffers32[0];

		const int32 blocks = static_cast<int32> (input.size ()) / kBlock;
		const int32 measureFrom = blocks / 4;
		double sumSquares = 0.0;
		int64 count = 0;

		for (int32 b = 0; b < blocks; b++)
		{
			inChanges.clearQueue ();
			outChanges.clearQueue ();
			for (const auto& p : params)
			{
				int32 index = 0;
				if (auto* q = inChanges.addParameterData (p.first, index))
				{
					int32 point = 0;
					q->addPoint (0, p.second, point);
				}
			}
			for (int32 i = 0; i < kBlock; i++)
			{
				inL[i] = input[b * kBlock + i];
				inR[i] = inL[i];
			}
			data.inputs[0].silenceFlags = 0;
			data.outputs[0].silenceFlags = 0;
			if (mProcessor->process (data) != kResultOk)
				return -300.0;
			if (b >= measureFrom)
			{
				for (int32 i = 0; i < kBlock; i++)
				{
					const double v = outL[i];
					sumSquares += v * v;
					count++;
				}
			}
		}
		if (count == 0)
			return -300.0;
		return 10.0 * std::log10 (std::max (sumSquares / count, 1e-30));
	}

	/** Stream a raw interleaved-stereo float32 file through the plug-in. */
	bool processRawFile (const char* inPath, const char* outPath,
	                     const std::vector<std::pair<ParamID, ParamValue>>& params)
	{
		FILE* fin = std::fopen (inPath, "rb");
		if (!fin)
		{
			std::printf ("cannot open %s\n", inPath);
			return false;
		}
		FILE* fout = std::fopen (outPath, "wb");
		if (!fout)
		{
			std::fclose (fin);
			std::printf ("cannot write %s\n", outPath);
			return false;
		}

		if (auto ctrl = controller ())
		{
			for (const auto& p : params)
				ctrl->setParamNormalized (p.first, p.second);
		}

		HostProcessData data;
		if (!data.prepare (*mComponent, kBlock, kSample32))
		{
			std::fclose (fin);
			std::fclose (fout);
			return false;
		}
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = kBlock;

		ParameterChanges inChanges (16), outChanges (16);
		data.inputParameterChanges = &inChanges;
		data.outputParameterChanges = &outChanges;

		float* inL = data.inputs[0].channelBuffers32[0];
		float* inR = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : inL;
		float* outL = data.outputs[0].channelBuffers32[0];
		float* outR = data.outputs[0].numChannels > 1 ? data.outputs[0].channelBuffers32[1] : outL;

		std::vector<float> inter (kBlock * 2);
		int64 frames = 0;
		while (true)
		{
			const size_t got = std::fread (inter.data (), sizeof (float), kBlock * 2, fin);
			const int32 n = static_cast<int32> (got / 2);
			if (n <= 0)
				break;
			for (int32 i = 0; i < kBlock; i++)
			{
				inL[i] = (i < n) ? inter[i * 2] : 0.f;
				inR[i] = (i < n) ? inter[i * 2 + 1] : 0.f;
			}
			inChanges.clearQueue ();
			outChanges.clearQueue ();
			for (const auto& p : params)
			{
				int32 index = 0;
				if (auto* q = inChanges.addParameterData (p.first, index))
				{
					int32 point = 0;
					q->addPoint (0, p.second, point);
				}
			}
			data.inputs[0].silenceFlags = 0;
			data.outputs[0].silenceFlags = 0;
			if (mProcessor->process (data) != kResultOk)
				break;
			for (int32 i = 0; i < n; i++)
			{
				inter[i * 2] = outL[i];
				inter[i * 2 + 1] = outR[i];
			}
			std::fwrite (inter.data (), sizeof (float), n * 2, fout);
			frames += n;
		}
		std::fclose (fin);
		std::fclose (fout);
		std::printf ("processed %lld frames\n", (long long) frames);
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

//------------------------------------------------------------------------
// A stand-in for a dense modern master: a decaying 50 Hz sub on every
// beat, a noise transient on every off-beat, and a sustained mid tone
// underneath, peak-normalised. Crest factor lands around 8 dB, which is
// roughly what a loud trap master has.
//------------------------------------------------------------------------
std::vector<float> makeProgramMaterial (double seconds, double bpm, double peakDbFs)
{
	const int n = static_cast<int> (kSampleRate * seconds);
	std::vector<float> buf (n, 0.f);
	const double beat = 60.0 / bpm;
	uint32_t seed = 0x12345678u;

	for (int i = 0; i < n; i++)
	{
		const double t = i / kSampleRate;
		const double inBeat = std::fmod (t, beat);
		const double inHalf = std::fmod (t, beat * 0.5);

		// 808: decaying sine, the loudest sustained element.
		double v = std::sin (2.0 * 3.14159265358979 * 50.0 * t) *
		           std::exp (-inBeat * 5.0) * 0.9;
		// Hat: very short noise burst on the off-beat -- the transient.
		seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
		const double noise = (static_cast<double> (seed) * 2.3283064365386963e-10 - 0.5) * 2.0;
		v += noise * std::exp (-inHalf * 900.0) * 0.7;
		// A sustained mid element so the compressor never fully releases.
		v += std::sin (2.0 * 3.14159265358979 * 330.0 * t) * 0.18;

		buf[i] = static_cast<float> (v);
	}

	double peak = 0.0;
	for (float s : buf)
		peak = std::max (peak, std::fabs (static_cast<double> (s)));
	const double scale = (peak > 0.0) ? dbToLinear (peakDbFs) / peak : 1.0;
	for (float& s : buf)
		s = static_cast<float> (s * scale);
	return buf;
}

/** Push a prepared buffer through a plug-in and return its output RMS in
    dBFS. Uses its own fresh instance, opened by the caller. */
double runBuffer (Runner& r, const std::vector<float>& input,
                  const std::vector<std::pair<ParamID, ParamValue>>& params)
{
	return r.runBuffer (input, params);
}

int runProgramComparison (const char* glassPath, const char* wavesPath)
{
	const auto material = makeProgramMaterial (4.0, 140.0, -0.3);
	double inSum = 0.0;
	for (float s : material)
		inSum += static_cast<double> (s) * s;
	const double inRmsDb = 10.0 * std::log10 (inSum / material.size ());
	std::printf ("\nProgram material: %.1f s, peak -0.3 dBFS, RMS %s dBFS "
	             "(crest %s dB)\n",
	             material.size () / kSampleRate, f2 (inRmsDb).c_str (),
	             f2 (-0.3 - inRmsDb).c_str ());

	std::printf ("%-40s %9s %9s %8s\n", "settings", "Glass76", "CLA-76", "delta");
	std::printf ("%s\n", std::string (70, '-').c_str ());

	struct Case { const char* label; int inMark, outMark, ratioStep; bool autoMakeup; };
	static const Case cases[] = {
		{"in 4 / out 4, 4:1",            4, 4, 3, false},
		{"in 6 / out 4, 4:1",            6, 4, 3, false},
		{"in 6 / out 4, 20:1",           6, 4, 0, false},
		{"in 6 / out 4, 4:1, AUTO MAKEUP", 6, 4, 3, true},
	};

	for (const auto& c : cases)
	{
		std::vector<std::pair<ParamID, ParamValue>> gp {
			{kParamInputId, c.inMark / 8.0},
			{kParamOutputId, c.outMark / 8.0},
			{kParamRatioId, step (c.ratioStep, kRatioStepCount)},
			{kParamAttackId, kAttackDefaultNormalized},
			{kParamReleaseId, kReleaseDefaultNormalized},
			{kParamCompOffId, 0.0},
			{kParamAutoMakeupId, c.autoMakeup ? 1.0 : 0.0},
			{kParamAnalogId, step (kAnalogOff, kAnalogStepCount)},
			{kParamMixId, 1.0},
			{kParamTrimId, trimDbToNormalized (0.0)},
		};
		std::vector<std::pair<ParamID, ParamValue>> wp {
			{waves::kInput, waves::gainMark (c.inMark)},
			{waves::kOutput, waves::gainMark (c.outMark)},
			{waves::kRatio, step (c.ratioStep, kRatioStepCount)},
			{waves::kAttack, waves::timePos (3.0)},
			{waves::kRelease, waves::timePos (5.0)},
			{waves::kCompOff, 0.0},
			{waves::kAutoMakeup, c.autoMakeup ? 1.0 : 0.0},
			{waves::kAnalog, step (2, 3)},
			{waves::kMix, 1.0},
			{waves::kTrim, 0.5},
			{waves::kRevision, 1.0},
		};

		std::string err;
		double g = 0.0, w = 0.0;
		{
			Runner r;
			if (!r.open (glassPath, err))
			{
				std::printf ("Glass76: %s\n", err.c_str ());
				return 1;
			}
			g = runBuffer (r, material, gp);
		}
		{
			Runner r;
			if (!r.open (wavesPath, err, "CLA-76 Stereo"))
			{
				std::printf ("CLA-76: %s\n", err.c_str ());
				return 1;
			}
			w = runBuffer (r, material, wp);
		}
		std::printf ("%-40s %9s %9s %+8.2f\n", c.label, f2 (g).c_str (), f2 (w).c_str (), g - w);
	}

	std::printf ("\nA positive delta means Glass76 is louder.\n");
	return 0;
}

int runGainLaw (const char* glassPath, const char* wavesPath)
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

	auto glassParams = [] (double inNorm, double outNorm) {
		return std::vector<std::pair<ParamID, ParamValue>> {
			{kParamInputId, inNorm}, {kParamOutputId, outNorm},
			{kParamRatioId, step (3, kRatioStepCount)},
			{kParamAttackId, kAttackDefaultNormalized},
			{kParamReleaseId, kReleaseDefaultNormalized},
			{kParamCompOffId, 0.0}, {kParamAutoMakeupId, 0.0},
			{kParamAnalogId, step (kAnalogOff, kAnalogStepCount)},
			{kParamMixId, 1.0}, {kParamTrimId, trimDbToNormalized (0.0)},
		};
	};
	auto wavesParams = [] (double inNorm, double outNorm) {
		return std::vector<std::pair<ParamID, ParamValue>> {
			{waves::kInput, inNorm}, {waves::kOutput, outNorm},
			{waves::kRatio, step (3, kRatioStepCount)},
			{waves::kAttack, waves::timePos (3.0)},
			{waves::kRelease, waves::timePos (5.0)},
			{waves::kCompOff, 0.0}, {waves::kAutoMakeup, 0.0},
			{waves::kAnalog, step (2, 3)},
			{waves::kMix, 1.0}, {waves::kTrim, 0.5}, {waves::kRevision, 1.0},
		};
	};

	// -55 dBFS: quiet enough that nothing compresses even at the hottest
	// input mark, loud enough to stay clear of the plug-in's noise floor
	// (-70 dBFS sat in it and produced nonsense).
	const double probeDb = -55.0;
	const double probeRms = probeDb - 3.01;
	auto gainOf = [&] (Runner& r, const std::vector<std::pair<ParamID, ParamValue>>& p) {
		return linearToDb (r.run (dbToLinear (probeDb), 220.0, 2.0, p, 1.0).outRms) - probeRms;
	};

	// Linearity check: the probe must be below threshold, or these numbers
	// are gain reduction, not gain law.
	{
		auto p = wavesParams (4 / 8.0, 4 / 8.0);
		const double a = linearToDb (waves.run (dbToLinear (-55.0), 220.0, 2.0, p, 1.0).outRms);
		const double b = linearToDb (waves.run (dbToLinear (-50.0), 220.0, 2.0, p, 1.0).outRms);
		std::printf ("\nlinearity probe (CLA-76, marks 4/4): +5 dB in -> %+.2f dB out%s\n",
		             b - a, (std::fabs ((b - a) - 5.0) < 0.3) ? "  [clean]" : "  [COMPRESSING - suspect]");
	}

	std::printf ("\n%s\n", std::string (64, '=').c_str ());
	std::printf ("INPUT sweep (output held at mark 4 = -24 dB)\n");
	std::printf ("%s\n", std::string (64, '=').c_str ());
	std::printf ("  %-18s %12s %12s %9s\n", "input mark", "Glass76", "CLA-76", "delta");
	for (int m = 1; m <= 8; m++)
	{
		const double g = gainOf (glass, glassParams (m / 8.0, 4 / 8.0));
		const double w = gainOf (waves, wavesParams (m / 8.0, 4 / 8.0));
		std::printf ("  %d (%6.0f dB)     %12.2f %12.2f %+9.2f\n",
		             m, kGainStepsDb[m], g, w, g - w);
	}

	std::printf ("\n%s\n", std::string (64, '=').c_str ());
	std::printf ("OUTPUT sweep (input held at mark 4 = -24 dB)\n");
	std::printf ("%s\n", std::string (64, '=').c_str ());
	std::printf ("  %-18s %12s %12s %9s\n", "output mark", "Glass76", "CLA-76", "delta");
	for (int m = 1; m <= 8; m++)
	{
		const double g = gainOf (glass, glassParams (4 / 8.0, m / 8.0));
		const double w = gainOf (waves, wavesParams (4 / 8.0, m / 8.0));
		std::printf ("  %d (%6.0f dB)     %12.2f %12.2f %+9.2f\n",
		             m, kGainStepsDb[m], g, w, g - w);
	}

	std::printf ("\nThe user's render used input mark 3 (-30) / output mark 5 (-18):\n");
	{
		const double g = gainOf (glass, glassParams (3 / 8.0, 5 / 8.0));
		const double w = gainOf (waves, wavesParams (3 / 8.0, 5 / 8.0));
		std::printf ("  Glass76 %+.2f dB   CLA-76 %+.2f dB   delta %+.2f dB\n", g, w, g - w);
	}
	return 0;
}

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
			{kParamAttackId, kAttackDefaultNormalized},    // position 3
			{kParamReleaseId, kReleaseDefaultNormalized},   // position 5
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
			const double gNorm = (pos - 1.0) / 6.0;   // position 1..7 -> normalized
			for (auto& e : gp)
			{
				if (e.first == kParamAttackId)
					e.second = gNorm;
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

//------------------------------------------------------------------------
// Attack/Release went from a 4-detent StringListParameter to a continuous
// Parameter, both ends going through attackNormalizedToSeconds/
// releaseNormalizedToSeconds in params.h. Pure functions, no plugin or host
// needed -- checked directly here rather than only through a live drag,
// which a screen-automation click can fail to register for reasons that
// have nothing to do with whether the curve itself is right.
//------------------------------------------------------------------------
bool runParamMathTests ()
{
	using namespace Jaxson;
	const int before = gFailures;

	std::printf ("\nAttack / Release continuous curve\n");

	auto near = [] (double a, double b, double tolFrac) {
		return std::fabs (a - b) <= std::fabs (b) * tolFrac;
	};

	//--- the four printed positions still land where they always did -----
	// The curve itself never changed (see attackPositionToSeconds's own
	// comment); only the parameter and widget on top of it were stepped.
	// 1% tolerance covers rounding in the numbers this is checked against.
	{
		const double us1 = attackNormalizedToSeconds (0.0) * 1e6;
		const double us3 = attackNormalizedToSeconds (1.0 / 3.0) * 1e6;
		const double us5 = attackNormalizedToSeconds (2.0 / 3.0) * 1e6;
		const double us7 = attackNormalizedToSeconds (1.0) * 1e6;
		check (near (us1, 800.0, 0.01), "attack norm 0.0 (position 1) is 800 us", f2 (us1));
		check (near (us3, 234.0, 0.01), "attack norm 1/3 (position 3) is 234 us", f2 (us3));
		check (near (us5, 68.0, 0.01), "attack norm 2/3 (position 5) is 68 us", f2 (us5));
		check (near (us7, 20.0, 0.01), "attack norm 1.0 (position 7) is 20 us", f2 (us7));
	}
	{
		const double ms1 = releaseNormalizedToSeconds (0.0) * 1e3;
		const double ms3 = releaseNormalizedToSeconds (1.0 / 3.0) * 1e3;
		const double ms5 = releaseNormalizedToSeconds (2.0 / 3.0) * 1e3;
		const double ms7 = releaseNormalizedToSeconds (1.0) * 1e3;
		check (near (ms1, 1100.0, 0.01), "release norm 0.0 (position 1) is 1100 ms", f2 (ms1));
		check (near (ms3, 392.0, 0.01), "release norm 1/3 (position 3) is 392 ms", f2 (ms3));
		check (near (ms5, 140.0, 0.01), "release norm 2/3 (position 5) is 140 ms", f2 (ms5));
		check (near (ms7, 50.0, 0.01), "release norm 1.0 (position 7) is 50 ms", f2 (ms7));
	}

	//--- the defaults the controller/processor both seed from -------------
	check (std::fabs (kAttackDefaultNormalized - 1.0 / 3.0) < 1e-9,
	       "kAttackDefaultNormalized is exactly position 3");
	check (std::fabs (kReleaseDefaultNormalized - 2.0 / 3.0) < 1e-9,
	       "kReleaseDefaultNormalized is exactly position 5");

	//--- genuinely continuous: no plateau, no snap back to the old 4 -------
	// A stepped parameter would repeat the same seconds value across a
	// whole sub-range of norm; a continuous one changes at every step of
	// this sweep. This is the actual behavioural difference the "make it
	// continuous" request was about, and the one a slow visual drag can't
	// prove by itself even when it does register.
	{
		bool attackStrictlyMonotonic = true, releaseStrictlyMonotonic = true;
		double prevAttack = attackNormalizedToSeconds (0.0);
		double prevRelease = releaseNormalizedToSeconds (0.0);
		constexpr int kSweepSteps = 200;
		for (int i = 1; i <= kSweepSteps; i++)
		{
			const double n = static_cast<double> (i) / kSweepSteps;
			const double a = attackNormalizedToSeconds (n);
			const double r = releaseNormalizedToSeconds (n);
			if (a >= prevAttack)
				attackStrictlyMonotonic = false;
			if (r >= prevRelease)
				releaseStrictlyMonotonic = false;
			prevAttack = a;
			prevRelease = r;
		}
		check (attackStrictlyMonotonic,
		       "attack time strictly decreases across a 200-point sweep of norm 0..1");
		check (releaseStrictlyMonotonic,
		       "release time strictly decreases across a 200-point sweep of norm 0..1");
	}

	return gFailures == before;
}

//------------------------------------------------------------------------
// prefs.h/.cpp has no VSTGUI dependency, so its JSON reader/writer is
// tested directly here rather than only through the running plug-in. The
// pure parse()/serialize() cases below touch no filesystem at all; the
// last one exercises the real load()/save()/mtime() path against whatever
// Documents folder this machine actually has.
//------------------------------------------------------------------------
bool runPrefsTests ()
{
	using namespace Jaxson;
	const int before = gFailures;

	std::printf ("\nPreferences JSON\n");

	//--- round trip -------------------------------------------------------
	{
		Glass76Prefs p;
		p.version = 1;
		p.skin = "glass";
		p.appearance = "light";
		p.refreshRateHz = 120;
		p.backgroundImage = "C:\\Users\\jaxson\\Pictures\\wall.png";
		p.scalePercent = 150;
		p.transparentBackground = true;

		const std::string json = prefs::serialize (p);
		Glass76Prefs back;
		const bool ok = prefs::parse (json, back);
		check (ok && back.version == p.version && back.skin == p.skin &&
		           back.appearance == p.appearance && back.refreshRateHz == p.refreshRateHz &&
		           back.backgroundImage == p.backgroundImage && back.scalePercent == p.scalePercent &&
		           back.transparentBackground == p.transparentBackground,
		       "round trip preserves every field, including a backslash path", json);
	}

	//--- empty object: a successful parse, holding the struct's own -------
	// defaults. parse() always fills `out` from a fresh Glass76Prefs{} on
	// success -- it is `false` (a malformed file) that must never touch
	// `out` at all, not a well-formed-but-empty object.
	{
		Glass76Prefs p;
		p.skin = "sentinel";
		const bool ok = prefs::parse ("{}", p);
		check (ok && p.skin == Glass76Prefs {}.skin, "empty object parses ok, holding built-in defaults");
	}

	//--- missing file entirely: distinct from a malformed one -------------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse ("", p);
		check (!ok, "empty string is not a valid object");
	}

	//--- unknown keys are ignored, not fatal -------------------------------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (
		    R"({"skin":"glass","unknownKey":"ignored","anotherOne":42})", p);
		check (ok && p.skin == "glass", "unknown keys are ignored rather than rejected");
	}

	//--- a future version's nested object/array fields are skipped --------
	// This is the forward-compat hatch: a later build might add
	// "skins": { "hardware": {...} } and today's binary must still read
	// everything it understands out of the rest of the file.
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (
		    R"({"future":{"a":[1,2,{"b":"c\"d"}],"c":{}},)"
		    R"("skin":"hardware","trailingArray":[1,[2,3],"x"]})",
		    p);
		check (ok && p.skin == "hardware",
		       "a nested object/array field is skipped, not a parse error");
	}

	//--- trailing comma before the closing brace ---------------------------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"skin":"glass","appearance":"dark",})", p);
		check (ok && p.skin == "glass" && p.appearance == "dark",
		       "a trailing comma before '}' is tolerated");
	}

	//--- a UTF-8 BOM ahead of the object ------------------------------------
	{
		Glass76Prefs p;
		const std::string withBom = "\xEF\xBB\xBF" R"({"skin":"glass"})";
		const bool ok = prefs::parse (withBom, p);
		check (ok && p.skin == "glass", "a leading UTF-8 BOM is skipped");
	}

	//--- an unrecognised escape keeps its literal character, not the file -
	// A hand-edited preferences.json with a single backslash (rather than
	// our own writer's doubled "\\\\") must not corrupt the rest of the
	// object -- only the malformed run inside that one string is affected.
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"backgroundImage":"C:\Users\jaxson","skin":"glass"})", p);
		check (ok && p.skin == "glass" && p.backgroundImage == "C:Usersjaxson",
		       "an unrecognised \\U / \\j escape degrades to the literal letter",
		       p.backgroundImage);
	}

	//--- numbers parse correctly under a comma-decimal C locale -----------
	// std::from_chars is locale-independent by design; strtod() is not, and
	// a host that has changed the C locale would otherwise corrupt this.
	{
		const char* prevLocale = std::setlocale (LC_NUMERIC, nullptr);
		const std::string saved = prevLocale ? prevLocale : "C";
		std::setlocale (LC_NUMERIC, "German");   // no-op if this build has no such locale

		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"version":1.0,"refreshRateHz":60,"skin":"glass"})", p);
		check (ok && p.refreshRateHz == 60 && p.skin == "glass",
		       "numeric fields parse correctly under a comma-decimal locale");

		std::setlocale (LC_NUMERIC, saved.c_str ());
	}

	//--- truncated file -----------------------------------------------------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"skin": "glass")", p);   // missing closing brace
		check (!ok, "a truncated object is rejected, not partially applied");
	}

	//--- garbage file ---------------------------------------------------
	{
		Glass76Prefs p;
		check (!prefs::parse ("not json at all", p), "non-JSON text is rejected");
		check (!prefs::parse (std::string ("\x00\x01\x02", 3), p), "binary garbage is rejected");
	}

	//--- refreshRateHz outside {30,60,120} is not silently accepted -------
	// prefs::parse() itself is a pure field copy (snapping is the loader's
	// job, matching RootView::setRefreshRateHz / the controller's own
	// setter) -- this just confirms the raw value round-trips so the
	// caller-side snap has something correct to snap.
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"refreshRateHz":45})", p);
		check (ok && p.refreshRateHz == 45,
		       "an out-of-set refresh rate still parses -- snapping is the loader's job, not parse()'s");
	}

	//--- scalePercent outside {25,50,100,150,200}: same story ------------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"scalePercent":33})", p);
		check (ok && p.scalePercent == 33,
		       "an out-of-set scale percent still parses -- snapping is the loader's job, not parse()'s");
	}

	//--- a missing scalePercent leaves the struct's built-in default ------
	{
		Glass76Prefs p;
		const bool ok = prefs::parse (R"({"skin":"glass"})", p);
		check (ok && p.scalePercent == Glass76Prefs {}.scalePercent,
		       "a file predating the window-scale feature leaves scalePercent at its default");
	}

	//--- transparentBackground: true, false, and a missing-key default ----
	{
		Glass76Prefs p;
		bool ok = prefs::parse (R"({"transparentBackground":true})", p);
		check (ok && p.transparentBackground == true, "transparentBackground: true parses");

		p = Glass76Prefs {};
		ok = prefs::parse (R"({"transparentBackground":false})", p);
		check (ok && p.transparentBackground == false, "transparentBackground: false parses");

		p = Glass76Prefs {};
		ok = prefs::parse (R"({"skin":"glass"})", p);
		check (ok && p.transparentBackground == Glass76Prefs {}.transparentBackground,
		       "a file predating the transparent-background feature leaves it at its default");
	}

	//--- the real filesystem path: save, then load back -------------------
	// Exercises path resolution, atomic write, and mtime() together. Skips
	// itself (without counting as a failure) on a machine where Documents
	// could not be resolved at all -- prefs::dir() returning empty is a
	// documented, handled condition, not a bug.
	//
	// This is the one test in the file that touches the real filesystem
	// path, not a pure in-memory parse/serialize -- which means it was
	// overwriting whatever real preferences.json already existed at
	// Documents\Glass76 with the test's own values, with nothing to put a
	// user's actual settings back afterwards. Concretely: running
	// --prefs-test on a machine where Glass76 had already been used would
	// silently reset that user's skin/appearance/refresh rate/scale/
	// background image to whatever this test happened to write. Back up
	// whatever is there (bytes, not a parsed struct -- a malformed or
	// hand-edited file must round-trip through this unharmed too) before
	// touching the file, and restore it (or remove the file entirely, if
	// there wasn't one) once the test below is done, success or failure.
	{
		if (prefs::dir ().empty ())
		{
			std::printf ("  [skip] filesystem round trip -- Documents could not be resolved here\n");
		}
		else
		{
			std::string originalContent;
			bool hadOriginal = false;
			{
				std::ifstream backupIn (std::filesystem::u8path (prefs::path ()), std::ios::binary);
				if (backupIn)
				{
					originalContent.assign ((std::istreambuf_iterator<char> (backupIn)),
					                        std::istreambuf_iterator<char> ());
					hadOriginal = true;
				}
			}

			Glass76Prefs p;
			p.version = 1;
			p.skin = "hardware";
			p.appearance = "dark";
			p.refreshRateHz = 30;
			p.backgroundImage.clear ();
			p.scalePercent = 200;
			p.transparentBackground = true;

			const int64_t before64 = prefs::mtime ();
			const bool saved = prefs::save (p);
			check (saved, "save() writes preferences.json", prefs::path ());

			Glass76Prefs back;
			const bool loaded = saved && prefs::load (back);
			check (loaded && back.skin == p.skin && back.appearance == p.appearance &&
			           back.refreshRateHz == p.refreshRateHz && back.scalePercent == p.scalePercent &&
			           back.transparentBackground == p.transparentBackground,
			       "load() reads back exactly what save() wrote");

			const int64_t after64 = prefs::mtime ();
			check (!saved || after64 != 0, "mtime() reports a write time after save()");
			(void) before64;

			// Restore whatever was really there -- see the comment above this
			// block. Best-effort: a failure here is not this test's to report,
			// since prefs::save() itself already exercised (and, if it failed,
			// already checked) the write path above.
			if (hadOriginal)
			{
				std::ofstream restoreOut (std::filesystem::u8path (prefs::path ()),
				                          std::ios::binary | std::ios::trunc);
				if (restoreOut)
					restoreOut.write (originalContent.data (),
					                  static_cast<std::streamsize> (originalContent.size ()));
			}
			else
			{
				std::error_code ec;
				std::filesystem::remove (std::filesystem::u8path (prefs::path ()), ec);
			}
		}
	}

	return gFailures == before;
}

} // anonymous namespace

//------------------------------------------------------------------------
int main (int argc, char* argv[])
{
	if (argc < 2)
	{
		std::printf ("usage: glass76_test <path to Glass76.vst3>\n");
		std::printf ("       glass76_test --list <path to any .vst3>\n");
		std::printf ("       glass76_test --prefs-test\n");
		std::printf ("       glass76_test --param-test\n");
		return 2;
	}

	// --prefs-test: exercises prefs.h/.cpp directly. No .vst3 needed -- this
	// is pure C++ with no VSTGUI or host dependency at all.
	if (std::string (argv[1]) == "--prefs-test")
		return runPrefsTests () ? 0 : 1;

	// --param-test: exercises the continuous attack/release curve in
	// params.h directly. Same story -- pure C++, no plugin or host needed.
	if (std::string (argv[1]) == "--param-test")
		return runParamMathTests () ? 0 : 1;

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

	// --program <glass76.vst3> <waveshell.vst3>
	//
	// A steady tone only exercises the gain computer. This drives both
	// plug-ins with dense, transient-heavy material at a modern master's
	// level and crest factor, which is where a clone actually diverges.
	// Each measurement gets a fresh plug-in instance, because the Waves
	// shell latches settings unreliably when one is reused.
	if (std::string (argv[1]) == "--program" && argc >= 4)
		return runProgramComparison (argv[2], argv[3]);

	// --gainlaw <glass76.vst3> <waveshell.vst3>
	//
	// Sweeps each attenuator on its own with a signal far below the
	// threshold, so gain reduction is zero and what comes out is purely the
	// control's gain law. A real render showed the CLA-76 does not treat its
	// two controls as matching 1:1 dB attenuators, and this is what pins
	// down the actual curve.
	if (std::string (argv[1]) == "--gainlaw" && argc >= 4)
		return runGainLaw (argv[2], argv[3]);

	// --processraw <plugin> <in.raw> <out.raw> <inMark> <outMark> [ratioStep]
	//
	// Runs real material (raw interleaved stereo float32 at 48 kHz) through
	// the plug-in. This is how Glass76 gets fitted against the CLA-76: the
	// reference curve comes from a render made inside FL Studio, where Waves
	// is properly licensed, and the candidate curve comes from here.
	if (std::string (argv[1]) == "--processraw" && argc >= 7)
	{
		Runner r;
		std::string perr;
		if (!r.open (argv[2], perr))
		{
			std::printf ("could not load: %s\n", perr.c_str ());
			return 1;
		}
		const int inMark = std::atoi (argv[5]);
		const int outMark = std::atoi (argv[6]);
		const int ratioStep = (argc >= 8) ? std::atoi (argv[7]) : 3;
		const std::vector<std::pair<ParamID, ParamValue>> params {
			{kParamInputId, inMark / 8.0},
			{kParamOutputId, outMark / 8.0},
			{kParamRatioId, step (ratioStep, kRatioStepCount)},
			{kParamAttackId, kAttackDefaultNormalized},   // position 3
			{kParamReleaseId, kReleaseDefaultNormalized},  // position 5
			{kParamCompOffId, 0.0},
			{kParamAutoMakeupId, 0.0},
			{kParamAnalogId, step (kAnalogOff, kAnalogStepCount)},
			{kParamMixId, 1.0},
			{kParamTrimId, trimDbToNormalized (0.0)},
		};
		return r.processRawFile (argv[3], argv[4], params) ? 0 : 1;
	}

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
		// Calibrated against the CLA-76, not chosen: marks 4/4 give
		// kInputMakeupDb + kOutputMakeupDb + 2 * kGainStepsDb[4] of static gain.
		const double expected =
		    kInputMakeupDb + kOutputMakeupDb + 2.0 * kGainStepsDb[kInputDefaultStep];
		check (std::fabs (gainDb - expected) < 0.5,
		       "static gain at marks 4/4 matches the calibration",
		       f2 (gainDb) + " dB, expected " + f2 (expected));
		check (r.meterGrDb < 0.5, "no gain reduction below threshold",
		       f2 (r.meterGrDb) + " dB");
	}

	//--- 2. Compression above the threshold ---------------------------
	std::printf ("\nCompression\n");
	{
		// At input mark 4 the net drive into the detector is
		// kGainStepsDb[4] + kInputMakeupDb. A -24 dBFS signal therefore lands
		// (-24 + kGainStepsDb[4] + kInputMakeupDb - kThresholdDb) dB over the
		// threshold, and 4:1 reduces that by (1 - 1/4).
		const double amp = dbToLinear (-24.0);
		const double over =
		    -24.0 + kGainStepsDb[kInputDefaultStep] + kInputMakeupDb - kThresholdDb;
		const double expectedGr = over * 0.75;
		Result r;
		if (!measure (amp, 220.0, 1.5, baseline, r))
			return 1;
		check (std::fabs (r.meterGrDb - expectedGr) < 1.5,
		       "4:1 reduction matches the gain computer",
		       f2 (r.meterGrDb) + " dB, expected " + f2 (expectedGr));

		const double staticGain = kInputMakeupDb + kOutputMakeupDb + 2.0 * kGainStepsDb[kInputDefaultStep];
		const double outDb = linearToDb (r.outPeak);
		check (outDb < -24.0 + staticGain - 1.0, "output is pulled down accordingly",
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
		// Compare the two against each other, not against the input: that
		// tests Trim itself rather than the gain staging around it.
		const double spread = linearToDb (plus.outPeak) - linearToDb (minus.outPeak);
		check (std::fabs (spread - 12.0) < 0.4, "Trim spans 12 dB from -6 to +6",
		       f2 (spread) + " dB");
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
		// Mark 4 to mark 8 (0 dB) is kGainStepsDb[8] - kGainStepsDb[4] dB of
		// attenuator travel, whatever the fixed make-up behind it happens to be.
		const double amp = dbToLinear (-40.0);
		Result low, high;
		if (!measure (amp, 220.0, 0.5, baseline, low))
			return 1;
		if (!measure (amp, 220.0, 0.5, withParam (kParamOutputId, step (8, kGainStepCount)), high))
			return 1;
		const double lift = linearToDb (high.outPeak) - linearToDb (low.outPeak);
		const double expectedLift = kGainStepsDb[8] - kGainStepsDb[kOutputDefaultStep];
		check (std::fabs (lift - expectedLift) < 0.6, "Output mark 4 -> mark 8 travel matches the table",
		       f2 (lift) + " dB, expected " + f2 (expectedLift));
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
		// IN and OUT are a VU meter now: RMS, with 0 VU = -18 dBFS. A sine
		// peaking at -24 dBFS has an RMS of -27.01 dBFS, so the needle should
		// sit at -9.0 VU. Chosen to stay clear of the +3 VU top of the scale.
		const double amp = dbToLinear (-24.0);
		const double expectedVu = dbFsToVu (-24.0 - 3.01);
		Result r;
		if (!measure (amp, 220.0, 2.5, baseline, r))
			return 1;
		check (std::fabs (r.meterInDb - expectedVu) < 1.0,
		       "IN meter reads the input in VU",
		       f2 (r.meterInDb) + " VU, expected " + f2 (expectedVu));
		const double staticGain = kInputMakeupDb + kOutputMakeupDb + 2.0 * kGainStepsDb[kInputDefaultStep];
		check (r.meterOutDb < r.meterInDb + staticGain + 0.5,
		       "OUT meter shows reduction relative to the static gain",
		       f2 (r.meterOutDb) + " VU vs IN " + f2 (r.meterInDb) + " + " + f2 (staticGain));
	}

	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
