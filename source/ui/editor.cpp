//------------------------------------------------------------------------
// Copyright (c) 2026 Jaxson
//
// RootView -- see editor.h for the design notes.
//
// Every number in this file is traced to references/macos-27.md in the
// macos-ui-on-windows skill (measured from Apple's macOS 27 UI kit), or
// derived from it by the rules in that document. Where a value is a
// judgement call it says so.
//------------------------------------------------------------------------

#include "editor.h"

#include "../controller.h"
#include "../params.h"
#include "macdraw.h"
#include "theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

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
constexpr CCoord kCardRadius = mac::kGroupBoxRadius;      // 12 [kit]

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

/** A small drop shadow for chips and knobs: 0 0.5px 1.5px, the scale the
    kit uses for controls. Anything larger reads as a web card. */
void chipShadow (CDrawContext* context, const CRect& r, CCoord radius, const CColor& color)
{
	for (int i = 3; i >= 1; i--)
	{
		CRect s (r);
		s.inset (-static_cast<CCoord> (i) * 0.5, -static_cast<CCoord> (i) * 0.5);
		s.offset (0, 0.5 + i * 0.25);
		mac::fillSquircle (context, s, radius + i * 0.5,
		                   mac::withAlpha (color, 0.05 / i));
	}
}

} // anonymous namespace

