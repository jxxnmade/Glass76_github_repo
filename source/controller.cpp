//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#include "controller.h"
#include "cids.h"
#include "params.h"
#include "ui/editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Jaxson {

namespace {

/** Build a stepped parameter from a table of display strings. */
StringListParameter* addStringList (ParameterContainer& parameters, const TChar* title,
                                    ParamID tag, const TChar* units,
                                    const TChar* const* strings, int count, int defaultIndex)
{
	auto* p = new StringListParameter (title, tag, units,
	                                   ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
	for (int i = 0; i < count; i++)
		p->appendString (strings[i]);
	parameters.addParameter (p);
	p->setNormalized (stepToNormalized (defaultIndex, count));
	return p;
}

//------------------------------------------------------------------------
// The input and output attenuators. Continuous, like the hardware pot and
// like the Waves CLA-76, but printed with the nine marks from the faceplate
// so the readout matches what the panel says.
//------------------------------------------------------------------------
class AttenuatorParameter : public Parameter
{
public:
	AttenuatorParameter (const TChar* title, ParamID tag, double defaultNormalized)
	{
		UString (info.title, str16BufferSize (String128)).assign (title);
		UString (info.units, str16BufferSize (String128)).assign (STR16 ("dB"));
		info.id = tag;
		info.stepCount = 0;   // continuous
		info.defaultNormalizedValue = defaultNormalized;
		info.flags = ParameterInfo::kCanAutomate;
		setNormalized (defaultNormalized);
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		const double db = normalizedToAttenuatorDb (normalized);
		char text[32];
		if (db <= -600.0)
			std::snprintf (text, sizeof (text), "-Inf");
		else
			std::snprintf (text, sizeof (text), "%.1f", db);
		UString (string, str16BufferSize (String128)).assign (text);
	}

	bool fromString (const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE
	{
		// Coarse inverse: walk the marks and interpolate within the segment
		// the typed value falls in. Only used when a host offers text entry.
		char text[64] {};
		UString (const_cast<TChar*> (string), 64).toAscii (text, sizeof (text));

		if (std::strstr (text, "nf") || std::strstr (text, "NF"))   // -Inf
		{
			normalized = 0.0;
			return true;
		}
		double db = 0.0;
		if (std::sscanf (text, "%lf", &db) != 1)
			return false;
		db = std::clamp (db, -72.0, 0.0);
		for (int i = 0; i < kGainStepCount - 1; i++)
		{
			double lo = kGainStepsDb[i];
			const double hi = kGainStepsDb[i + 1];
			if (lo <= -600.0)
				lo = -72.0;
			if (db >= lo && db <= hi)
			{
				const double frac = (hi > lo) ? (db - lo) / (hi - lo) : 0.0;
				normalized = (i + frac) / (kGainStepCount - 1);
				return true;
			}
		}
		normalized = 1.0;
		return true;
	}
};

} // anonymous namespace

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::initialize (FUnknown* context)
{
	tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	//--- Input / Output attenuators -------------------------------------
	// Continuous. The nine faceplate marks land on even eighths of the
	// travel, so anything a previous stepped build saved still reads back as
	// the same dB.
	parameters.addParameter (new AttenuatorParameter (
	    STR16 ("Input"), kParamInputId,
	    stepToNormalized (kInputDefaultStep, kGainStepCount)));
	parameters.addParameter (new AttenuatorParameter (
	    STR16 ("Output"), kParamOutputId,
	    stepToNormalized (kOutputDefaultStep, kGainStepCount)));

	//--- Auto make-up ---------------------------------------------------
	parameters.addParameter (STR16 ("Auto Makeup"), nullptr, 1, 0.,
	                         ParameterInfo::kCanAutomate, kParamAutoMakeupId);

	//--- Attack / Release, printed as the panel positions ---------------
	static const TChar* const attackStrings[kTimeStepCount] = {
		STR16 ("1 (800 us)"), STR16 ("3 (234 us)"), STR16 ("5 (68 us)"), STR16 ("7 (20 us)")
	};
	static const TChar* const releaseStrings[kTimeStepCount] = {
		STR16 ("1 (1100 ms)"), STR16 ("3 (392 ms)"), STR16 ("5 (140 ms)"), STR16 ("7 (50 ms)")
	};
	addStringList (parameters, STR16 ("Attack"), kParamAttackId, nullptr,
	               attackStrings, kTimeStepCount, kAttackDefaultStep);
	addStringList (parameters, STR16 ("Release"), kParamReleaseId, nullptr,
	               releaseStrings, kTimeStepCount, kReleaseDefaultStep);

	//--- Ratio ----------------------------------------------------------
	static const TChar* const ratioStrings[kRatioStepCount] = {
		STR16 ("20:1"), STR16 ("12:1"), STR16 ("8:1"), STR16 ("4:1"), STR16 ("All")
	};
	addStringList (parameters, STR16 ("Ratio"), kParamRatioId, nullptr,
	               ratioStrings, kRatioStepCount, kRatioDefaultStep);

	//--- Meter source ---------------------------------------------------
	static const TChar* const meterStrings[kMeterStepCount] = {
		STR16 ("GR"), STR16 ("IN"), STR16 ("OUT")
	};
	addStringList (parameters, STR16 ("Meter"), kParamMeterId, nullptr,
	               meterStrings, kMeterStepCount, kMeterDefaultStep);

	//--- Comp Off -------------------------------------------------------
	parameters.addParameter (STR16 ("Comp Off"), nullptr, 1, 0.,
	                         ParameterInfo::kCanAutomate, kParamCompOffId);

	//--- Analog ---------------------------------------------------------
	static const TChar* const analogStrings[kAnalogStepCount] = {
		STR16 ("50 Hz"), STR16 ("60 Hz"), STR16 ("Off")
	};
	addStringList (parameters, STR16 ("Analog"), kParamAnalogId, nullptr,
	               analogStrings, kAnalogStepCount, kAnalogDefaultStep);

	//--- Mix ------------------------------------------------------------
	auto* mix = new RangeParameter (STR16 ("Mix"), kParamMixId, STR16 ("%"),
	                                kMixMinPercent, kMixMaxPercent, kMixDefaultPercent,
	                                0, ParameterInfo::kCanAutomate);
	mix->setPrecision (0);
	parameters.addParameter (mix);

	//--- Trim -----------------------------------------------------------
	auto* trim = new RangeParameter (STR16 ("Trim"), kParamTrimId, STR16 ("dB"),
	                                 kTrimMinDb, kTrimMaxDb, kTrimDefaultDb,
	                                 0, ParameterInfo::kCanAutomate);
	trim->setPrecision (1);
	parameters.addParameter (trim);

	//--- Model: Glass76 CLEAN vs Glass76 Signature -----------------------
	// The glass slider at the top-left of the panel. CLEAN is transparent
	// math with no hardware modeling; Signature is the CLA-76-calibrated
	// character build. See params.h for exactly what each does.
	static const TChar* const modelStrings[kModelStepCount] = {
		STR16 ("Glass76 CLEAN"), STR16 ("Glass76 Signature")
	};
	addStringList (parameters, STR16 ("Model"), kParamModelId, nullptr,
	               modelStrings, kModelStepCount, kModelDefaultStep);

	//--- Meter feedback, written by the processor ------------------------
	// Read-only so no host offers them for automation, but still real
	// parameters, which is what lets the processor push values to the UI
	// without a message allocation on the audio thread.
	auto addMeter = [this] (const TChar* title, ParamID tag, const TChar* units,
	                        double min, double max) {
		auto* p = new RangeParameter (title, tag, units, min, max, min,
		                              0, ParameterInfo::kIsReadOnly);
		p->setPrecision (1);
		parameters.addParameter (p);
	};
	addMeter (STR16 ("Meter GR"), kParamMeterGrId, STR16 ("dB"), 0.0, kMeterGrMaxDb);
	addMeter (STR16 ("Meter In"), kParamMeterInId, STR16 ("VU"),
	          kMeterLevelMinDb, kMeterLevelMaxDb);
	addMeter (STR16 ("Meter Out"), kParamMeterOutId, STR16 ("VU"),
	          kMeterLevelMinDb, kMeterLevelMaxDb);
	addMeter (STR16 ("Auto Makeup Gain"), kParamMeterMakeupId, STR16 ("dB"),
	          0.0, kMeterGrMaxDb);

	//--- Bypass ---------------------------------------------------------
	// kIsBypass is what lets the host do a delay-compensated bypass. Exactly
	// one parameter may carry this flag, and it must be a 1-step one.
	parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0.,
	                         ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
	                         kParamBypassId);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::terminate ()
{
	mRoot = nullptr;
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
// Mirror of Glass76Processor::getState. Read exactly what the processor
// wrote, in the same order. If these two drift apart, presets and saved
// projects silently load the wrong values.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::setComponentState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;
	if (version > kGlass76StateVersion)
		return kResultFalse;

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
	// v2: model, written right after trim. Version-1 streams stop here and
	// default to Signature, which is what they already sounded like.
	if (version >= 2)
	{
		if (!streamer.readDouble (modelNorm)) return kResultFalse;
	}
	if (!streamer.readBool (autoMakeup))    return kResultFalse;
	if (!streamer.readBool (compOff))       return kResultFalse;
	if (!streamer.readBool (bypass))        return kResultFalse;

	setParamNormalized (kParamInputId, inputNorm);
	setParamNormalized (kParamOutputId, outputNorm);
	setParamNormalized (kParamAttackId, attackNorm);
	setParamNormalized (kParamReleaseId, releaseNorm);
	setParamNormalized (kParamRatioId, ratioNorm);
	setParamNormalized (kParamMeterId, meterNorm);
	setParamNormalized (kParamAnalogId, analogNorm);
	setParamNormalized (kParamMixId, mixNorm);
	setParamNormalized (kParamTrimId, trimNorm);
	setParamNormalized (kParamModelId, modelNorm);
	setParamNormalized (kParamAutoMakeupId, autoMakeup ? 1.0 : 0.0);
	setParamNormalized (kParamCompOffId, compOff ? 1.0 : 0.0);
	setParamNormalized (kParamBypassId, bypass ? 1.0 : 0.0);

	return kResultOk;
}

//------------------------------------------------------------------------
// Everything the host knows about a parameter arrives here: automation,
// preset recall, and the processor's own meter output. Push it straight
// into the view so the UI never has to poll.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::setParamNormalized (ParamID tag, ParamValue value)
{
	tresult result = EditControllerEx1::setParamNormalized (tag, value);
	if (result != kResultOk || !mRoot)
		return result;

	switch (tag)
	{
		case kParamInputId:
		case kParamOutputId:
			mRoot->setContinuous (tag, value);
			break;
		case kParamAttackId:
		case kParamReleaseId:
			mRoot->setStep (tag, normalizedToStep (value, kTimeStepCount));
			break;
		case kParamRatioId:
			mRoot->setStep (tag, normalizedToStep (value, kRatioStepCount));
			break;
		case kParamMeterId:
			mRoot->setStep (tag, normalizedToStep (value, kMeterStepCount));
			break;
		case kParamAnalogId:
			mRoot->setStep (tag, normalizedToStep (value, kAnalogStepCount));
			break;
		case kParamModelId:
			mRoot->setStep (tag, normalizedToStep (value, kModelStepCount));
			break;

		case kParamAutoMakeupId:
		case kParamCompOffId:
		case kParamBypassId:
			mRoot->setToggle (tag, value >= 0.5);
			break;

		case kParamMixId:
		case kParamTrimId:
			mRoot->setContinuous (tag, value);
			break;

		case kParamMeterGrId:
			mRoot->setMeterGr (normalizedToGrDb (value));
			break;
		case kParamMeterInId:
			mRoot->setMeterIn (normalizedToLevelDb (value));
			break;
		case kParamMeterOutId:
			mRoot->setMeterOut (normalizedToLevelDb (value));
			break;
		case kParamMeterMakeupId:
			mRoot->setMeterMakeup (normalizedToGrDb (value));
			break;

		default:
			break;
	}

	return result;
}

//------------------------------------------------------------------------
// Outgoing edits from the view. Each one is a complete gesture, so the
// begin/perform/end trio is sent together -- that is what lets a host
// record it as a single automation event.
//------------------------------------------------------------------------
void Glass76Controller::changeStep (ParamID id, int step)
{
	int count = 2;
	switch (id)
	{
		case kParamAttackId:
		case kParamReleaseId: count = kTimeStepCount;   break;
		case kParamRatioId:   count = kRatioStepCount;  break;
		case kParamMeterId:   count = kMeterStepCount;  break;
		case kParamAnalogId:  count = kAnalogStepCount; break;
		case kParamModelId:   count = kModelStepCount;  break;
		default: return;
	}
	changeContinuous (id, stepToNormalized (step, count));
}

//------------------------------------------------------------------------
void Glass76Controller::changePill (ParamID id, bool on)
{
	changeContinuous (id, on ? 1.0 : 0.0);
}

//------------------------------------------------------------------------
void Glass76Controller::changeContinuous (ParamID id, double normalized)
{
	beginEdit (id);
	setParamNormalized (id, normalized);
	performEdit (id, normalized);
	endEdit (id);
}

//------------------------------------------------------------------------
// Controller-only state: the light/dark preference, the background image
// path, and the UI refresh rate. Versioned separately from the processor
// state so the two can evolve independently. The image path and refresh
// rate were both added after ship; a stream that ends early (an older save)
// just leaves the rest at their defaults.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::setState (IBStream* state)
{
	if (!state)
		return kResultTrue;

	IBStreamer streamer (state, kLittleEndian);
	int32 appearance = 0;
	if (streamer.readInt32 (appearance))
	{
		mAppearance = appearance ? 1 : 0;
		if (mRoot)
			mRoot->setAppearance (mAppearance);
	}

	int32 pathLen = 0;
	if (streamer.readInt32 (pathLen) && pathLen >= 0 && pathLen < 4096)
	{
		std::string path (static_cast<size_t> (pathLen), '\0');
		if (pathLen == 0 || streamer.readRaw (path.data (), pathLen) == pathLen)
		{
			mBackgroundImagePath = path;
			if (mRoot)
				mRoot->setBackgroundImagePath (mBackgroundImagePath);
		}
	}

	// Added after ship, same as the background image path -- a stream that
	// ends here (an older save) just keeps the 30 Hz default.
	int32 refreshRateHz = 0;
	if (streamer.readInt32 (refreshRateHz))
	{
		setRefreshRateHz (refreshRateHz);
		if (mRoot)
			mRoot->setRefreshRateHz (mRefreshRateHz);
	}

	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::getState (IBStream* state)
{
	if (!state)
		return kResultTrue;

	IBStreamer streamer (state, kLittleEndian);
	streamer.writeInt32 (mAppearance);
	streamer.writeInt32 (static_cast<int32> (mBackgroundImagePath.size ()));
	if (!mBackgroundImagePath.empty ())
		streamer.writeRaw (mBackgroundImagePath.data (),
		                   static_cast<int32> (mBackgroundImagePath.size ()));
	streamer.writeInt32 (mRefreshRateHz);
	return kResultTrue;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API Glass76Controller::createView (FIDString name)
{
	if (FIDStringsEqual (name, ViewType::kEditor))
		return new VSTGUI::VST3Editor (this, "view", "glass76.uidesc");

	return nullptr;
}

//------------------------------------------------------------------------
// The .uidesc template holds one view with custom-view-name="root". The
// SDK asks us to build it here; everything the editor draws lives inside
// the RootView we return.
//------------------------------------------------------------------------
VSTGUI::CView* Glass76Controller::createCustomView (VSTGUI::UTF8StringPtr name,
                                                    const VSTGUI::UIAttributes& /*attributes*/,
                                                    const VSTGUI::IUIDescription* /*description*/,
                                                    VSTGUI::VST3Editor* /*editor*/)
{
	if (name && std::strcmp (name, "root") == 0)
	{
		auto* root = new RootView (this, VSTGUI::CRect (0, 0, RootView::kPanelWidth,
		                                               RootView::kPanelHeight));
		mRoot = root;
		root->setAppearance (mAppearance);
		if (!mBackgroundImagePath.empty ())
			root->setBackgroundImagePath (mBackgroundImagePath);
		root->setRefreshRateHz (mRefreshRateHz);

		// Seed the view with the values the host already has, so it opens
		// showing the real state rather than the defaults.
		for (int32 i = 0; i < parameters.getParameterCount (); i++)
		{
			if (auto* p = parameters.getParameterByIndex (i))
				setParamNormalized (p->getInfo ().id, p->getNormalized ());
		}
		return root;
	}
	return nullptr;
}

//------------------------------------------------------------------------
} // namespace Jaxson
