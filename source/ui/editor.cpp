//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// RootView -- see editor.h for the design notes.
//
// Layout geometry here is unchanged from before the skin split: every
// number is still traced to references/macos-27.md in the
// macos-ui-on-windows skill, or derived from it. What moved to
// skin_glass.cpp is *painting* -- how each widget looks -- not *where* it
// is or *when* it reacts to a click, which both stay here.
//------------------------------------------------------------------------

#include "editor.h"

#include "../controller.h"
#include "../params.h"
#include "macdraw.h"
#include "skin_glass.h"
#include "theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <optional>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace Jaxson {

namespace {

//------------------------------------------------------------------------
// Layout. Fixed, on a 4pt grid except the control heights, which come
// from the kit's five size classes.
//------------------------------------------------------------------------
constexpr CCoord kToolbarH = mac::kUnifiedToolbarHeight;  // 52 [kit]
constexpr CCoord kInset = mac::kContentInset;             // 12 in 27, not Aqua's 20
constexpr CCoord kGap = 12;
constexpr CCoord kCardPad = 16;

// Cards, in view-local coordinates.
constexpr CCoord kColLeftX = 12, kColLeftR = 532;
constexpr CCoord kColRightX = 544, kColRightR = 868;

constexpr CCoord kCardGainT = 64, kCardGainB = 250;
constexpr CCoord kCardDynT = 262, kCardDynB = 458;
constexpr CCoord kCardMeterT = 64, kCardMeterB = 298;
constexpr CCoord kCardOutT = 310, kCardOutB = 458;

// Rows in the two-column form. Horizontal insets are constants: they never
// scale with control height, font size or radius.
constexpr CCoord kLabelGap = 8;
constexpr CCoord kValueGap = 8;

// Punctuation. Source stays ASCII; these are the UTF-8 bytes for the
// characters macOS actually uses -- a real minus sign, not a hyphen.
constexpr const char* kMinus = "\xE2\x88\x92";      // U+2212
constexpr const char* kInfinity = "\xE2\x88\x9E";   // U+221E
constexpr const char* kMicro = "\xC2\xB5";          // U+00B5

std::string fmt (const char* format, ...)
{
	char buffer[128];
	va_list args;
	va_start (args, format);
	std::vsnprintf (buffer, sizeof (buffer), format, args);
	va_end (args);
	return std::string (buffer);
}

bool hit (const CRect& r, const CPoint& p)
{
	return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

/** The image's dominant colour, as a brightness-weighted circular mean of
    every sampled pixel's hue: a pixel's vote counts in direct proportion to
    its brightness (channel value 255 counts 255x as much as value 1), and
    hues are averaged as vectors on the colour wheel so a mix of just-below-
    and just-above-0-degrees reds does not cancel out to a bogus cyan.

    A big upload is sampled on a grid rather than pixel-by-pixel -- the user
    explicitly doesn't need full-resolution precision for a single dominant
    colour, and it keeps this off the UI thread's critical path negligible
    even for a multi-megapixel photo. Returns false if the image has no
    bitmap or is fully transparent/black. */
bool computeAccentHueFromImage (VSTGUI::CBitmap* bitmap, double& outHueDeg)
{
	if (!bitmap)
		return false;

	auto access = VSTGUI::owned (VSTGUI::CBitmapPixelAccess::create (bitmap));
	if (!access)
		return false;

	const uint32_t w = access->getBitmapWidth ();
	const uint32_t h = access->getBitmapHeight ();
	if (w == 0 || h == 0)
		return false;

	// Downscale purely for this analysis by sampling on a grid capped at
	// roughly 128 samples per axis -- plenty to find the dominant hue,
	// nowhere near enough to matter for wall-clock time.
	constexpr uint32_t kMaxSamplesPerAxis = 128;
	const uint32_t strideX = std::max<uint32_t> (1, w / kMaxSamplesPerAxis);
	const uint32_t strideY = std::max<uint32_t> (1, h / kMaxSamplesPerAxis);

	double sumX = 0.0, sumY = 0.0, sumWeight = 0.0;
	VSTGUI::CColor c;
	for (uint32_t y = 0; y < h; y += strideY)
	{
		for (uint32_t x = 0; x < w; x += strideX)
		{
			access->setPosition (x, y);
			access->getColor (c);
			if (c.alpha < 8)
				continue;   // fully transparent pixels carry no colour

			// Brightness = the pixel's HSV value channel, i.e. its brightest
			// component -- a fully-on red (255,0,0) weighs 255x a barely-on
			// blue (0,0,1), exactly as specified.
			const double weight = static_cast<double> (std::max ({c.red, c.green, c.blue}));
			if (weight <= 0.0)
				continue;

			double hue, sat, val;
			mac::rgbToHsv (c.red, c.green, c.blue, hue, sat, val);

			const double rad = hue * (3.14159265358979323846 / 180.0);
			sumX += weight * std::cos (rad);
			sumY += weight * std::sin (rad);
			sumWeight += weight;
		}
	}

	if (sumWeight <= 0.0)
		return false;

	double meanHue = std::atan2 (sumY, sumX) * (180.0 / 3.14159265358979323846);
	if (meanHue < 0.0)
		meanHue += 360.0;
	outHueDeg = meanHue;
	return true;
}

} // anonymous namespace

//========================================================================
// Construction
//========================================================================
RootView::RootView (Glass76Controller* owner, SkinId skin, const CRect& size)
: CView (size), mController (owner)
{
	// The paint is still always Glass -- every SkinId resolves to the same
	// instance until the Hardware faceplate exists (see skins::get in
	// skin_glass.cpp) -- but the window this view opens at is real from
	// this stage on, so the id is threaded through regardless.
	mSkin = &skins::get (skin);

	setMouseEnabled (true);
	setTransparency (false);
	buildLayout ();
	updateFit ();

	// Seed the animated "shown" values from the targets buildLayout just set
	// so the first frame is drawn at rest, not gliding in from zero.
	for (auto& w : mWidgets)
	{
		switch (w.kind)
		{
			case WidgetKind::Slider: w.shownNorm = w.norm; break;
			case WidgetKind::Switch:
			case WidgetKind::Pill: w.shownOn = w.on ? 1.0 : 0.0; break;
			case WidgetKind::Segmented: w.shownStep = static_cast<double> (w.step); break;
			default: break;
		}
	}
}

//------------------------------------------------------------------------
RootView::~RootView ()
{
	// removed() normally stops the timer, but it only runs when the view is
	// taken out of a frame. A view destroyed by any other path would leave a
	// 30 Hz timer firing its lambda into freed memory, so stop it here too.
	if (mTimer)
	{
		mTimer->stop ();
		mTimer = nullptr;
	}
	if (mController)
		mController->clearRoot (this);
}

//------------------------------------------------------------------------
void RootView::buildLayout ()
{
	const CCoord X = getViewSize ().left;
	const CCoord Y = getViewSize ().top;
	auto R = [X, Y] (CCoord l, CCoord t, CCoord r, CCoord b) {
		return CRect (X + l, Y + t, X + r, Y + b);
	};

	// Small builders, one per shape, mirroring the aggregate-initializer
	// call sites this replaced field-for-field -- see widget.h for what
	// each field means per kind.
	auto addCard = [this] (const CRect& r, std::string title) {
		Widget w;
		w.kind = WidgetKind::Card;
		w.r = r;
		w.text = std::move (title);
		mWidgets.push_back (std::move (w));
	};
	auto addLabel = [this] (const CRect& r, std::string text, CHoriTxtAlign align, int style) {
		Widget w;
		w.kind = WidgetKind::Label;
		w.r = r;
		w.text = std::move (text);
		w.align = align;
		w.style = style;
		mWidgets.push_back (std::move (w));
	};
	auto addSegmented = [this] (const CRect& r, ParamID id, std::vector<std::string> labels,
	                            int step, bool capsule) {
		Widget w;
		w.kind = WidgetKind::Segmented;
		w.r = r;
		w.id = id;
		w.labels = std::move (labels);
		w.step = step;
		w.capsule = capsule;
		mWidgets.push_back (std::move (w));
	};
	auto addSlider = [this] (const CRect& r, ParamID id, int detents, bool snap, bool bipolar,
	                         double norm, double defaultNorm) {
		Widget w;
		w.kind = WidgetKind::Slider;
		w.r = r;
		w.id = id;
		w.detents = detents;
		w.snap = snap;
		w.bipolar = bipolar;
		w.norm = norm;
		w.defaultNorm = defaultNorm;
		mWidgets.push_back (std::move (w));
	};
	auto addSwitch = [this] (const CRect& r, ParamID id, bool on) {
		Widget w;
		w.kind = WidgetKind::Switch;
		w.r = r;
		w.id = id;
		w.on = on;
		mWidgets.push_back (std::move (w));
	};
	auto addPill = [this] (const CRect& r, ParamID id, std::string text, bool on) {
		Widget w;
		w.kind = WidgetKind::Pill;
		w.r = r;
		w.id = id;
		w.text = std::move (text);
		w.on = on;
		mWidgets.push_back (std::move (w));
	};

	//--- toolbar ------------------------------------------------------
	// Unified toolbar + title: 52 tall, which is the kit's control height
	// plus 16. No traffic lights: the host owns the window chrome, and
	// non-functional ones would be a lie.
	mToolbar = R (0, 0, kPanelWidth, kToolbarH);
	mTitleRect = R (0, 10, kPanelWidth, 27);
	mSubtitleRect = R (0, 27, kPanelWidth, 42);

	// Toolbar trailing edge: 20 from the window edge, XL-class controls.
	mAppearanceRect = R (kPanelWidth - 20 - 28, 12, kPanelWidth - 20, 40);
	addPill (R (kPanelWidth - 20 - 28 - 8 - 100, 12, kPanelWidth - 20 - 28 - 8, 40),
	        kParamCompOffId, "Comp Off", false);
	mSettingsButtonRect = R (kPanelWidth - 20 - 28 - 8 - 100 - 8 - 28, 12,
	                         kPanelWidth - 20 - 28 - 8 - 100 - 8, 40);

	// Toolbar leading edge: the glass model slider. It doubles as the
	// wordmark -- "Glass76 CLEAN" or "Glass76 Signature" -- and as the
	// control that switches the processor between the two. A Segmented
	// like any other, drawn with its own fonts (see GlassSkin::paintSegmented).
	// Narrower than the original 368: at that width each of the two chips
	// read as an oversized button rather than a compact model switch.
	constexpr CCoord kModelSwitchWidth = 300;
	addSegmented (R (kInset, 12, kInset + kModelSwitchWidth, 40), kParamModelId,
	             {"Glass76 CLEAN", "Glass76 Signature"}, kModelDefaultStep, true);

	//--- cards --------------------------------------------------------
	struct CardSpec
	{
		CRect r;
		const char* title;
	};
	const CardSpec cardSpecs[4] = {
	    {R (kColLeftX, kCardGainT, kColLeftR, kCardGainB), "Gain"},
	    {R (kColLeftX, kCardDynT, kColLeftR, kCardDynB), "Dynamics"},
	    {R (kColRightX, kCardMeterT, kColRightR, kCardMeterB), "Meter"},
	    {R (kColRightX, kCardOutT, kColRightR, kCardOutB), "Output"},
	};
	for (const auto& c : cardSpecs)
		addCard (c.r, c.title);
	for (const auto& c : cardSpecs)
	{
		CRect title (c.r);
		title.left += kCardPad;
		title.top += 12;
		title.bottom = title.top + 18;
		title.right = title.left + 200;
		addLabel (title, c.title, kLeftText, 0);
	}

	//--- Gain card ----------------------------------------------------
	// Two columns: right-aligned labels ending 8pt before one shared
	// controls column, then a right-aligned value column. Left-aligning
	// the labels varies the gap per row and destroys the vertical line
	// that holds a Mac form together.
	{
		const CCoord labelR = kColLeftX + 92;
		const CCoord ctrlX = labelR + kLabelGap;
		const CCoord ctrlR = ctrlX + 316;
		const CCoord valueX = ctrlR + kValueGap;
		const CCoord valueR = kColLeftR - kCardPad;

		const CCoord row1 = kCardGainT + 48;
		const CCoord row2 = kCardGainT + 90;
		const CCoord row3 = kCardGainT + 132;

		addLabel (R (kColLeftX + kCardPad, row1, labelR, row1 + 26), "Input", kRightText, 1);
		addSlider (R (ctrlX, row1, ctrlR, row1 + 26), kParamInputId, kGainStepCount, false, false,
		          stepToNormalized (kInputDefaultStep, kGainStepCount),
		          stepToNormalized (kInputDefaultStep, kGainStepCount));
		mValueInput = R (valueX, row1, valueR, row1 + 26);

		addLabel (R (kColLeftX + kCardPad, row2, labelR, row2 + 26), "Output", kRightText, 1);
		addSlider (R (ctrlX, row2, ctrlR, row2 + 26), kParamOutputId, kGainStepCount, false, false,
		          stepToNormalized (kOutputDefaultStep, kGainStepCount),
		          stepToNormalized (kOutputDefaultStep, kGainStepCount));
		mValueOutput = R (valueX, row2, valueR, row2 + 26);

		addLabel (R (kColLeftX + kCardPad, row3, labelR, row3 + 24), "Auto makeup", kRightText, 1);
		addSwitch (R (ctrlX, row3, ctrlX + mac::kSwitchWidth, row3 + mac::kSwitchHeight),
		          kParamAutoMakeupId, false);
	}

	//--- Dynamics card ------------------------------------------------
	{
		const CCoord labelR = kColLeftX + 92;
		const CCoord ctrlX = labelR + kLabelGap;
		const CCoord ctrlR = ctrlX + 316;
		const CCoord valueX = ctrlR + kValueGap;
		const CCoord valueR = kColLeftR - kCardPad;

		const CCoord row1 = kCardDynT + 46;
		const CCoord row2 = kCardDynT + 92;
		const CCoord row3 = kCardDynT + 138;
		const CCoord h = mac::kSizeLg;   // 28: segmented controls are capsules at Lg

		addLabel (R (kColLeftX + kCardPad, row1, labelR, row1 + h), "Attack", kRightText, 1);
		addSegmented (R (ctrlX, row1, ctrlR, row1 + h), kParamAttackId, {"1", "3", "5", "7"},
		             kAttackDefaultStep, true);
		mValueAttack = R (valueX, row1, valueR, row1 + h);

		addLabel (R (kColLeftX + kCardPad, row2, labelR, row2 + h), "Release", kRightText, 1);
		addSegmented (R (ctrlX, row2, ctrlR, row2 + h), kParamReleaseId, {"1", "3", "5", "7"},
		             kReleaseDefaultStep, true);
		mValueRelease = R (valueX, row2, valueR, row2 + h);

		addLabel (R (kColLeftX + kCardPad, row3, labelR, row3 + h), "Ratio", kRightText, 1);
		addSegmented (R (ctrlX, row3, ctrlR, row3 + h), kParamRatioId,
		             {"20:1", "12:1", "8:1", "4:1", "All"}, kRatioDefaultStep, true);
		mValueRatio = R (valueX, row3, valueR, row3 + h);
	}

	//--- Meter card ---------------------------------------------------
	{
		mGaugeRect = R (kColRightX + kCardPad, kCardMeterT + 38, kColRightR - kCardPad,
		                kCardMeterT + 186);
		addSegmented (R (kColRightX + kCardPad, kCardMeterT + 192, kColRightR - kCardPad,
		                kCardMeterT + 220),
		             kParamMeterId, {"GR", "IN", "OUT"}, kMeterDefaultStep, true);
	}

	//--- Output card --------------------------------------------------
	{
		const CCoord labelR = kColRightX + 56;
		const CCoord ctrlX = labelR + kLabelGap;
		const CCoord ctrlR = ctrlX + 172;
		const CCoord valueX = ctrlR + kValueGap;
		const CCoord valueR = kColRightR - kCardPad;

		const CCoord row1 = kCardOutT + 44;
		const CCoord row2 = kCardOutT + 76;
		const CCoord row3 = kCardOutT + 108;

		addLabel (R (kColRightX + kCardPad, row1, labelR, row1 + 24), "Mix", kRightText, 1);
		addSlider (R (ctrlX, row1, ctrlR, row1 + 24), kParamMixId, 0, false, false, 1.0, 1.0);
		mValueMix = R (valueX, row1, valueR, row1 + 24);

		addLabel (R (kColRightX + kCardPad, row2, labelR, row2 + 24), "Trim", kRightText, 1);
		addSlider (R (ctrlX, row2, ctrlR, row2 + 24), kParamTrimId, 0, false, true,
		          trimDbToNormalized (kTrimDefaultDb), trimDbToNormalized (kTrimDefaultDb));
		mValueTrim = R (valueX, row2, valueR, row2 + 24);

		addLabel (R (kColRightX + kCardPad, row3, labelR, row3 + 24), "Analog", kRightText, 1);
		addSegmented (R (ctrlX, row3, ctrlR, row3 + mac::kSizeRg), kParamAnalogId,
		             {"50 Hz", "60 Hz", "Off"}, kAnalogDefaultStep, false);
	}

	//--- settings overlay -----------------------------------------------
	{
		// +85 over the pre-skin card height: one label row plus one full-width
		// button row plus the trailing separator gap, the same rhythm the
		// refresh-rate block above it already uses -- see the skin row below.
		// A placeholder pending stage 6's proper skin list; it earns its
		// keep now by being the thing that actually exercises exchangeView.
		constexpr CCoord cardW = 380, cardH = 364 + 85;
		const CCoord cardL = kPanelWidth * 0.5 - cardW * 0.5;
		const CCoord cardT = kPanelHeight * 0.5 - cardH * 0.5;
		mSettingsCardRect = R (cardL, cardT, cardL + cardW, cardT + cardH);

		mSettingsChooseRect = R (cardL + kCardPad, cardT + 78, cardL + kCardPad + 168, cardT + 78 + 32);
		mSettingsClearRect = R (cardL + kCardPad + 168 + 10, cardT + 78,
		                        cardL + kCardPad + 168 + 10 + 84, cardT + 78 + 32);

		// Refresh rate row: three equal pill buttons below the image path,
		// same 8px gap the rest of the panel uses between grouped controls.
		const CCoord rateTop = cardT + 191;
		const CCoord rateGap = 8;
		const CCoord rateW = (cardW - 2 * kCardPad - 2 * rateGap) / 3.0;
		for (int i = 0; i < 3; i++)
		{
			const CCoord left = cardL + kCardPad + i * (rateW + rateGap);
			mSettingsRateRect[i] = R (left, rateTop, left + rateW, rateTop + 32);
		}

		// Skin toggle: one full-width pill, 8px below the rate row's own
		// label-to-button gap (rateTop + 32 rate buttons + 16+1 separator +
		// 10 label gap + 18 label + 8 button gap).
		const CCoord skinTop = rateTop + 32 + 17 + 10 + 18 + 8;
		mSettingsSkinRect = R (cardL + kCardPad, skinTop, cardL + cardW - kCardPad, skinTop + 32);

		mSettingsCloseRect = R (cardL + cardW - kCardPad - 90, cardT + cardH - 16 - 32,
		                        cardL + cardW - kCardPad, cardT + cardH - 16);
	}
}

//========================================================================
// Incoming values
//========================================================================
Widget* RootView::findWidget (ParamID id, WidgetKind kind)
{
	for (auto& w : mWidgets)
		if (w.id == id && w.kind == kind)
			return &w;
	return nullptr;
}

//------------------------------------------------------------------------
const Widget* RootView::findWidget (ParamID id, WidgetKind kind) const
{
	for (const auto& w : mWidgets)
		if (w.id == id && w.kind == kind)
			return &w;
	return nullptr;
}

//------------------------------------------------------------------------
int RootView::stepOf (ParamID id) const
{
	if (const auto* w = findWidget (id, WidgetKind::Segmented))
		return w->step;
	return 0;
}

//------------------------------------------------------------------------
void RootView::setStep (ParamID id, int step)
{
	if (auto* seg = findWidget (id, WidgetKind::Segmented))
	{
		if (seg->step != step)
		{
			seg->step = step;
			invalid ();
		}
		return;
	}
	// Input and Output are detented sliders, not segmented controls.
	if (auto* sl = findWidget (id, WidgetKind::Slider))
	{
		const double n = stepToNormalized (step, sl->detents);
		if (std::fabs (sl->norm - n) > 1e-9)
		{
			sl->norm = n;
			invalid ();
		}
	}
}

//------------------------------------------------------------------------
void RootView::setToggle (ParamID id, bool on)
{
	if (auto* sw = findWidget (id, WidgetKind::Switch))
	{
		if (sw->on != on)
		{
			sw->on = on;
			if (id == kParamAutoMakeupId)
				mAutoMakeupOn = on;
			invalid ();
		}
		return;
	}
	if (auto* p = findWidget (id, WidgetKind::Pill))
	{
		if (p->on != on)
		{
			p->on = on;
			invalid ();
		}
	}
}

//------------------------------------------------------------------------
void RootView::setContinuous (ParamID id, double normalized)
{
	if (auto* sl = findWidget (id, WidgetKind::Slider))
	{
		if (std::fabs (sl->norm - normalized) > 1e-9)
		{
			sl->norm = normalized;
			invalid ();
		}
	}
}

//------------------------------------------------------------------------
// The processor pushes these once per audio block -- roughly 100 times a
// second. Redrawing at that rate would be wasteful, so the values are just
// stored here and the timer decides when to repaint.
//------------------------------------------------------------------------
void RootView::setMeterGr (double db) { mMeterGrDb = db; }
void RootView::setMeterIn (double db) { mMeterInDb = db; }
void RootView::setMeterOut (double db) { mMeterOutDb = db; }

//------------------------------------------------------------------------
void RootView::setMeterMakeup (double db)
{
	if (std::fabs (mMakeupDb - db) < 0.05)
		return;
	mMakeupDb = db;
	if (mAutoMakeupOn)
		invalidRect (mValueOutput);
}

//------------------------------------------------------------------------
void RootView::setAppearance (int dark)
{
	const int d = dark ? 1 : 0;
	if (mDark == d)
		return;
	mDark = d;
	invalidateChrome ();
	invalid ();
}

//------------------------------------------------------------------------
void RootView::setBackgroundImagePath (const std::string& path)
{
	mBackgroundImagePath = path;
	mBackgroundImage = nullptr;
	mHasImageAccent = false;
	if (!path.empty ())
	{
		if (auto platformBmp = VSTGUI::getPlatformFactory ().createBitmapFromPath (path.c_str ()))
			mBackgroundImage = VSTGUI::owned (new CBitmap (platformBmp));

		// Recolour the accent family (sliders, switches, gauge arc) to the
		// image's own dominant hue instead of the default beige. Recomputed
		// from the image every time rather than persisted -- it is
		// deterministic from the same file, so there is nothing worth
		// saving in the controller's state stream.
		double hue = 0.0;
		if (mBackgroundImage && computeAccentHueFromImage (mBackgroundImage, hue))
		{
			mAccentHueDeg = hue;
			mHasImageAccent = true;
		}
	}

	mChromeBgToken++;
	invalidateChrome ();
	invalid ();

	// Persisted separately in the controller's own state; see setState /
	// getState there and Glass76Controller::setBackgroundImagePath.
	if (mController)
		mController->setBackgroundImagePath (mBackgroundImagePath);
}

//========================================================================
// Design space <-> view size
//========================================================================
void RootView::setViewSize (const CRect& rect, bool invalid)
{
	CView::setViewSize (rect, invalid);
	updateFit ();
}

//------------------------------------------------------------------------
// scale = min(w/designW, h/designH), centred -- uniform, so nothing ever
// stretches; the smaller axis always leaves letterbox bars on the other,
// painted with the theme's windowBg in drawChrome(). Called from the
// constructor (once, against whatever size the .uidesc template opened
// with) and from setViewSize() (whenever the host's real answer to a
// resize request turns out to differ from what was asked for).
//------------------------------------------------------------------------
void RootView::updateFit ()
{
	const CRect view (getViewSize ());
	const CCoord vw = view.getWidth ();
	const CCoord vh = view.getHeight ();
	if (vw <= 0 || vh <= 0)
	{
		mFit = CGraphicsTransform ();
		return;
	}
	const double scale = std::min (vw / kPanelWidth, vh / kPanelHeight);
	const double offsetX = (vw - kPanelWidth * scale) * 0.5;
	const double offsetY = (vh - kPanelHeight * scale) * 0.5;
	mFit = CGraphicsTransform (scale, 0, 0, scale, offsetX, offsetY);
}

//========================================================================
// Timer: meter animation
//========================================================================
bool RootView::attached (CView* parent)
{
	const bool result = CView::attached (parent);
	if (result && !mTimer)
		startTimer ();
	return result;
}

//------------------------------------------------------------------------
// User-selectable redraw rate: 30 Hz (the original fixed rate -- fast
// enough that the gauge reads as continuous, slow enough that the editor
// costs nothing when nothing is playing), 60 or 120 for smoother motion on
// higher refresh-rate displays.
//------------------------------------------------------------------------
void RootView::startTimer ()
{
	if (mTimer)
		mTimer->stop ();
	const int hz = (mRefreshRateHz == 60 || mRefreshRateHz == 120) ? mRefreshRateHz : 30;
	const uint32_t intervalMs = std::max (1u, static_cast<uint32_t> (1000 / hz));
	mTimer = VSTGUI::owned (new CVSTGUITimer ([this] (CVSTGUITimer*) { onTimer (); }, intervalMs, true));
}

//------------------------------------------------------------------------
void RootView::setRefreshRateHz (int hz)
{
	if (hz != 30 && hz != 60 && hz != 120)
		hz = 30;
	if (mRefreshRateHz == hz)
		return;
	mRefreshRateHz = hz;
	if (mTimer)
		startTimer ();
	invalid ();
}

//------------------------------------------------------------------------
bool RootView::removed (CView* parent)
{
	// A change made just before the editor closes must not be lost inside
	// the debounce window -- see Glass76Controller::flushPrefsNow.
	if (mController)
		mController->flushPrefsNow ();

	if (mTimer)
	{
		mTimer->stop ();
		mTimer = nullptr;
	}
	mChrome = nullptr;
	return CView::removed (parent);
}

//------------------------------------------------------------------------
// Eases `shown` toward `target`, returning whether it moved. Knobs, switch
// thumbs, pill fills and segmented chips all animate through this rather
// than snapping straight to the new value on every parameter change --
// without it, every automation write or click read as an instant jump cut.
//------------------------------------------------------------------------
namespace {
bool easeToward (double& shown, double target, double rate)
{
	const double delta = target - shown;
	if (std::fabs (delta) > 0.0005)
	{
		shown += delta * rate;
		return true;
	}
	if (shown != target)
	{
		shown = target;
		return true;
	}
	return false;
}
} // anonymous namespace

//------------------------------------------------------------------------
void RootView::onTimer ()
{
	// ~210ms to settle (6-7 ticks at 30Hz), which reads as a deliberate
	// glide rather than either an instant snap or a sluggish drag. Four
	// separate passes, in the same relative order the six original vectors
	// were each eased in, though the order does not actually matter here --
	// each widget's easing is independent of every other.
	constexpr double kControlEase = 0.45;
	bool controlsChanged = false;
	for (auto& w : mWidgets)
		if (w.kind == WidgetKind::Slider)
			controlsChanged |= easeToward (w.shownNorm, w.norm, kControlEase);
	for (auto& w : mWidgets)
		if (w.kind == WidgetKind::Switch)
			controlsChanged |= easeToward (w.shownOn, w.on ? 1.0 : 0.0, kControlEase);
	for (auto& w : mWidgets)
		if (w.kind == WidgetKind::Pill)
			controlsChanged |= easeToward (w.shownOn, w.on ? 1.0 : 0.0, kControlEase);
	for (auto& w : mWidgets)
		if (w.kind == WidgetKind::Segmented)
			controlsChanged |= easeToward (w.shownStep, static_cast<double> (w.step), kControlEase);
	if (controlsChanged)
		invalid ();

	const int mode = stepOf (kParamMeterId);
	double target = 0.0;
	switch (mode)
	{
		case kMeterGR:  target = grDbToNormalized (mMeterGrDb); break;
		case kMeterIn:  target = levelDbToNormalized (mMeterInDb); break;
		default:        target = levelDbToNormalized (mMeterOutDb); break;
	}

	// A little extra smoothing on top of the processor's VU ballistics, so
	// the needle never steps between frames.
	const double shown = mGaugeShown + (target - mGaugeShown) * 0.35;
	double peak = mGaugePeak;
	if (shown >= peak)
		peak = shown;
	else
		peak = std::max (shown, peak - 0.006);   // ~0.2 of full scale per second

	const bool changed = std::fabs (shown - mGaugeShown) > 0.0008 ||
	                     std::fabs (peak - mGaugePeak) > 0.0008;
	mGaugeShown = shown;
	mGaugePeak = peak;

	if (changed)
		invalidRect (mGaugeRect);

	// Debounced off this same tick rather than fired synchronously on every
	// click -- see Glass76Controller::flushPrefsIfDue.
	if (mController)
		mController->flushPrefsIfDue ();
}

//========================================================================
// Drawing -- RootView paints nothing itself; every call below hands off to
// mSkin. RootView's job here is caching (the chrome bitmap) and ordering
// (which widgets get painted when), not pixels.
//========================================================================
mac::Theme RootView::theme () const
{
	static const mac::Theme light = mac::makeLightTheme ();
	static const mac::Theme dark = mac::makeDarkTheme ();

	mac::Theme t = mDark ? dark : light;
	if (mHasImageAccent)
		mac::applyAccentHue (t, mAccentHueDeg);
	return t;
}

//------------------------------------------------------------------------
void RootView::invalidateChrome ()
{
	mChrome = nullptr;
	mChromeDark = -1;
}

//------------------------------------------------------------------------
// The static layer -- background wash, glass panels, titles and form
// labels -- is rendered once into a bitmap. Without this, every meter
// frame would repaint four glass panels and their shadows, which is the
// one genuinely expensive thing this editor does.
//------------------------------------------------------------------------
void RootView::ensureChrome (CDrawContext* context)
{
	const double scale = getFrame () ? getFrame ()->getScaleFactor () : 1.0;
	if (mChrome && mChromeDark == mDark && mChromeBgTokenCached == mChromeBgToken &&
	    std::fabs (mChromeScale - scale) < 1e-6)
		return;

	const CRect view (getViewSize ());
	auto offscreen = COffscreenContext::create (CPoint (view.getWidth (), view.getHeight ()), scale);
	if (!offscreen)
	{
		// Fall back to drawing the chrome inline. Slower, but correct.
		mChrome = nullptr;
		mChromeDark = -1;
		drawChrome (context);
		return;
	}

	offscreen->beginDraw ();
	// The offscreen's origin is (0,0); the view may not be, so shift.
	const CGraphicsTransform shift (1, 0, 0, 1, -view.left, -view.top);
	offscreen->setDrawMode (kAntiAliasing);
	{
		CDrawContext::Transform t (*offscreen, shift);
		drawChrome (offscreen);
	}
	offscreen->endDraw ();

	mChrome = offscreen->getBitmap ();
	mChromeScale = scale;
	mChromeDark = mDark;
	mChromeBgTokenCached = mChromeBgToken;

	if (!mChrome)
		drawChrome (context);
}

//------------------------------------------------------------------------
void RootView::drawChrome (CDrawContext* context)
{
	const mac::Theme t = theme ();
	const CRect view (getViewSize ());

	// Letterbox bars, painted at the view's real size, outside mFit -- when
	// the view isn't exactly kPanelWidth x kPanelHeight, this is what shows
	// either side of the scaled, centred content below rather than leaving
	// the host's own background (or nothing) showing through.
	context->setFillColor (t.windowBg);
	context->drawRect (view, kDrawFilled);

	const CRect design (0, 0, kPanelWidth, kPanelHeight);
	const SkinContext sc {t, *this, mDark != 0};

	// Skip pushing an identity transform even though it would be a
	// numerically inert no-op: on the Windows/Direct2D backend it still
	// nudges the text rasteriser onto a different (correctly antialiased,
	// but not bit-identical) code path, which would otherwise cost the
	// no-resize case -- the overwhelming common one -- its pixel-identity
	// snapshot guarantee for zero benefit.
	std::optional<CDrawContext::Transform> fit;
	if (!mFit.isInvariant ())
		fit.emplace (*context, mFit);
	mSkin->paintBackdrop (context, design, mWidgets, mToolbar, mTitleRect, mSubtitleRect, sc);
}

//------------------------------------------------------------------------
void RootView::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing);

	ensureChrome (context);
	if (mChrome)
	{
		CRect view (getViewSize ());
		context->drawBitmap (mChrome, view);
	}

	const mac::Theme t = theme ();
	const SkinContext sc {t, *this, mDark != 0};
	const CRect design (0, 0, kPanelWidth, kPanelHeight);

	// See drawChrome()'s identical guard for why this only pushes when it
	// actually does something.
	std::optional<CDrawContext::Transform> fit;
	if (!mFit.isInvariant ())
		fit.emplace (*context, mFit);

	// Same per-kind pass order the six original vectors were each painted
	// in (Segmented, Slider, Switch, Pill) -- overlapping widgets, if any
	// ever exist, must keep painting in this priority.
	for (const auto& w : mWidgets)
		if (w.kind == WidgetKind::Segmented)
			mSkin->paintWidget (context, w, sc);
	for (const auto& w : mWidgets)
		if (w.kind == WidgetKind::Slider)
			mSkin->paintWidget (context, w, sc);
	for (const auto& w : mWidgets)
		if (w.kind == WidgetKind::Switch)
			mSkin->paintWidget (context, w, sc);
	for (const auto& w : mWidgets)
		if (w.kind == WidgetKind::Pill)
			mSkin->paintWidget (context, w, sc);

	mSkin->paintAppearanceButton (context, sc);
	mSkin->paintSettingsButton (context, sc);
	mSkin->paintValueColumn (context, sc);
	mSkin->paintGauge (context, sc);
	mSkin->paintSettingsOverlay (context, design, sc);

	setDirty (false);
}