//========================================================================
// Construction
//========================================================================
RootView::RootView (Glass76Controller* owner, const CRect& size)
: CView (size), mController (owner)
{
	setMouseEnabled (true);
	setTransparency (false);
	buildLayout ();
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

	//--- toolbar ------------------------------------------------------
	// Unified toolbar + title: 52 tall, which is the kit's control height
	// plus 16. No traffic lights: the host owns the window chrome, and
	// non-functional ones would be a lie.
	mToolbar = R (0, 0, kPanelWidth, kToolbarH);
	mTitleRect = R (0, 10, kPanelWidth, 27);
	mSubtitleRect = R (0, 27, kPanelWidth, 42);

	// Toolbar trailing edge: 20 from the window edge, XL-class controls.
	mAppearanceRect = R (kPanelWidth - 20 - 28, 12, kPanelWidth - 20, 40);
	mPills.push_back ({R (kPanelWidth - 20 - 28 - 8 - 100, 12,
	                      kPanelWidth - 20 - 28 - 8, 40),
	                   kParamCompOffId, "Comp Off", false});

	//--- cards --------------------------------------------------------
	mCards.push_back ({R (kColLeftX, kCardGainT, kColLeftR, kCardGainB), "Gain"});
	mCards.push_back ({R (kColLeftX, kCardDynT, kColLeftR, kCardDynB), "Dynamics"});
	mCards.push_back ({R (kColRightX, kCardMeterT, kColRightR, kCardMeterB), "Meter"});
	mCards.push_back ({R (kColRightX, kCardOutT, kColRightR, kCardOutB), "Output"});

	for (const auto& card : mCards)
	{
		CRect title (card.r);
		title.left += kCardPad;
		title.top += 12;
		title.bottom = title.top + 18;
		title.right = title.left + 200;
		mTexts.push_back ({title, card.title, kLeftText, 0});
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

		mTexts.push_back ({R (kColLeftX + kCardPad, row1, labelR, row1 + 26),
		                   "Input", kRightText, 1});
		mSliders.push_back ({R (ctrlX, row1, ctrlR, row1 + 26), kParamInputId,
		                     kGainStepCount, false, false,
		                     stepToNormalized (kInputDefaultStep, kGainStepCount),
		                     stepToNormalized (kInputDefaultStep, kGainStepCount)});
		mValueInput = R (valueX, row1, valueR, row1 + 26);

		mTexts.push_back ({R (kColLeftX + kCardPad, row2, labelR, row2 + 26),
		                   "Output", kRightText, 1});
		mSliders.push_back ({R (ctrlX, row2, ctrlR, row2 + 26), kParamOutputId,
		                     kGainStepCount, false, false,
		                     stepToNormalized (kOutputDefaultStep, kGainStepCount),
		                     stepToNormalized (kOutputDefaultStep, kGainStepCount)});
		mValueOutput = R (valueX, row2, valueR, row2 + 26);

		mTexts.push_back ({R (kColLeftX + kCardPad, row3, labelR, row3 + 24),
		                   "Auto makeup", kRightText, 1});
		mSwitches.push_back ({R (ctrlX, row3, ctrlX + mac::kSwitchWidth,
		                         row3 + mac::kSwitchHeight),
		                      kParamAutoMakeupId, false});
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

		mTexts.push_back ({R (kColLeftX + kCardPad, row1, labelR, row1 + h),
		                   "Attack", kRightText, 1});
		mSegments.push_back ({R (ctrlX, row1, ctrlR, row1 + h), kParamAttackId,
		                      {"1", "3", "5", "7"}, kAttackDefaultStep, true});
		mValueAttack = R (valueX, row1, valueR, row1 + h);

		mTexts.push_back ({R (kColLeftX + kCardPad, row2, labelR, row2 + h),
		                   "Release", kRightText, 1});
		mSegments.push_back ({R (ctrlX, row2, ctrlR, row2 + h), kParamReleaseId,
		                      {"1", "3", "5", "7"}, kReleaseDefaultStep, true});
		mValueRelease = R (valueX, row2, valueR, row2 + h);

		mTexts.push_back ({R (kColLeftX + kCardPad, row3, labelR, row3 + h),
		                   "Ratio", kRightText, 1});
		mSegments.push_back ({R (ctrlX, row3, ctrlR, row3 + h), kParamRatioId,
		                      {"20:1", "12:1", "8:1", "4:1", "All"},
		                      kRatioDefaultStep, true});
		mValueRatio = R (valueX, row3, valueR, row3 + h);
	}

	//--- Meter card ---------------------------------------------------
	{
		mGaugeRect = R (kColRightX + kCardPad, kCardMeterT + 38,
		                kColRightR - kCardPad, kCardMeterT + 186);
		mSegments.push_back ({R (kColRightX + kCardPad, kCardMeterT + 192,
		                         kColRightR - kCardPad, kCardMeterT + 220),
		                      kParamMeterId, {"GR", "IN", "OUT"},
		                      kMeterDefaultStep, true});
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

		mTexts.push_back ({R (kColRightX + kCardPad, row1, labelR, row1 + 24),
		                   "Mix", kRightText, 1});
		mSliders.push_back ({R (ctrlX, row1, ctrlR, row1 + 24), kParamMixId,
		                     0, false, false, 1.0, 1.0});
		mValueMix = R (valueX, row1, valueR, row1 + 24);

		mTexts.push_back ({R (kColRightX + kCardPad, row2, labelR, row2 + 24),
		                   "Trim", kRightText, 1});
		mSliders.push_back ({R (ctrlX, row2, ctrlR, row2 + 24), kParamTrimId,
		                     0, false, true, trimDbToNormalized (kTrimDefaultDb),
		                     trimDbToNormalized (kTrimDefaultDb)});
		mValueTrim = R (valueX, row2, valueR, row2 + 24);

		mTexts.push_back ({R (kColRightX + kCardPad, row3, labelR, row3 + 24),
		                   "Analog", kRightText, 1});
		mSegments.push_back ({R (ctrlX, row3, ctrlR, row3 + mac::kSizeRg),
		                      kParamAnalogId, {"50 Hz", "60 Hz", "Off"},
		                      kAnalogDefaultStep, false});
	}
}

//========================================================================
// Incoming values
//========================================================================
RootView::Segmented* RootView::findSegmented (ParamID id)
{
	for (auto& s : mSegments)
	{
		if (s.id == id)
			return &s;
	}
	return nullptr;
}

