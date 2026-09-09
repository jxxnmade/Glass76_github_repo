//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// Glass76Controller -- the UI / parameter side. Runs on the main thread.
//
// It owns the parameter definitions the host shows and automates. It must
// never touch the processor's members directly; the only links between
// them are the state stream, the parameter IDs, and the three read-only
// meter parameters the processor pushes back through the host.
//
// The controller doubles as a VST3EditorDelegate. createView() hands the
// host a stock VST3Editor built against resource/glass76.uidesc, whose
// single custom view we supply from createCustomView(). That one view --
// RootView -- paints and handles every interaction in the editor.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <string>

namespace Jaxson {

class RootView;

//------------------------------------------------------------------------
class Glass76Controller : public Steinberg::Vst::EditControllerEx1,
                          public VSTGUI::VST3EditorDelegate
{
public:
	Glass76Controller () = default;
	~Glass76Controller () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return (Steinberg::Vst::IEditController*)new Glass76Controller;
	}

	//--- from IPluginBase -----------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	//--- from EditController --------------------------------------------
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** Automation, preset recall and the processor's meter output all
	    arrive here. Everything is forwarded to the view. */
	Steinberg::tresult PLUGIN_API setParamNormalized (
	    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	//--- VST3EditorDelegate ---------------------------------------------
	VSTGUI::CView* createCustomView (VSTGUI::UTF8StringPtr name,
	                                 const VSTGUI::UIAttributes& attributes,
	                                 const VSTGUI::IUIDescription* description,
	                                 VSTGUI::VST3Editor* editor) override;
	void didOpen (VSTGUI::VST3Editor* editor) override;
	void willClose (VSTGUI::VST3Editor* editor) override;

	/** Fires whenever VST3Editor::setZoomFactor takes effect -- both from
	    requestScalePercent() below and from the native "Zoom" context-menu
	    VST3Editor itself builds off setAllowedZoomFactors(). Keeps the
	    persisted value in sync with a zoom picked from that menu rather than
	    through the settings overlay. Never re-drives setZoomFactor itself
	    (that would just re-enter this callback for no reason -- VST3Editor
	    has already applied the zoom by the time this runs). */
	void onZoomChanged (VSTGUI::VST3Editor* editor, double newZoom) override;

	//--- outgoing edits, called from RootView ---------------------------
	void changeStep (Steinberg::Vst::ParamID id, int step);
	void changePill (Steinberg::Vst::ParamID id, bool on);
	void changeContinuous (Steinberg::Vst::ParamID id, double normalized);

	/** The frame owns the view, so it can outlive nothing -- RootView calls
	    this from its destructor to stop us writing into freed memory. */
	void clearRoot (RootView* root)
	{
		if (mRoot == root)
			mRoot = nullptr;
	}

	/** 0 = light, 1 = dark. Purely a UI preference, so it lives in the
	    controller's own state and is never exposed as a parameter. */
	int getAppearance () const { return mAppearance; }
	void setAppearance (int appearance)
	{
		mAppearance = appearance ? 1 : 0;
		markPrefsDirty ();
	}

	/** Absolute path to a user-chosen background image, or empty for none.
	    Same story as appearance: a UI preference in the controller's own
	    state, not a parameter. The view does the actual loading; this is
	    just the persisted string. */
	const std::string& getBackgroundImagePath () const { return mBackgroundImagePath; }
	void setBackgroundImagePath (const std::string& path)
	{
		mBackgroundImagePath = path;
		markPrefsDirty ();
	}

	/** UI redraw rate in Hz: 30, 60 or 120. Same story as appearance -- a UI
	    preference in the controller's own state, not a parameter. */
	int getRefreshRateHz () const { return mRefreshRateHz; }
	void setRefreshRateHz (int hz)
	{
		mRefreshRateHz = (hz == 60 || hz == 120) ? hz : 30;
		markPrefsDirty ();
	}

	/** Skips every opaque backdrop and card fill so the host's own window
	    shows through everywhere but the glass edge stack, text and controls
	    -- same story as appearance: a UI preference, not a parameter. */
	bool getTransparentBackground () const { return mTransparentBackground; }
	void setTransparentBackground (bool on)
	{
		mTransparentBackground = on;
		markPrefsDirty ();
	}

	/** User zoom, as a percentage of the design canvas: one of 25, 50, 100,
	    150, 200 (anything else snaps to 100). A UI preference like the ones
	    above, but with a side effect the others don't have: when an editor is
	    already open, this also drives its actual window size via
	    VST3Editor::setZoomFactor -- see requestScalePercent() below, which is
	    what the settings-overlay pill and the native Zoom context-menu path
	    (onZoomChanged) both call. getScalePercent()/setScalePercent() alone
	    only touch the persisted value, matching every other preference here;
	    nothing calls setScalePercent() directly except ensurePrefsLoaded(),
	    setState()'s fallback branch, and requestScalePercent() itself. */
	int getScalePercent () const { return mScalePercent; }
	void setScalePercent (int pct)
	{
		mScalePercent = snapScalePercent (pct);
		markPrefsDirty ();
	}

	/** The interactive version of setScalePercent(): also resizes the
	    currently open editor, live, via VST3Editor::setZoomFactor -- what the
	    settings-overlay's zoom pills actually call. A no-op if `pct` (after
	    snapping) already matches. Safe to call with no editor open; the new
	    value simply takes effect the next time one opens. */
	void requestScalePercent (int pct);

	/** 0 = Hardware, 1 = Glass. Same story again -- a UI preference, not a
	    parameter. Each skin opens its own real faceplate and window size
	    now (RootView::designSize) -- stage 5. Silent: updates
	    the persisted value and, if an editor already has this skin's own
	    template open, nothing else -- next open (or requestSkinSwitch)
	    picks it up. */
	int getSkin () const { return mSkin; }
	void setSkin (int skin)
	{
		mSkin = skin ? 1 : 0;
		markPrefsDirty ();
	}

	/** The interactive version of setSkin(): also resizes and rebuilds the
	    currently open editor, live, against the other skin's .uidesc
	    template -- what the settings-overlay skin toggle actually calls. A
	    no-op if `skin` already matches, or (like setSkin()) if no editor is
	    open yet, in which case the new value simply takes effect the next
	    time one opens. */
	void requestSkinSwitch (int skin);

	/** Loads Documents\Glass76\preferences.json once per controller
	    instance, applying it over whatever setState() already read from the
	    project (see setState()'s own comment for the precedence rule).
	    Called from createView() and, defensively, from setState() -- hosts
	    do not all call these in the same order. Safe to call more than
	    once: it is a no-op once a load has succeeded, and retries on a
	    later call if Documents could not be read yet. */
	void ensurePrefsLoaded ();

	/** Marks the effective UI settings as changed since the last write to
	    preferences.json. The actual write happens debounced, off
	    RootView's existing animation timer (flushPrefsIfDue()), never
	    synchronously with the click that caused it. */
	void markPrefsDirty () { mPrefsDirty = true; }

	/** Called every tick from RootView::onTimer. Writes preferences.json at
	    most once per ~500 ms of real dirtiness (expressed in ticks, so it
	    self-scales with the 30/60/120 Hz refresh rate). */
	void flushPrefsIfDue ();

	/** Unconditional write bypassing the debounce, so a change made just
	    before the editor closes is never lost. Called from RootView's
	    destructor path and from terminate(). */
	void flushPrefsNow ();

	//--- Interface ------------------------------------------------------
	DEFINE_INTERFACES
	END_DEFINE_INTERFACES (EditController)
	DELEGATE_REFCOUNT (EditController)

private:
	/** Snaps to the nearest of {25, 50, 100, 150, 200}; anything else (an
	    old/foreign preferences.json, a state stream from a build with a
	    different set) falls back to 100. */
	static int snapScalePercent (int pct);

	RootView* mRoot {nullptr};
	VSTGUI::VST3Editor* mEditor {nullptr};   // tracked via didOpen/willClose, for requestSkinSwitch
	bool mReopenSettingsAfterSwitch {false}; // sticky settings panel across exchangeView
	int mAppearance {1};   // dark by default
	std::string mBackgroundImagePath;
	int mRefreshRateHz {30};
	int mSkin {0};   // 0 = Hardware, 1 = Glass; see setSkin()
	int mScalePercent {100};   // see getScalePercent()/requestScalePercent()
	bool mTransparentBackground {false};

	//--- global preferences file ------------------------------------------
	bool mPrefsLoaded {false};      // true once preferences.json was read successfully
	bool mPrefsDirty {false};       // an effective value changed since the last write
	bool mPrefsWritable {true};     // false once a write has failed; stops retrying
	int mPrefsDirtyTicks {0};       // timer ticks since markPrefsDirty(), for the debounce
};

//------------------------------------------------------------------------
} // namespace Jaxson