//========================================================================
// IWidgetHost: value formatting
//========================================================================
std::string RootView::gainStepText (ParamID id) const
{
	double norm = 0.0;
	if (const auto* sl = findWidget (id, WidgetKind::Slider))
		norm = sl->norm;
	const double db = normalizedToAttenuatorDb (norm);
	if (db <= -600.0)
		return std::string (kMinus) + kInfinity;
	// Continuous now, so one decimal -- the marks are a printed scale, not
	// the set of reachable values.
	return fmt ("%s%.1f dB", kMinus, -db);
}

//------------------------------------------------------------------------
std::string RootView::attackText () const
{
	const int step = clampIndex (stepOf (kParamAttackId), kTimeStepCount);
	const double us = attackStepToSeconds (step) * 1e6;
	return fmt ("%d %ss", static_cast<int> (std::lround (us)), kMicro);
}

//------------------------------------------------------------------------
std::string RootView::releaseText () const
{
	const int step = clampIndex (stepOf (kParamReleaseId), kTimeStepCount);
	const double ms = releaseStepToSeconds (step) * 1e3;
	return fmt ("%d ms", static_cast<int> (std::lround (ms)));
}

//------------------------------------------------------------------------
std::string RootView::ratioText () const
{
	const int step = clampIndex (stepOf (kParamRatioId), kRatioStepCount);
	if (step == kRatioAllStep)
		return "All buttons";
	return fmt ("%d:1", static_cast<int> (kRatioValues[step] + 0.5));
}