//------------------------------------------------------------------------
RootView::Slider* RootView::findSlider (ParamID id)
{
	for (auto& s : mSliders)
	{
		if (s.id == id)
			return &s;
	}
	return nullptr;
}

//------------------------------------------------------------------------
int RootView::stepOf (ParamID id) const
{
	for (const auto& s : mSegments)
	{
		if (s.id == id)
			return s.step;
	}
	return 0;
}

//------------------------------------------------------------------------
void RootView::setStep (ParamID id, int step)
{
	if (auto* seg = findSegmented (id))
	{
		if (seg->step != step)
		{
			seg->step = step;
			invalid ();
		}
		return;
	}
	// Input and Output are detented sliders, not segmented controls.
	if (auto* sl = findSlider (id))
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
	for (auto& sw : mSwitches)
	{
		if (sw.id == id)
		{
			if (sw.on != on)
			{
				sw.on = on;
				invalid ();
			}
			return;
		}
	}
	for (auto& p : mPills)
	{
		if (p.id == id)
		{
			if (p.on != on)
			{
				p.on = on;
				invalid ();
			}
			return;
		}
	}
}

//------------------------------------------------------------------------
void RootView::setContinuous (ParamID id, double normalized)
{
	if (auto* sl = findSlider (id))
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
void RootView::setAppearance (int dark)
{
	const int d = dark ? 1 : 0;
	if (mDark == d)
		return;
	mDark = d;
	invalidateChrome ();
	invalid ();
}

//========================================================================
// Timer: meter animation
//========================================================================
bool RootView::attached (CView* parent)
{
	const bool result = CView::attached (parent);
	if (result && !mTimer)
	{
		// 30 Hz. Fast enough that the gauge reads as continuous, slow
		// enough that the editor costs nothing when nothing is playing.
		mTimer = VSTGUI::owned (new CVSTGUITimer ([this] (CVSTGUITimer*) { onTimer (); }, 33, true));
	}
	return result;
}

//------------------------------------------------------------------------
bool RootView::removed (CView* parent)
{
	if (mTimer)
	{
		mTimer->stop ();
		mTimer = nullptr;
	}
	mChrome = nullptr;
	return CView::removed (parent);
}

//------------------------------------------------------------------------
void RootView::onTimer ()
{
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
}

//========================================================================
// Drawing
//========================================================================
const mac::Theme& RootView::theme () const
{
	static const mac::Theme light = mac::makeLightTheme ();
	static const mac::Theme dark = mac::makeDarkTheme ();
	return mDark ? dark : light;
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
	if (mChrome && mChromeDark == mDark && std::fabs (mChromeScale - scale) < 1e-6)
		return;

	const CRect view (getViewSize ());
	auto offscreen = COffscreenContext::create (
	    CPoint (view.getWidth (), view.getHeight ()), scale);
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

	if (!mChrome)
		drawChrome (context);
}

//------------------------------------------------------------------------
void RootView::drawChrome (CDrawContext* context)
{
	const mac::Theme& t = theme ();
	const CRect view (getViewSize ());

	//--- window background ------------------------------------------
	// #FFFFFF light / #1E1E1E dark [kit]. Aqua's #ECECEC is wrong here.
	context->setFillColor (t.windowBg);
	context->drawRect (view, kDrawFilled);

	//--- wallpaper stand-in -----------------------------------------
	// A plug-in window has no desktop behind it, so glass has nothing to
	// sample. Two very faint radial washes give the glass something to
	// separate itself from; without them the panels vanish into the white.
	{
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addRect (view);
			auto grad = VSTGUI::owned (CGradient::create (0.0, 1.0, t.washA,
			                                      mac::withAlpha (t.washA, 0.0)));
			if (grad)
			{
				context->fillRadialGradient (path, *grad,
				                             CPoint (view.left + 150, view.top + 40), 460);
				auto grad2 = VSTGUI::owned (CGradient::create (0.0, 1.0, t.washB,
				                                       mac::withAlpha (t.washB, 0.0)));
				if (grad2)
					context->fillRadialGradient (path, *grad2,
					                             CPoint (view.right - 120, view.bottom - 20), 440);
			}
		}
	}

	//--- toolbar ----------------------------------------------------
	// Square corners: it is flush with the window edge, and the host owns
	// the window's own rounding. Radius 0 still gets the full edge stack.
	mac::drawGlassPanel (context, mToolbar, 0.0, t, false);
	{
		CRect sep (mToolbar.left, mToolbar.bottom - 1, mToolbar.right, mToolbar.bottom);
		context->setFillColor (t.separator);
		context->drawRect (sep, kDrawFilled);
	}

	mac::drawText (context, "Glass76", mTitleRect, kCenterText,
	               mac::Fonts::get ().headline, t.label1);
	mac::drawText (context, "FET Compressor", mSubtitleRect, kCenterText,
	               mac::Fonts::get ().subhead, t.label2);

	//--- cards ------------------------------------------------------
	// Glass containers. Their children get plain fills from the
	// over-glass set -- glass never composites on glass.
	for (const auto& card : mCards)
		mac::drawGlassPanel (context, card.r, kCardRadius, t, true);

	//--- static text ------------------------------------------------
	const auto& fonts = mac::Fonts::get ();
	for (const auto& text : mTexts)
	{
		const CFontRef font = (text.style == 0) ? fonts.headline
		                    : (text.style == 1) ? fonts.body
		                                        : fonts.subhead;
		const CColor color = (text.style == 0) ? t.label1 : t.label2;
		mac::drawText (context, text.text.c_str (), text.r, text.align, font, color);
	}
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

	for (const auto& seg : mSegments)
		drawSegmented (context, seg);
	for (const auto& sl : mSliders)
		drawSlider (context, sl);
	for (const auto& sw : mSwitches)
		drawSwitch (context, sw);
	for (const auto& pill : mPills)
		drawPill (context, pill);

	drawAppearanceButton (context);
	drawValueColumn (context);
	drawGauge (context);

	setDirty (false);
}

