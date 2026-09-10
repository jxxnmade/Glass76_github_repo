//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#include "controller.h"
#include "cids.h"
#include "params.h"
#include "prefs.h"
#include "ui/editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

//------------------------------------------------------------------------
// Attack and release. Continuous now -- position 1..7 printed as on the
// faceplate, normalized 0..1 mapping onto it linearly (see
// attackNormalizedToSeconds/releaseNormalizedToSeconds in params.h, which
// this and the UI both go through so the two can never disagree on what a
// given position actually resolves to).
//------------------------------------------------------------------------
class TimeConstantParameter : public Parameter
{
public:
	TimeConstantParameter (const TChar* title, ParamID tag, bool isAttack, double defaultNormalized)
	: mIsAttack (isAttack)
	{
		UString (info.title, str16BufferSize (String128)).assign (title);
		info.id = tag;
		info.stepCount = 0;   // continuous
		info.defaultNormalizedValue = defaultNormalized;
		info.flags = ParameterInfo::kCanAutomate;
		setNormalized (defaultNormalized);
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		// 2 decimals on the position -- "4.29", not "4.3" -- matching the
		// precision the real CLA-76 plug-in reads its own knobs at, and the
		// same string RootView::attackText()/releaseText() show on-screen
		// (see editor.cpp) so the automation lane and the UI never disagree.
		const double position = 1.0 + std::clamp (normalized, 0.0, 1.0) * 6.0;
		char text[32];
		if (mIsAttack)
			std::snprintf (text, sizeof (text), "%.2f (%.0f us)", position,
			               attackNormalizedToSeconds (normalized) * 1e6);
		else
			std::snprintf (text, sizeof (text), "%.2f (%.0f ms)", position,
			               releaseNormalizedToSeconds (normalized) * 1e3);
		UString (string, str16BufferSize (String128)).assign (text);
	}