//------------------------------------------------------------------------
std::string RootView::mixText () const
{
	double norm = 1.0;
	if (const auto* sl = findWidget (kParamMixId, WidgetKind::Slider))
		norm = sl->norm;
	return fmt ("%d%%", static_cast<int> (std::lround (normalizedToMixPercent (norm))));
}

//------------------------------------------------------------------------
std::string RootView::trimText () const
{
	double norm = 0.5;
	if (const auto* sl = findWidget (kParamTrimId, WidgetKind::Slider))
		norm = sl->norm;
	const double db = normalizedToTrimDb (norm);
	if (db < -0.05)
		return fmt ("%s%.1f dB", kMinus, -db);
	if (db > 0.05)
		return fmt ("+%.1f dB", db);
	return "0.0 dB";
}

//------------------------------------------------------------------------
const CRect& RootView::valueRect (ParamID id) const
{
	switch (id)
	{
		case kParamInputId: return mValueInput;
		case kParamOutputId: return mValueOutput;
		case kParamAttackId: return mValueAttack;
		case kParamReleaseId: return mValueRelease;
		case kParamRatioId: return mValueRatio;
		case kParamMixId: return mValueMix;
		case kParamTrimId: return mValueTrim;
		default: break;
	}
	static const CRect empty (0, 0, 0, 0);
	return empty;
}