//------------------------------------------------------------------------
// Segmented control. macOS 27 makes these capsules at Lg and XL; at Rg and
// below the radius is height / 4. The selected chip is a raised white
// capsule inside a recessed trough.
//------------------------------------------------------------------------
void RootView::drawSegmented (CDrawContext* context, const Segmented& seg) const
{
	const mac::Theme& t = theme ();
	const CCoord h = seg.r.getHeight ();
	const CCoord radius = seg.capsule ? mac::capsuleFor (h) : mac::radiusFor (h);

	mac::fillSquircle (context, seg.r, radius, t.fill2);

	const int n = static_cast<int> (seg.labels.size ());
	if (n <= 0)
		return;

	const CCoord segW = seg.r.getWidth () / static_cast<CCoord> (n);
	const auto& fonts = mac::Fonts::get ();

	for (int i = 0; i < n; i++)
	{
		CRect cell (seg.r.left + segW * i, seg.r.top,
		            seg.r.left + segW * (i + 1), seg.r.bottom);

		if (i == seg.step)
		{
			CRect chip (cell);
			chip.inset (2.0, 2.0);
			const CCoord chipRadius = mac::concentricRadius (radius, 2.0, chip.getHeight ());
			chipShadow (context, chip, chipRadius, t.chipShadow);
			mac::fillSquircle (context, chip, chipRadius, t.chipFill);
			mac::strokeSquircle (context, chip, chipRadius, t.chipRing, 1.0);
		}
		else if (i > 0)
		{
			// Hairline between unselected segments, suppressed either side
			// of the chip -- the same rule AppKit uses.
			if (i != seg.step + 1)
			{
				CRect divider (cell.left, cell.top + 6, cell.left + 1, cell.bottom - 6);
				context->setFillColor (t.label4);
				context->drawRect (divider, kDrawFilled);
			}
		}

		mac::drawText (context, seg.labels[i].c_str (), cell, kCenterText,
		               fonts.body, i == seg.step ? t.label1 : t.label2);
	}
}

