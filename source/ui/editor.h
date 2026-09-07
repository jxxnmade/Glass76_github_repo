//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// RootView -- the entire Glass76 editor surface, in one CView.
//
// There are no CControl subclasses and no .uidesc bindings: the whole
// panel is laid out in the constructor and painted in draw(). That keeps
// every macOS 27 metric in one readable place instead of scattered across
// a dozen view classes, which matters when the point of the exercise is
// getting the metrics right.
//
// The controller substitutes this view for the "root" custom view in
// glass76.uidesc. Incoming automation arrives through the set* methods;
// outgoing edits go back out through Glass76Controller::changeStep /
// changePill / changeContinuous.
//
// Layout is fixed-size. macOS 27 controls do not reflow, and a plug-in
// window that resizes its own contents is not a thing either host or
// user expects.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cbitmap.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <string>
#include <vector>

namespace VSTGUI {
class CDrawContext;
}

namespace Jaxson {

class Glass76Controller;
namespace mac { struct Theme; }

//------------------------------------------------------------------------
class RootView : public VSTGUI::CView
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

	CLASS_METHODS (RootView, VSTGUI::CView)

private:
	//--- widget model ---------------------------------------------------
	// Every widget is a rect plus the parameter it edits. Hit testing walks
	// these lists; drawing walks them in the same order.

	struct Card
	{
		VSTGUI::CRect r;
		std::string title;
	};

	struct StaticText
	{
		VSTGUI::CRect r;
		std::string text;
		VSTGUI::CHoriTxtAlign align;
		int style;   // 0 = section title, 1 = form label, 2 = caption
	};

	struct Segmented
	{
		VSTGUI::CRect r;
		Steinberg::Vst::ParamID id {0};
		std::vector<std::string> labels;
		int step {0};
		bool capsule {true};
	};

	struct Slider
	{
		VSTGUI::CRect r;          // the full row-height hit area
		Steinberg::Vst::ParamID id {0};
		int detents {0};          // tick marks drawn under the track, 0 = none
		bool snap {false};        // stop only on the ticks
		bool bipolar {false};     // fill from the centre instead of the left
		double norm {0.0};
		double defaultNorm {0.0};
	};

	struct Switch
	{
		VSTGUI::CRect r;
		Steinberg::Vst::ParamID id {0};
		bool on {false};
	};

	struct Pill
	{
		VSTGUI::CRect r;
		Steinberg::Vst::ParamID id {0};
		std::string label;
		bool on {false};
	};

	//--- construction ---------------------------------------------------
	void buildLayout ();

	//--- drawing --------------------------------------------------------
	const mac::Theme& theme () const;
	void invalidateChrome ();
	void drawChrome (VSTGUI::CDrawContext* context);
	void ensureChrome (VSTGUI::CDrawContext* context);

	void drawSegmented (VSTGUI::CDrawContext* context, const Segmented& seg) const;
	void drawSlider (VSTGUI::CDrawContext* context, const Slider& sl) const;
	void drawSwitch (VSTGUI::CDrawContext* context, const Switch& sw) const;
	void drawPill (VSTGUI::CDrawContext* context, const Pill& pill) const;
	void drawAppearanceButton (VSTGUI::CDrawContext* context) const;
	void drawSettingsButton (VSTGUI::CDrawContext* context) const;
	void drawSettingsOverlay (VSTGUI::CDrawContext* context) const;
	void drawGauge (VSTGUI::CDrawContext* context) const;
	void drawValueColumn (VSTGUI::CDrawContext* context) const;

	//--- settings panel ---------------------------------------------------
	void openSettings ();
	void closeSettings ();
	void chooseBackgroundImage ();
	void clearBackgroundImage ();

	//--- value formatting ------------------------------------------------
	std::string gainStepText (Steinberg::Vst::ParamID id) const;
	std::string attackText () const;
	std::string releaseText () const;
	std::string ratioText () const;
	std::string mixText () const;
	std::string trimText () const;

	//--- lookups ---------------------------------------------------------
	Segmented* findSegmented (Steinberg::Vst::ParamID id);
	Slider* findSlider (Steinberg::Vst::ParamID id);
	int stepOf (Steinberg::Vst::ParamID id) const;

	void applySliderFrom (Slider& sl, VSTGUI::CCoord x, bool fine);
	void onTimer ();

	//--- state ------------------------------------------------------------
	Glass76Controller* mController {nullptr};

	std::vector<Card> mCards;
	std::vector<StaticText> mTexts;
	std::vector<Segmented> mSegments;
	std::vector<Slider> mSliders;
	std::vector<Switch> mSwitches;
	std::vector<Pill> mPills;

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
	std::string mBackgroundImagePath;
	VSTGUI::SharedPointer<VSTGUI::CBitmap> mBackgroundImage;
	int mChromeBgToken {0};       // bumped whenever the background image changes
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