//------------------------------------------------------------------------
int RootView::meterMode () const { return stepOf (kParamMeterId); }

//------------------------------------------------------------------------
// Analog only means anything under Signature -- CLEAN has no mains
// emulation to switch, so the control reads as disabled (macOS 27's third
// state, just reduced opacity) instead of silently doing nothing.
//------------------------------------------------------------------------
bool RootView::isDisabled (const Widget& w) const
{
	return w.id == kParamAnalogId && stepOf (kParamModelId) != kModelSignature;
}

//------------------------------------------------------------------------
const CRect& RootView::settingsRateRect (int index) const
{
	return mSettingsRateRect[std::clamp (index, 0, 2)];
}

//========================================================================
// Interaction
//
// macOS 27 ships Idle / Clicked / Disabled only -- there is no hover state
// for these control families, so there is none here either. Adding one is
// a Windows habit.
//========================================================================
CMouseEventResult RootView::onMouseDown (CPoint& where, const CButtonState& buttons)
{
	if (!buttons.isLeftButton () || !mController)
		return kMouseEventNotHandled;

	// Every rect compared against `where` below (mSettingsCloseRect, w.r,
	// ...) is authored in design space -- undo mFit's scale-and-letterbox
	// before hit testing against any of it.
	mFit.inverse ().transform (where);

	if (mSettingsOpen)
	{
		if (hit (mSettingsSkinRect, where))
		{
			const bool nowGlass = (mSkin->id () == SkinId::Glass);
			mController->requestSkinSwitch (nowGlass ? 0 : 1);
			return kMouseEventHandled;
		}
		if (hit (mSettingsCloseRect, where))
		{
			closeSettings ();
			return kMouseEventHandled;
		}
		if (hit (mSettingsChooseRect, where))
		{
			chooseBackgroundImage ();
			return kMouseEventHandled;
		}
		if (hit (mSettingsClearRect, where) && mBackgroundImage)
		{
			clearBackgroundImage ();
			return kMouseEventHandled;
		}
		{
			static constexpr int kRateChoices[3] = {30, 60, 120};
			for (int i = 0; i < 3; i++)
			{
				if (hit (mSettingsRateRect[i], where))
				{
					mController->setRefreshRateHz (kRateChoices[i]);
					setRefreshRateHz (kRateChoices[i]);
					return kMouseEventHandled;
				}
			}
		}
		if (!hit (mSettingsCardRect, where))
			closeSettings ();   // click on the scrim dismisses the panel
		return kMouseEventHandled;   // swallow everything else while open
	}

	if (hit (mSettingsButtonRect, where))
	{
		openSettings ();
		return kMouseEventHandled;
	}

	if (hit (mAppearanceRect, where))
	{
		mController->setAppearance (mDark ? 0 : 1);
		setAppearance (mDark ? 0 : 1);
		return kMouseEventHandled;
	}

	// Priority order -- Pill, Switch, Segmented, Slider -- unchanged from
	// when these were four separate vectors: it is a click-priority rule,
	// not an accident of storage.
	for (const auto& w : mWidgets)
	{
		if (w.kind != WidgetKind::Pill || !hit (w.r, where))
			continue;
		mController->changePill (w.id, !w.on);
		return kMouseEventHandled;
	}

	for (const auto& w : mWidgets)
	{
		if (w.kind != WidgetKind::Switch || !hit (w.r, where))
			continue;
		mController->changePill (w.id, !w.on);
		return kMouseEventHandled;
	}

	for (const auto& w : mWidgets)
	{
		if (w.kind != WidgetKind::Segmented || !hit (w.r, where))
			continue;
		// Analog is inert under CLEAN -- drawn dimmed, and it stays that
		// way rather than accepting a click that would do nothing audible.
		if (w.id == kParamAnalogId && stepOf (kParamModelId) != kModelSignature)
			return kMouseEventHandled;
		mDrag = Drag::Segment;
		mDragId = w.id;
		const int n = static_cast<int> (w.labels.size ());
		const int index =
		    clampIndex (static_cast<int> ((where.x - w.r.left) / (w.r.getWidth () / n)), n);
		if (index != w.step)
			mController->changeStep (w.id, index);
		return kMouseEventHandled;
	}

	for (auto& w : mWidgets)
	{
		// Generous vertical hit area: the row, not just the 6pt track.
		if (w.kind != WidgetKind::Slider || !hit (w.r, where))
			continue;

		if (buttons.isDoubleClick ())
		{
			mController->changeContinuous (w.id, w.defaultNorm);
			return kMouseEventHandled;
		}

		mDrag = Drag::Slider;
		mDragId = w.id;
		applySliderFrom (w, where.x, buttons.getModifierState () & kShift);
		return kMouseEventHandled;
	}

	return kMouseEventNotHandled;
}