//------------------------------------------------------------------------
// Slider. Track is 6 tall at Rg and above [kit]. Detented sliders draw
// their tick marks below the track, which is where AppKit puts them.
//------------------------------------------------------------------------
void RootView::drawSlider (CDrawContext* context, const Slider& sl) const
{
	const mac::Theme& t = theme ();
	const CCoord trackH = mac::kSliderTrackHeight;
	const CCoord knobD = 18.0;

	const CCoord cy = (sl.detents > 0) ? sl.r.top + 9.0 : sl.r.getCenter ().y;
	const CRect track (sl.r.left, cy - trackH * 0.5, sl.r.right, cy + trackH * 0.5);

	mac::fillSquircle (context, track, trackH * 0.5, t.fill1);

	const CCoord travel = sl.r.getWidth () - knobD;
	const CCoord knobX = sl.r.left + knobD * 0.5 + travel * sl.norm;

	// Filled portion: from the left, or from the centre for a bipolar
	// control like Trim, where the meaningful reference is zero.
	if (sl.bipolar)
	{
		const CCoord centreX = sl.r.getCenter ().x;
		CRect filled (std::min (centreX, knobX), track.top,
		              std::max (centreX, knobX), track.bottom);
		if (filled.getWidth () > 1.0)
			mac::fillSquircle (context, filled, trackH * 0.5, t.accent);
	}
	else
	{
		CRect filled (track.left, track.top, knobX, track.bottom);
		if (filled.getWidth () > 1.0)
			mac::fillSquircle (context, filled, trackH * 0.5, t.accent);
	}

	//--- tick marks ---------------------------------------------------
	if (sl.detents > 1)
	{
		context->setFillColor (t.label3);
		for (int i = 0; i < sl.detents; i++)
		{
			const double f = static_cast<double> (i) / (sl.detents - 1);
			const CCoord x = sl.r.left + knobD * 0.5 + travel * f;
			CRect tick (x - 0.5, sl.r.top + 21.0, x + 0.5, sl.r.top + 25.0);
			context->drawRect (tick, kDrawFilled);
		}
	}
	else if (sl.bipolar)
	{
		// A single centre detent, so the user can find unity by eye.
		context->setFillColor (t.label3);
		const CCoord x = sl.r.getCenter ().x;
		CRect tick (x - 0.5, cy + 7.0, x + 0.5, cy + 11.0);
		context->drawRect (tick, kDrawFilled);
	}

	//--- knob ---------------------------------------------------------
	CRect knob (knobX - knobD * 0.5, cy - knobD * 0.5,
	            knobX + knobD * 0.5, cy + knobD * 0.5);
	chipShadow (context, knob, knobD * 0.5, t.chipShadow);
	mac::fillSquircle (context, knob, knobD * 0.5, t.chipFill);
	mac::strokeSquircle (context, knob, knobD * 0.5, t.chipRing, 1.0);
}

//------------------------------------------------------------------------
// Switch: 54 x 24 capsule with a 32 x 20 capsule knob inset 2 [kit]. The
// knob is a capsule, not a circle -- that is one of the details that
// separates a 27 switch from an iOS one.
//------------------------------------------------------------------------
void RootView::drawSwitch (CDrawContext* context, const Switch& sw) const
{
	const mac::Theme& t = theme ();
	const CCoord radius = mac::capsuleFor (sw.r.getHeight ());

	mac::fillSquircle (context, sw.r, radius, sw.on ? t.accent : t.fill1);

	const CCoord travel = sw.r.getWidth () - 4.0 - mac::kSwitchKnobWidth;
	CRect knob (sw.r.left + 2.0 + (sw.on ? travel : 0.0), sw.r.top + 2.0, 0, 0);
	knob.setWidth (mac::kSwitchKnobWidth);
	knob.setHeight (mac::kSwitchKnobHeight);

	const CCoord knobRadius = mac::capsuleFor (knob.getHeight ());
	chipShadow (context, knob, knobRadius, t.chipShadow);
	mac::fillSquircle (context, knob, knobRadius,
	                   mac::rgba (255, 255, 255, sw.on ? 1.0 : 0.98));
	mac::strokeSquircle (context, knob, knobRadius, t.chipRing, 1.0);
}

