//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// RootView -- the widget host. Owns layout, hit testing, per-frame
// animation, the redraw timer, and the chrome bitmap cache. It does not
// paint a single pixel itself: every draw call is delegated to whichever
// ISkin is active (see skin.h), through the IWidgetHost interface RootView
// implements so a skin can read live state (the meter, formatted value
// text, the settings-overlay geometry) without ever touching RootView
// directly.
//
// There are no CControl subclasses and no .uidesc bindings: the whole
// panel is laid out in the constructor. That keeps every metric in one
// readable place instead of scattered across a dozen view classes.
//
// The controller substitutes this view for the "root" custom view in
// glass76.uidesc. Incoming automation arrives through the set* methods;
// outgoing edits go back out through Glass76Controller::changeStep /
// changePill / changeContinuous.
//
// Layout is fixed-size for now. Per-skin native sizes (and the resize
// negotiation with the host that goes with them) are a later stage; today
// only the Glass skin exists, at the size it always was.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cbitmap.h"

#include "pluginterfaces/vst/vsttypes.h"

#include "skin.h"
#include "widget.h"

#include <string>
#include <vector>

namespace VSTGUI {
class CDrawContext;
}

namespace Jaxson {

class Glass76Controller;

//------------------------------------------------------------------------
class RootView : public VSTGUI::CView, public IWidgetHost
{
public:
	// The .uidesc template must declare exactly this size.
	static constexpr VSTGUI::CCoord kPanelWidth = 880;
	static constexpr VSTGUI::CCoord kPanelHeight = 470;

	RootView (Glass76Controller* owner, const VSTGUI::CRect& size);
	~RootView () override;

	//--- incoming, from the controller ---------------------------------
	void setStep (Steinberg::Vst::ParamID id, int step);
	void setToggle (Steinberg::Vst::ParamID id, bool on);
	void setContinuous (Steinberg::Vst::ParamID id, double normalized);
	void setMeterGr (double db);
	void setMeterIn (double db);
	void setMeterOut (double db);
	void setMeterMakeup (double db);
	void setAppearance (int dark);

	/** Loads (or clears, if path is empty) the settings-panel background
	    image. Safe to call before the view has a frame -- the load just
	    fails silently and is retried the next time this is called. */
	void setBackgroundImagePath (const std::string& path);

	/** UI redraw rate: 30, 60 or 120 Hz. Restarts the animation timer if it
	    is already running. Anything else snaps to 30. */
	void setRefreshRateHz (int hz);

	//--- CView ----------------------------------------------------------
	void draw (VSTGUI::CDrawContext* context) override;
	bool attached (VSTGUI::CView* parent) override;
	bool removed (VSTGUI::CView* parent) override;

	VSTGUI::CMouseEventResult onMouseDown (VSTGUI::CPoint& where,
	                                       const VSTGUI::CButtonState& buttons) override;
	VSTGUI::CMouseEventResult onMouseMoved (VSTGUI::CPoint& where,
	                                        const VSTGUI::CButtonState& buttons) override;
	VSTGUI::CMouseEventResult onMouseUp (VSTGUI::CPoint& where,
	                                     const VSTGUI::CButtonState& buttons) override;
	VSTGUI::CMouseEventResult onMouseCancel () override;

	//--- IWidgetHost: what a skin is allowed to read ---------------------
	VSTGUI::CBitmap* backgroundImage () const override { return mBackgroundImage; }
	const std::string& backgroundImagePath () const override { return mBackgroundImagePath; }
	double meterShown () const override { return mGaugeShown; }
	double meterPeak () const override { return mGaugePeak; }
	double meterGrDb () const override { return mMeterGrDb; }
	double meterInDb () const override { return mMeterInDb; }
	double meterOutDb () const override { return mMeterOutDb; }
	int meterMode () const override;
	bool autoMakeupOn () const override { return mAutoMakeupOn; }
	double autoMakeupDb () const override { return mMakeupDb; }
	const VSTGUI::CRect& gaugeRect () const override { return mGaugeRect; }
	std::string gainStepText (Steinberg::Vst::ParamID id) const override;
	std::string attackText () const override;
	std::string releaseText () const override;
	std::string ratioText () const override;
	std::string mixText () const override;
	std::string trimText () const override;
	const VSTGUI::CRect& valueRect (Steinberg::Vst::ParamID id) const override;
	bool isDisabled (const Widget& widget) const override;
	const VSTGUI::CRect& appearanceRect () const override { return mAppearanceRect; }
	const VSTGUI::CRect& settingsButtonRect () const override { return mSettingsButtonRect; }
	bool settingsOpen () const override { return mSettingsOpen; }
	int refreshRateHz () const override { return mRefreshRateHz; }
	const VSTGUI::CRect& settingsCardRect () const override { return mSettingsCardRect; }
	const VSTGUI::CRect& settingsChooseRect () const override { return mSettingsChooseRect; }
	const VSTGUI::CRect& settingsClearRect () const override { return mSettingsClearRect; }
	const VSTGUI::CRect& settingsCloseRect () const override { return mSettingsCloseRect; }
	const VSTGUI::CRect& settingsRateRect (int index) const override;