//------------------------------------------------------------------------
CMouseEventResult RootView::onMouseMoved (CPoint& where, const CButtonState& buttons)
{
	if (mDrag == Drag::None || !buttons.isLeftButton () || !mController)
		return kMouseEventNotHandled;

	mFit.inverse ().transform (where);

	if (mDrag == Drag::Slider)
	{
		if (auto* sl = findWidget (mDragId, WidgetKind::Slider))
		{
			applySliderFrom (*sl, where.x, buttons.getModifierState () & kShift);
			return kMouseEventHandled;
		}
	}
	else if (mDrag == Drag::Segment)
	{
		if (auto* seg = findWidget (mDragId, WidgetKind::Segmented))
		{
			const int n = static_cast<int> (seg->labels.size ());
			const int index =
			    clampIndex (static_cast<int> ((where.x - seg->r.left) / (seg->r.getWidth () / n)), n);
			if (index != seg->step)
				mController->changeStep (seg->id, index);
			return kMouseEventHandled;
		}
	}

	return kMouseEventNotHandled;
}

//------------------------------------------------------------------------
CMouseEventResult RootView::onMouseUp (CPoint& /*where*/, const CButtonState& /*buttons*/)
{
	mDrag = Drag::None;
	return kMouseEventHandled;
}

//------------------------------------------------------------------------
CMouseEventResult RootView::onMouseCancel ()
{
	mDrag = Drag::None;
	return kMouseEventHandled;
}