//------------------------------------------------------------------------
// Capsule toggle button in the toolbar. It sits on glass, so it uses the
// over-glass fill set rather than the content-area one.
//------------------------------------------------------------------------
void RootView::drawPill (CDrawContext* context, const Pill& pill) const
{
	const mac::Theme& t = theme ();
	const CCoord radius = mac::capsuleFor (pill.r.getHeight ());

	mac::fillSquircle (context, pill.r, radius, pill.on ? t.accent : t.overGlassIdle);
	if (!pill.on)
		mac::strokeSquircle (context, pill.r, radius, t.chipRing, 1.0);

	mac::drawText (context, pill.label.c_str (), pill.r, kCenterText,
	               mac::Fonts::get ().body, pill.on ? t.accentGlyph : t.label1);
}

//------------------------------------------------------------------------
// Borderless appearance toggle. Toolbar items are completely flat -- no
// gradient, no border, no shadow. The glyph is the half-filled circle
// macOS uses for Appearance, drawn rather than imported: SF Symbols are
// Apple-platform licensed and cannot ship in a Windows binary.
//------------------------------------------------------------------------
void RootView::drawAppearanceButton (CDrawContext* context) const
{
	const mac::Theme& t = theme ();

	CRect glyph (mAppearanceRect);
	glyph.inset (6.0, 6.0);

	auto path = VSTGUI::owned (context->createGraphicsPath ());
	if (!path)
		return;

	// Filled half.
	path->addArc (glyph, 90.0, 270.0, true);
	path->closeSubpath ();
	context->setFillColor (mac::withAlpha (t.label1, 0.70));
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);

	// Outline of the whole circle.
	auto ring = VSTGUI::owned (context->createGraphicsPath ());
	if (ring)
	{
		ring->addEllipse (glyph);
		context->setFrameColor (mac::withAlpha (t.label1, 0.70));
		context->setLineWidth (1.5);
		context->drawGraphicsPath (ring, CDrawContext::kPathStroked);
	}
}

//------------------------------------------------------------------------
void RootView::drawValueColumn (CDrawContext* context) const
{
	const mac::Theme& t = theme ();
	const auto& fonts = mac::Fonts::get ();

	auto value = [&] (const CRect& r, const std::string& s) {
		if (r.getWidth () > 0)
			mac::drawText (context, s.c_str (), r, kRightText, fonts.body, t.label1);
	};

	value (mValueInput, gainStepText (kParamInputId));
	value (mValueOutput, gainStepText (kParamOutputId));
	value (mValueAttack, attackText ());
	value (mValueRelease, releaseText ());
	value (mValueRatio, ratioText ());
	value (mValueMix, mixText ());
	value (mValueTrim, trimText ());
}

