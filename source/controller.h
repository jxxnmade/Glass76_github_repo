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
	void setAppearance (int appearance) { mAppearance = appearance ? 1 : 0; }

	/** Absolute path to a user-chosen background image, or empty for none.
	    Same story as appearance: a UI preference in the controller's own
	    state, not a parameter. The view does the actual loading; this is
	    just the persisted string. */
	const std::string& getBackgroundImagePath () const { return mBackgroundImagePath; }
	void setBackgroundImagePath (const std::string& path) { mBackgroundImagePath = path; }

	//--- Interface ------------------------------------------------------
	DEFINE_INTERFACES
	END_DEFINE_INTERFACES (EditController)
	DELEGATE_REFCOUNT (EditController)

private:
	RootView* mRoot {nullptr};
	int mAppearance {1};   // dark by default
	std::string mBackgroundImagePath;
};

//------------------------------------------------------------------------
} // namespace Jaxson