	CLASS_METHODS (RootView, VSTGUI::CView)

private:
	//--- construction ---------------------------------------------------
	void buildLayout ();

	//--- drawing --------------------------------------------------------
	// Returned by value: when a background image supplies an accent hue
	// (see mHasImageAccent) the accent family is re-tinted on the fly, so
	// there is no single cached instance to hand back a reference to.
	mac::Theme theme () const;
	void invalidateChrome ();
	void drawChrome (VSTGUI::CDrawContext* context);
	void ensureChrome (VSTGUI::CDrawContext* context);

	//--- settings panel ---------------------------------------------------
	void openSettings ();
	void closeSettings ();
	void chooseBackgroundImage ();
	void clearBackgroundImage ();
	void startTimer ();

	//--- lookups ---------------------------------------------------------
	Widget* findWidget (Steinberg::Vst::ParamID id, WidgetKind kind);
	const Widget* findWidget (Steinberg::Vst::ParamID id, WidgetKind kind) const;
	int stepOf (Steinberg::Vst::ParamID id) const;

	void applySliderFrom (Widget& sl, VSTGUI::CCoord x, bool fine);
	void onTimer ();

	//--- state ------------------------------------------------------------
	Glass76Controller* mController {nullptr};
	const ISkin* mSkin {nullptr};

	// Every widget the layout builds, in construction order. Painting and
	// hit testing each filter this by kind, in their own priority order --
	// see editor.cpp: draw() paints Segmented/Slider/Switch/Pill in that
	// order, onMouseDown hit-tests Pill/Switch/Segmented/Slider in that
	// (different) order, matching the exact behaviour the six separate
	// vectors this replaced always had.
	std::vector<Widget> mWidgets;

	VSTGUI::CRect mToolbar;
	VSTGUI::CRect mTitleRect;
	VSTGUI::CRect mSubtitleRect;
	VSTGUI::CRect mAppearanceRect;
	VSTGUI::CRect mSettingsButtonRect;
	VSTGUI::CRect mGaugeRect;

	//--- settings overlay ---------------------------------------------------
	bool mSettingsOpen {false};
	VSTGUI::CRect mSettingsCardRect;
	VSTGUI::CRect mSettingsChooseRect;
	VSTGUI::CRect mSettingsClearRect;
	VSTGUI::CRect mSettingsCloseRect;
	VSTGUI::CRect mSettingsRateRect[3];   // 30 / 60 / 120 Hz
	int mRefreshRateHz {30};
	std::string mBackgroundImagePath;
	VSTGUI::SharedPointer<VSTGUI::CBitmap> mBackgroundImage;
	int mChromeBgToken {0};       // bumped whenever the background image changes

	// The accent colour (sliders, switches, gauge arc) re-tinted to the
	// background image's dominant, brightness-weighted hue. Recomputed
	// whenever a new image is chosen; falls back to the default beige when
	// there is no image.
	bool mHasImageAccent {false};
	double mAccentHueDeg {0.0};
	int mChromeBgTokenCached {-1};

	// Value readout column, one per row that has one.
	VSTGUI::CRect mValueInput, mValueOutput;
	VSTGUI::CRect mValueAttack, mValueRelease, mValueRatio;
	VSTGUI::CRect mValueMix, mValueTrim;

	int mDark {0};

	//--- meter, written from the controller, animated by the timer -------
	double mMeterGrDb {0.0};
	double mMeterInDb {-60.0};
	double mMeterOutDb {-60.0};
	double mGaugeShown {0.0};    // smoothed 0..1, what is actually drawn
	double mGaugePeak {0.0};
	double mMakeupDb {0.0};      // what auto make-up is currently adding
	bool mAutoMakeupOn {false};

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;
	VSTGUI::SharedPointer<VSTGUI::CBitmap> mChrome;
	double mChromeScale {0.0};
	int mChromeDark {-1};

	//--- interaction ------------------------------------------------------
	enum class Drag { None, Slider, Segment };
	Drag mDrag {Drag::None};
	Steinberg::Vst::ParamID mDragId {0};
};

//------------------------------------------------------------------------
} // namespace Jaxson