//------------------------------------------------------------------------
// The gauge. A 240-degree arc with a 6pt track, the same weight as a
// slider track, so the two read as one system. macOS 27 has no VU meter
// to copy, so this follows the language of the progress and activity
// indicators instead of imitating a painted needle.
//------------------------------------------------------------------------
void RootView::drawGauge (CDrawContext* context) const
{
	const mac::Theme& t = theme ();
	const auto& fonts = mac::Fonts::get ();

	const CPoint centre (mGaugeRect.getCenter ().x, mGaugeRect.top + 90.0);
	const CCoord radius = 78.0;
	const CCoord trackW = 8.0;

	constexpr double kStartAngle = 150.0;
	constexpr double kSweep = 240.0;

	CRect arcRect (centre.x - radius, centre.y - radius,
	               centre.x + radius, centre.y + radius);

	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->setLineWidth (trackW);

	//--- track --------------------------------------------------------
	{
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addArc (arcRect, kStartAngle, kStartAngle + kSweep, true);
			context->setFrameColor (t.fill1);
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	}

	const int mode = stepOf (kParamMeterId);
	const double shown = std::clamp (mGaugeShown, 0.0, 1.0);

	// Red only where it means something: an output that has passed 0 dBFS.
	const bool overs = (mode == kMeterOut) &&
	                   (normalizedToLevelDb (shown) > 0.0);
	const CColor arcColor = overs ? mac::rgba (255, 56, 60, 1.0) : t.accent;

	//--- value arc ----------------------------------------------------
	if (shown > 0.002)
	{
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addArc (arcRect, kStartAngle, kStartAngle + kSweep * shown, true);
			context->setFrameColor (arcColor);
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	}

	//--- peak hold ----------------------------------------------------
	if (mGaugePeak > 0.01 && mGaugePeak > shown + 0.01)
	{
		const double a = (kStartAngle + kSweep * mGaugePeak) * 3.14159265358979 / 180.0;
		const CPoint p (centre.x + std::cos (a) * radius, centre.y + std::sin (a) * radius);
		CRect dot (p.x - 2.5, p.y - 2.5, p.x + 2.5, p.y + 2.5);
		context->setFillColor (mac::withAlpha (arcColor, 0.55));
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (dot);
			context->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}
	}

	//--- scale ticks and their labels ---------------------------------
	context->setLineWidth (1.0);
	context->setLineStyle (kLineSolid);
	for (int i = 0; i <= 4; i++)
	{
		const double f = i / 4.0;
		const double a = (kStartAngle + kSweep * f) * 3.14159265358979 / 180.0;
		const double c = std::cos (a), s = std::sin (a);
		context->setFrameColor (t.label3);
		context->drawLine (CPoint (centre.x + c * (radius + 8), centre.y + s * (radius + 8)),
		                   CPoint (centre.x + c * (radius + 12), centre.y + s * (radius + 12)));

		if (i == 0 || i == 2 || i == 4)
		{
			std::string label;
			if (mode == kMeterGR)
				label = (i == 0) ? "0" : fmt ("%s%d", kMinus,
				                              static_cast<int> (kMeterGrMaxDb * f + 0.5));
			else
				label = fmt ("%d", static_cast<int> (std::lround (normalizedToLevelDb (f))));
			if (!label.empty () && label[0] == '-')
				label = std::string (kMinus) + label.substr (1);

			const CPoint lp (centre.x + c * (radius + 26), centre.y + s * (radius + 26));
			CRect lr (lp.x - 24, lp.y - 8, lp.x + 24, lp.y + 8);
			mac::drawText (context, label.c_str (), lr, kCenterText, fonts.caption, t.label3);
		}
	}

	//--- centre readout -----------------------------------------------
	{
		double db = 0.0;
		const char* unit = "dB GR";
		switch (mode)
		{
			case kMeterGR: db = mMeterGrDb; unit = "dB GR"; break;
			case kMeterIn: db = mMeterInDb; unit = "dBFS in"; break;
			default:       db = mMeterOutDb; unit = "dBFS out"; break;
		}

		std::string text;
		if (mode == kMeterGR)
			text = (db < 0.05) ? "0.0" : fmt ("%s%.1f", kMinus, db);
		else if (db <= kMeterLevelMinDb + 0.1)
			text = std::string (kMinus) + kInfinity;
		else
			text = (db < 0.0) ? fmt ("%s%.1f", kMinus, -db) : fmt ("%.1f", db);

		CRect valueRect (centre.x - 78, centre.y - 20, centre.x + 78, centre.y + 8);
		mac::drawText (context, text.c_str (), valueRect, kCenterText, fonts.largeTitle, t.label1);

		CRect unitRect (centre.x - 78, centre.y + 12, centre.x + 78, centre.y + 28);
		mac::drawText (context, unit, unitRect, kCenterText, fonts.subhead, t.label2);
	}
}