//------------------------------------------------------------------------
void RootView::applySliderFrom (Widget& sl, CCoord x, bool fine)
{
	if (!mController)
		return;

	// The knob's centre travels between left + r and right - r, so the
	// value maps to that span, not to the whole track.
	const CCoord knobD = 18.0;
	const CCoord travel = sl.r.getWidth () - knobD;
	if (travel <= 0)
		return;

	double n = (x - sl.r.left - knobD * 0.5) / travel;
	n = std::clamp (n, 0.0, 1.0);

	if (sl.snap && sl.detents > 1)
	{
		// Only for sliders that really are detented, the way an NSSlider
		// with allowsTickMarkValuesOnly behaves.
		n = stepToNormalized (normalizedToStep (n, sl.detents), sl.detents);
	}
	else if (fine)
	{
		// Shift drags at a quarter rate around the current value.
		n = std::clamp (sl.norm + (n - sl.norm) * 0.25, 0.0, 1.0);
	}

	if (std::fabs (n - sl.norm) > 1e-9)
		mController->changeContinuous (sl.id, n);
}

//========================================================================
// Settings panel
//========================================================================
void RootView::openSettings ()
{
	mSettingsOpen = true;
	invalid ();
}

//------------------------------------------------------------------------
void RootView::closeSettings ()
{
	mSettingsOpen = false;
	invalid ();
}