	bool fromString (const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE
	{
		// Only the leading position number is parsed -- "(234 us)" and
		// similar is toString()'s own output echoed back by a host, not
		// something a user is expected to type.
		char text[64] {};
		UString (const_cast<TChar*> (string), 64).toAscii (text, sizeof (text));
		double position = 0.0;
		if (std::sscanf (text, "%lf", &position) != 1)
			return false;
		position = std::clamp (position, 1.0, 7.0);
		normalized = (position - 1.0) / 6.0;
		return true;
	}

private:
	bool mIsAttack;
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

	//--- Attack / Release, continuous, printed as the panel positions ----
	parameters.addParameter (new TimeConstantParameter (
	    STR16 ("Attack"), kParamAttackId, true, kAttackDefaultNormalized));
	parameters.addParameter (new TimeConstantParameter (
	    STR16 ("Release"), kParamReleaseId, false, kReleaseDefaultNormalized));

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
	flushPrefsNow ();
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
		case kParamAttackId:
		case kParamReleaseId:
			mRoot->setContinuous (tag, value);
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
// path, the UI refresh rate, and (added here) the selected skin. Versioned
// separately from the processor state so the two can evolve independently.
// Every field after appearance was added after ship; a stream that ends
// early (an older save) just leaves the rest at their defaults -- each read
// stays behind its own `if (streamer.readXxx(...))`, so nothing new here
// breaks a project saved by an older build, and an older build still opens
// a project saved by this one.
//
// Precedence: Documents\Glass76\preferences.json, when it can be read, is
// what actually takes effect -- these settings are meant to follow the
// user across every project and instance, not reset every time a different
// project is opened. What is read from the stream below is therefore only
// a fallback, applied exclusively when ensurePrefsLoaded() could not read
// the global file this run (e.g. a locked-down or offline machine). See
// prefs.h for the file itself.
//------------------------------------------------------------------------
tresult PLUGIN_API Glass76Controller::setState (IBStream* state)
{
	if (!state)
		return kResultTrue;

	// Some hosts call setState before createView, some after -- make sure
	// the global file has had its one chance to load either way.
	ensurePrefsLoaded ();

	IBStreamer streamer (state, kLittleEndian);

	int32 appearance = 0;
	const bool haveAppearance = streamer.readInt32 (appearance);

	int32 pathLen = 0;
	std::string path;
	bool havePath = false;
	if (streamer.readInt32 (pathLen) && pathLen >= 0 && pathLen < 4096)
	{
		path.assign (static_cast<size_t> (pathLen), '\0');
		if (pathLen == 0 || streamer.readRaw (path.data (), pathLen) == pathLen)
			havePath = true;
	}

	int32 refreshRateHz = 0;
	const bool haveRefresh = streamer.readInt32 (refreshRateHz);

	// Appended for the skin feature. Older streams simply end before this
	// point, so haveSkin is false and the built-in Hardware default holds.
	int32 skinId = 0;
	const bool haveSkin = streamer.readInt32 (skinId);

	// Appended for the window-scale feature (stage 3.5). Same story: an
	// older stream ends before this point and the built-in 100% holds.
	int32 scalePercent = 0;
	const bool haveScale = streamer.readInt32 (scalePercent);

	// Appended for the transparent-background feature. Same story again.
	int32 transparentBg = 0;
	const bool haveTransparentBg = streamer.readInt32 (transparentBg);

	if (!mPrefsLoaded)
	{
		if (haveAppearance)
		{
			mAppearance = appearance ? 1 : 0;
			if (mRoot)
				mRoot->setAppearance (mAppearance);
		}
		if (havePath)
		{
			mBackgroundImagePath = path;
			if (mRoot)
				mRoot->setBackgroundImagePath (mBackgroundImagePath);
		}
		if (haveRefresh)
		{
			mRefreshRateHz = (refreshRateHz == 60 || refreshRateHz == 120) ? refreshRateHz : 30;
			if (mRoot)
				mRoot->setRefreshRateHz (mRefreshRateHz);
		}
		if (haveSkin)
			mSkin = skinId ? 1 : 0;
		if (haveScale)
		{
			mScalePercent = snapScalePercent (scalePercent);
			if (mRoot)
				mRoot->setScalePercent (mScalePercent);
		}
		if (haveTransparentBg)
		{
			mTransparentBackground = transparentBg != 0;
			if (mRoot)
				mRoot->setTransparentBackground (mTransparentBackground);
		}
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
	streamer.writeInt32 (mSkin);
	streamer.writeInt32 (mScalePercent);
	streamer.writeInt32 (mTransparentBackground ? 1 : 0);
	return kResultTrue;
}

//------------------------------------------------------------------------
void Glass76Controller::ensurePrefsLoaded ()
{
	if (mPrefsLoaded)
		return;

	Glass76Prefs p;
	if (!prefs::load (p))
		return;   // stays false; the next caller (there are only ever one
		          // or two) gets another chance rather than giving up for
		          // the life of the instance

	mPrefsLoaded = true;
	mAppearance = (p.appearance == "light") ? 0 : 1;
	mBackgroundImagePath = p.backgroundImage;
	mRefreshRateHz = (p.refreshRateHz == 60 || p.refreshRateHz == 120) ? p.refreshRateHz : 30;
	mSkin = (p.skin == "glass") ? 1 : 0;
	mScalePercent = snapScalePercent (p.scalePercent);
	mTransparentBackground = p.transparentBackground;

	if (mRoot)
	{
		mRoot->setAppearance (mAppearance);
		mRoot->setBackgroundImagePath (mBackgroundImagePath);
		mRoot->setRefreshRateHz (mRefreshRateHz);
		mRoot->setScalePercent (mScalePercent);
		mRoot->setTransparentBackground (mTransparentBackground);
	}
}

//------------------------------------------------------------------------
void Glass76Controller::flushPrefsIfDue ()
{
	if (!mPrefsDirty || !mPrefsWritable)
		return;

	// A tick count rather than a wall clock, so the ~500 ms debounce holds
	// regardless of whether the editor is redrawing at 30, 60 or 120 Hz.
	const int ticksFor500ms = std::max (1, mRefreshRateHz / 2);
	if (++mPrefsDirtyTicks < ticksFor500ms)
		return;

	flushPrefsNow ();
}

//------------------------------------------------------------------------
void Glass76Controller::flushPrefsNow ()
{
	if (!mPrefsDirty || !mPrefsWritable)
		return;

	Glass76Prefs p;
	p.version = 1;
	p.skin = (mSkin == 1) ? "glass" : "hardware";
	p.appearance = mAppearance ? "dark" : "light";
	p.refreshRateHz = mRefreshRateHz;
	p.backgroundImage = mBackgroundImagePath;
	p.scalePercent = mScalePercent;
	p.transparentBackground = mTransparentBackground;

	mPrefsDirty = false;
	mPrefsDirtyTicks = 0;

	if (prefs::save (p))
	{
		// Our own write makes the file current -- no reason to keep treating
		// it as "not yet successfully loaded" for the rest of this instance.
		mPrefsLoaded = true;
	}
	else
	{
		// Documents is unwritable (locked-down machine, read-only mount, a
		// scanner holding the file). Stop retrying every tick; the
		// per-project state stream above still works as a fallback.
		mPrefsWritable = false;
	}
}

//------------------------------------------------------------------------
// Each skin has its own .uidesc template -- see resource/glass76.uidesc --
// and, since stage 5, its own window size too (RootView::designSize).
// mSkin picks which one opens; requestSkinSwitch() is what moves between
// them once an editor is already up.
//------------------------------------------------------------------------
namespace {
const char* templateNameFor (int skin) { return (skin == 1) ? "view_glass" : "view_hardware"; }

// The five window-scale steps the settings overlay offers, as zoom factors
// -- also handed to VST3Editor::setAllowedZoomFactors so the host's native
// "Zoom" context-menu item offers exactly the same set. Kept in this one
// place; snapScalePercent() below and the settings-overlay geometry/paint
// code (editor.cpp, skin_glass.cpp) each have their own copy of the
// percentage values because none of the three has a reasonable way to share
// a single array without a new shared header for five constants -- if a
// sixth step is ever added, all three need the same edit, along with
// mSettingsScaleRect's fixed size.
const std::vector<double>& allowedZoomFactors ()
{
	static const std::vector<double> factors {0.25, 0.5, 1.0, 1.5, 2.0};
	return factors;
}

#if SMTG_OS_MACOS
// Temporary diagnostic: traces what the host actually hands us for content
// scale on macOS before stage 3.5 decides whether VST3Editor's existing
// IPlugViewContentScaleSupport handling (vst3editor.cpp, unconditionally
// compiled in via VST3_CONTENT_SCALE_SUPPORT) needs a Retina-specific
// override here, or whether the window is simply too wide in logical points
// for the display. Remove once that question has an answer recorded in
// CHANGELOG.md -- see the stage 3.5 plan's "macOS content scale" section.
class Glass76DiagnosticEditor : public VSTGUI::VST3Editor
{
public:
	using VST3Editor::VST3Editor;

protected:
	Steinberg::tresult PLUGIN_API setContentScaleFactor (ScaleFactor factor) override
	{
		std::fprintf (stderr, "[Glass76] setContentScaleFactor(%f)\n", static_cast<double> (factor));
		return VST3Editor::setContentScaleFactor (factor);
	}
};
using PlatformEditor = Glass76DiagnosticEditor;
#else
using PlatformEditor = VSTGUI::VST3Editor;
#endif

} // anonymous namespace

//------------------------------------------------------------------------
int Glass76Controller::snapScalePercent (int pct)
{
	static constexpr int kChoices[5] = {25, 50, 100, 150, 200};
	for (int c : kChoices)
		if (c == pct)
			return c;
	return 100;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API Glass76Controller::createView (FIDString name)
{
	if (FIDStringsEqual (name, ViewType::kEditor))
	{
		// A fresh instance with no saved project state never calls
		// setState() at all, so this is the one call site guaranteed to run
		// before the editor opens.
		ensurePrefsLoaded ();
		auto* editor = new PlatformEditor (this, templateNameFor (mSkin), "glass76.uidesc");
		editor->setAllowedZoomFactors (allowedZoomFactors ());
		// The frame doesn't exist yet -- setZoomFactor just records the
		// value now, and VST3Editor::open() sizes the frame at
		// getAbsScaleFactor() before the window is ever shown, so this opens
		// at the right size with no visible resize.
		editor->setZoomFactor (mScalePercent / 100.0);
		return editor;
	}

	return nullptr;
}

//------------------------------------------------------------------------
// Both templates hold one view with custom-view-name="root" -- see
// resource/glass76.uidesc. The SDK asks us to build it here; everything the
// editor draws lives inside the RootView we return. Which skin (and which
// window size) it opens at follows mSkin, not the template's own name, so
// this and requestSkinSwitch() only ever have to agree with each other.
//------------------------------------------------------------------------
VSTGUI::CView* Glass76Controller::createCustomView (VSTGUI::UTF8StringPtr name,
                                                    const VSTGUI::UIAttributes& /*attributes*/,
                                                    const VSTGUI::IUIDescription* /*description*/,
                                                    VSTGUI::VST3Editor* editor)
{
	if (name && std::strcmp (name, "root") == 0)
	{
		const SkinId skinId = (mSkin == 1) ? SkinId::Glass : SkinId::Hardware;
		const VSTGUI::CPoint size = RootView::designSize (skinId);

		// requestSkinSwitch() already called setEditorSizeConstrains() on
		// mEditor before exchangeView() got here, but that call computed its
		// target against whatever the frame's size happened to be at that
		// exact moment -- before the old skin's content was even torn down.
		// Re-asserting it here, now that the new view actually exists, is
		// what makes the resize land correctly rather than racing: at a
		// large zoom mismatch between two skins' design sizes (e.g. 25%,
		// where Hardware's 1240-wide and Glass's 880-wide canvases differ by
		// 90 physical px) a host can otherwise leave the window at the old
		// skin's size while this skin's full design-space content renders
		// into it, which looks like most of the panel got cropped off
		// rather than the whole thing being consistently tiny. Harmless on
		// a fresh open too -- VST3Editor::open() already sizes correctly by
		// itself; this just repeats the same request against the same
		// editor and target size.
		editor->setEditorSizeConstrains (size, size);

		auto* root = new RootView (this, skinId, VSTGUI::CRect (0, 0, size.x, size.y));
		mRoot = root;
		root->setAppearance (mAppearance);
		if (!mBackgroundImagePath.empty ())
			root->setBackgroundImagePath (mBackgroundImagePath);
		root->setRefreshRateHz (mRefreshRateHz);
		root->setScalePercent (mScalePercent);
		root->setTransparentBackground (mTransparentBackground);

		// Seed the view with the values the host already has, so it opens
		// showing the real state rather than the defaults.
		for (int32 i = 0; i < parameters.getParameterCount (); i++)
		{
			if (auto* p = parameters.getParameterByIndex (i))
				setParamNormalized (p->getInfo ().id, p->getNormalized ());
		}

		if (mReopenSettingsAfterSwitch)
		{
			mReopenSettingsAfterSwitch = false;
			root->openSettings ();
		}
		return root;
	}
	return nullptr;
}

//------------------------------------------------------------------------
void Glass76Controller::didOpen (VSTGUI::VST3Editor* editor)
{
	mEditor = editor;

#if SMTG_OS_MACOS
	// See the Glass76DiagnosticEditor comment in createView(): temporary,
	// pending the stage 3.5 macOS content-scale finding. getRect() is
	// IPlugView's own idea of the view's size in logical points -- what the
	// host asked for / was granted -- independent of whatever
	// setContentScaleFactor() separately reports through the diagnostic
	// subclass's override.
	const Steinberg::ViewRect r = editor->getRect ();
	std::fprintf (stderr, "[Glass76] didOpen: getRect() = %dx%d\n",
	             static_cast<int> (r.getWidth ()), static_cast<int> (r.getHeight ()));
#endif
}

//------------------------------------------------------------------------
void Glass76Controller::willClose (VSTGUI::VST3Editor* editor)
{
	if (mEditor == editor)
		mEditor = nullptr;
}

//------------------------------------------------------------------------
// setZoomFactor is idempotent (VST3Editor::setZoomFactor early-returns if
// the factor is unchanged) and drives the actual resize itself via
// CFrame::setZoom -- there is nothing else to call here, and in particular
// no requestResize: a second resize request racing the one setZoom already
// issued is exactly the failure mode the plan's risk section calls out.
//------------------------------------------------------------------------
void Glass76Controller::requestScalePercent (int pct)
{
	setScalePercent (pct);   // updates mScalePercent and persists
	if (mEditor)
		mEditor->setZoomFactor (mScalePercent / 100.0);
}

//------------------------------------------------------------------------
// Reached from two places: requestScalePercent() above (via the
// setZoomFactor() call it just made) and, independently, VST3Editor's own
// "Zoom" context-submenu path (built off setAllowedZoomFactors() in
// createView()). setZoomFactor() has already taken effect by the time this
// runs either way, so there is nothing left to apply -- only mRoot's own
// idea of the current value (which pill paints selected, next time the
// settings overlay opens) needs to be kept in sync, since a menu-driven
// change never goes through RootView::setScalePercent() the way a
// pill click does.
//------------------------------------------------------------------------
void Glass76Controller::onZoomChanged (VSTGUI::VST3Editor* editor, double newZoom)
{
	if (editor != mEditor)
		return;
	setScalePercent (static_cast<int> (std::lround (newZoom * 100.0)));
	if (mRoot)
		mRoot->setScalePercent (mScalePercent);
}

//------------------------------------------------------------------------
// setEditorSizeConstrains() first: exchangeView() only re-reads minSize and
// maxSize off the new template, not its size attribute, so without this the
// second skin would snap back to whatever the first one's constraints were
// and read as a host bug (see the plan's "Per-skin window size" section).
// requestRecreateView(), which exchangeView() calls, defers itself via
// doAfterEventProcessing() while an event is in flight, so it is fine to
// call this from RootView's own onMouseDown.
//------------------------------------------------------------------------
void Glass76Controller::requestSkinSwitch (int skin)
{
	skin = skin ? 1 : 0;
	if (skin == mSkin)
		return;

	setSkin (skin);

	if (!mEditor)
		return;   // takes effect on the next createView() instead

	// exchangeView destroys and rebuilds RootView, so capture whether the
	// settings panel was open before that happens -- createCustomView()
	// consumes this flag on the other side.
	mReopenSettingsAfterSwitch = mRoot && mRoot->settingsOpen ();

	const VSTGUI::CPoint newSize = RootView::designSize (mSkin == 1 ? SkinId::Glass : SkinId::Hardware);
	mEditor->setEditorSizeConstrains (newSize, newSize);
	mEditor->exchangeView (templateNameFor (mSkin));
}

//------------------------------------------------------------------------
} // namespace Jaxson