//========================================================================
// Value formatting
//========================================================================
std::string RootView::gainStepText (ParamID id) const
{
	double norm = 0.0;
	for (const auto& sl : mSliders)
	{
		if (sl.id == id)
			norm = sl.norm;
	}
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
	for (const auto& sl : mSliders)
	{
		if (sl.id == kParamMixId)
			norm = sl.norm;
	}
	return fmt ("%d%%", static_cast<int> (std::lround (normalizedToMixPercent (norm))));
}

//------------------------------------------------------------------------
std::string RootView::trimText () const
{
	double norm = 0.5;
	for (const auto& sl : mSliders)
	{
		if (sl.id == kParamTrimId)
			norm = sl.norm;
	}
	const double db = normalizedToTrimDb (norm);
	if (db < -0.05)
		return fmt ("%s%.1f dB", kMinus, -db);
	if (db > 0.05)
		return fmt ("+%.1f dB", db);
	return "0.0 dB";
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

	if (hit (mAppearanceRect, where))
	{
		mController->setAppearance (mDark ? 0 : 1);
		setAppearance (mDark ? 0 : 1);
		return kMouseEventHandled;
	}

	for (const auto& pill : mPills)
	{
		if (hit (pill.r, where))
		{
			mController->changePill (pill.id, !pill.on);
			return kMouseEventHandled;
		}
	}

	for (const auto& sw : mSwitches)
	{
		if (hit (sw.r, where))
		{
			mController->changePill (sw.id, !sw.on);
			return kMouseEventHandled;
		}
	}

	for (const auto& seg : mSegments)
	{
		if (!hit (seg.r, where))
			continue;
		mDrag = Drag::Segment;
		mDragId = seg.id;
		const int n = static_cast<int> (seg.labels.size ());
		const int index = clampIndex (
		    static_cast<int> ((where.x - seg.r.left) / (seg.r.getWidth () / n)), n);
		if (index != seg.step)
			mController->changeStep (seg.id, index);
		return kMouseEventHandled;
	}

	for (auto& sl : mSliders)
	{
		// Generous vertical hit area: the row, not just the 6pt track.
		if (!hit (sl.r, where))
			continue;

		if (buttons.isDoubleClick ())
		{
			mController->changeContinuous (sl.id, sl.defaultNorm);
			return kMouseEventHandled;
		}

		mDrag = Drag::Slider;
		mDragId = sl.id;
		applySliderFrom (sl, where.x, buttons.getModifierState () & kShift);
		return kMouseEventHandled;
	}

	return kMouseEventNotHandled;
}

//------------------------------------------------------------------------
CMouseEventResult RootView::onMouseMoved (CPoint& where, const CButtonState& buttons)
{
	if (mDrag == Drag::None || !buttons.isLeftButton () || !mController)
		return kMouseEventNotHandled;

	if (mDrag == Drag::Slider)
	{
		if (auto* sl = findSlider (mDragId))
		{
			applySliderFrom (*sl, where.x, buttons.getModifierState () & kShift);
			return kMouseEventHandled;
		}
	}
	else if (mDrag == Drag::Segment)
	{
		if (auto* seg = findSegmented (mDragId))
		{
			const int n = static_cast<int> (seg->labels.size ());
			const int index = clampIndex (
			    static_cast<int> ((where.x - seg->r.left) / (seg->r.getWidth () / n)), n);
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
void RootView::applySliderFrom (Slider& sl, CCoord x, bool fine)
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

//------------------------------------------------------------------------
} // namespace Jaxson