//------------------------------------------------------------------------
void RootView::chooseBackgroundImage ()
{
	auto* frame = getFrame ();
	if (!frame)
		return;

	auto* selector = VSTGUI::CNewFileSelector::create (frame, VSTGUI::CNewFileSelector::kSelectFile);
	if (!selector)
		return;

	selector->setTitle ("Choose Background Image");
	selector->addFileExtension (VSTGUI::CFileExtension ("PNG Image", "png"));
	selector->addFileExtension (VSTGUI::CFileExtension ("JPEG Image", "jpg"));
	selector->addFileExtension (VSTGUI::CFileExtension ("JPEG Image", "jpeg"));
	selector->addFileExtension (VSTGUI::CFileExtension ("Bitmap Image", "bmp"));
	selector->setDefaultExtension (VSTGUI::CFileExtension ("PNG Image", "png"));

	selector->run ([this] (VSTGUI::CNewFileSelector* sel) {
		if (sel->getNumSelectedFiles () > 0)
		{
			if (auto* path = sel->getSelectedFile (0))
				setBackgroundImagePath (path);
		}
	});
	selector->forget ();
}

//------------------------------------------------------------------------
void RootView::clearBackgroundImage ()
{
	setBackgroundImagePath (std::string ());
}

//------------------------------------------------------------------------
} // namespace Jaxson
