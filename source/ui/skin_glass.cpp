//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// See skin_glass.h. Every number here is unchanged from the RootView
// draw* methods it was ported out of -- this file changes *where* the
// macOS 27 painting code lives, not what it draws. Where a value is a
// judgement call, the reasoning is traced to references/macos-27.md in
// the macos-ui-on-windows skill, same as before.
//------------------------------------------------------------------------

#include "skin_glass.h"

#include "macdraw.h"
#include "../params.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace Jaxson {

namespace {

// Punctuation. Source stays ASCII; these are the UTF-8 bytes for the
// characters macOS actually uses -- a real minus sign, not a hyphen.
constexpr const char* kMinus = "\xE2\x88\x92";   // U+2212

std::string fmt (const char* format, ...)
{
	char buffer[128];
	va_list args;
	va_start (args, format);
	std::vsnprintf (buffer, sizeof (buffer), format, args);
	va_end (args);
	return std::string (buffer);
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
		mac::fillSquircle (context, s, radius + i * 0.5, mac::withAlpha (color, 0.05 / i));
	}
}

} // anonymous namespace

//------------------------------------------------------------------------
void GlassSkin::paintBackdrop (CDrawContext* context, const CRect& view,
                               const std::vector<Widget>& widgets, const CRect& toolbar,
                               const CRect& titleRect, const CRect& subtitleRect,
                               const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;

	//--- window background ------------------------------------------
	// #FFFFFF light / #1E1E1E dark [kit]. Aqua's #ECECEC is wrong here.
	context->setFillColor (t.windowBg);
	context->drawRect (view, kDrawFilled);

	CBitmap* bg = sc.host.backgroundImage ();
	if (bg && bg->isLoaded ())
	{
		//--- user background image --------------------------------------
		// This is the actual wallpaper the wash gradients below normally
		// stand in for, so it replaces them outright: the glass panels
		// sample it directly, the way Liquid Glass samples a real desktop.
		const CCoord bw = bg->getWidth ();
		const CCoord bh = bg->getHeight ();
		if (bw > 0 && bh > 0)
		{
			// fillRectWithBitmap does not scale -- on the Direct2D backend it
			// paints srcRect's own pixels 1:1 with WRAP tiling beyond that, so
			// a source rect bigger than the view only ever shows its top-left
			// corner and a smaller one repeats. Getting an actual "cover" fit
			// (scaled to fill the view, centred, excess cropped) needs a real
			// scale in the transform, so draw the whole bitmap through a
			// scale+translate transform instead of pre-cropping a source rect.
			const double scale = std::max (view.getWidth () / bw, view.getHeight () / bh);
			const double drawnW = bw * scale;
			const double drawnH = bh * scale;
			const double offX = view.left + (view.getWidth () - drawnW) * 0.5;
			const double offY = view.top + (view.getHeight () - drawnH) * 0.5;

			ConcatClip clip (*context, view);
			CGraphicsTransform fit;
			fit.scale (scale, scale);
			fit.translate (offX, offY);
			CDrawContext::Transform t2 (*context, fit);
			context->drawBitmap (bg, CRect (0, 0, bw, bh), CPoint (0, 0), 1.0f);
		}

		// A scrim in the window colour so text and glass edges keep reading
		// correctly over arbitrary artwork, the same job the washes do below.
		context->setFillColor (mac::withAlpha (t.windowBg, t.dark ? 0.55 : 0.45));
		context->drawRect (view, kDrawFilled);
	}
	else
	{
		//--- wallpaper stand-in -----------------------------------------
		// A plug-in window has no desktop behind it, so glass has nothing to
		// sample. Two very faint radial washes give the glass something to
		// separate itself from; without them the panels vanish into the
		// background. Only used when there is no real wallpaper to sample.
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addRect (view);
			auto grad =
			    VSTGUI::owned (CGradient::create (0.0, 1.0, t.washA, mac::withAlpha (t.washA, 0.0)));
			if (grad)
			{
				context->fillRadialGradient (path, *grad, CPoint (view.left + 150, view.top + 40),
				                             460);
				auto grad2 = VSTGUI::owned (
				    CGradient::create (0.0, 1.0, t.washB, mac::withAlpha (t.washB, 0.0)));
				if (grad2)
					context->fillRadialGradient (path, *grad2,
					                             CPoint (view.right - 120, view.bottom - 20), 440);
			}
		}
	}

	//--- toolbar ----------------------------------------------------
	// Square corners: it is flush with the window edge, and the host owns
	// the window's own rounding. Radius 0 still gets the full edge stack.
	mac::drawGlassPanel (context, toolbar, 0.0, t, false);
	{
		CRect sep (toolbar.left, toolbar.bottom - 1, toolbar.right, toolbar.bottom);
		context->setFillColor (t.separator);
		context->drawRect (sep, kDrawFilled);
	}

	mac::drawText (context, "Glass76", titleRect, kCenterText, mac::Fonts::get ().headline,
	              t.label1);
	mac::drawText (context, "FET Compressor", subtitleRect, kCenterText, mac::Fonts::get ().subhead,
	              t.label2);

	//--- cards ------------------------------------------------------
	// Glass containers. Their children get plain fills from the
	// over-glass set -- glass never composites on glass.
	for (const auto& w : widgets)
		if (w.kind == WidgetKind::Card)
			mac::drawGlassPanel (context, w.r, mac::kGroupBoxRadius, t, true);

	//--- static text ------------------------------------------------
	const auto& fonts = mac::Fonts::get ();
	for (const auto& w : widgets)
	{
		if (w.kind != WidgetKind::Label)
			continue;
		const CFontRef font =
		    (w.style == 0) ? fonts.headline : (w.style == 1) ? fonts.body : fonts.subhead;
		const CColor color = (w.style == 0) ? t.label1 : t.label2;
		mac::drawText (context, w.text.c_str (), w.r, w.align, font, color);
	}
}

//------------------------------------------------------------------------
void GlassSkin::paintWidget (CDrawContext* context, const Widget& w, const SkinContext& sc) const
{
	switch (w.kind)
	{
		case WidgetKind::Segmented: paintSegmented (context, w, sc); break;
		case WidgetKind::Slider: paintSlider (context, w, sc); break;
		case WidgetKind::Switch: paintSwitch (context, w, sc); break;
		case WidgetKind::Pill: paintPill (context, w, sc); break;
		case WidgetKind::Card:
		case WidgetKind::Label:
			break;   // painted once, into the cached chrome -- see paintBackdrop
	}
}

//------------------------------------------------------------------------
// Segmented control. macOS 27 makes these capsules at Lg and XL; at Rg and
// below the radius is height / 4. The selected chip is a raised white
// capsule inside a recessed trough.
//------------------------------------------------------------------------
void GlassSkin::paintSegmented (CDrawContext* context, const Widget& seg,
                                const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CCoord h = seg.r.getHeight ();
	const CCoord radius = seg.capsule ? mac::capsuleFor (h) : mac::radiusFor (h);

	// Analog only means anything under Signature -- CLEAN has no mains
	// emulation to switch, so the control reads as disabled (macOS 27's
	// third state, just reduced opacity) instead of silently doing nothing.
	const bool disabled = sc.host.isDisabled (seg);
	const double dim = disabled ? 0.4 : 1.0;
	auto dimmed = [dim] (const CColor& c) { return mac::withAlpha (c, (c.alpha / 255.0) * dim); };

	mac::fillSquircle (context, seg.r, radius, dimmed (t.fill2));

	const int n = static_cast<int> (seg.labels.size ());
	if (n <= 0)
		return;

	// The model switch is a wordmark as much as a control: "Glass76 CLEAN"
	// in the same bold text as the rest of the UI, "Glass76 Signature" in a
	// script face. Every other segmented control just uses body text.
	const bool isModelSwitch = (seg.id == kParamModelId);

	const CCoord segW = seg.r.getWidth () / static_cast<CCoord> (n);
	const auto& fonts = mac::Fonts::get ();

	// The chip glides continuously between cells on the timer-eased
	// shownStep, rather than jumping straight from one integer index to the
	// next -- the animated counterpart of the discrete AppKit chip.
	{
		const double shown = std::clamp (seg.shownStep, 0.0, static_cast<double> (n - 1));
		CRect chip (seg.r.left + segW * shown, seg.r.top, seg.r.left + segW * (shown + 1.0),
		           seg.r.bottom);
		chip.inset (2.0, 2.0);
		const CCoord chipRadius = mac::concentricRadius (radius, 2.0, chip.getHeight ());
		if (!disabled)
			chipShadow (context, chip, chipRadius, t.chipShadow);
		mac::fillSquircle (context, chip, chipRadius, dimmed (t.chipFill));
		mac::strokeSquircle (context, chip, chipRadius, dimmed (t.chipRing), 1.0);
	}

	for (int i = 0; i < n; i++)
	{
		CRect cell (seg.r.left + segW * i, seg.r.top, seg.r.left + segW * (i + 1), seg.r.bottom);

		// Hairline between unselected segments, suppressed either side of
		// the chip -- the same rule AppKit uses.
		if (i > 0 && i != seg.step && i != seg.step + 1)
		{
			CRect divider (cell.left, cell.top + 6, cell.left + 1, cell.bottom - 6);
			context->setFillColor (dimmed (t.label4));
			context->drawRect (divider, kDrawFilled);
		}

		const CColor labelColor = dimmed (i == seg.step ? t.label1 : t.label2);

		// "Glass76 Signature": only the "Signature" word is script -- "Glass76"
		// stays in the same bold text every other label on the panel uses.
		if (isModelSwitch && i == 1)
		{
			const std::string& label = seg.labels[i];
			const size_t splitAt = label.find_last_of (' ');
			const std::string head =
			    splitAt == std::string::npos ? std::string () : label.substr (0, splitAt + 1);
			const std::string tail = splitAt == std::string::npos ? label : label.substr (splitAt + 1);

			const CCoord headW =
			    head.empty () ? 0.0 : mac::textWidth (context, head.c_str (), fonts.headline);
			const CCoord tailW = mac::textWidth (context, tail.c_str (), fonts.signature);
			const CCoord left = cell.left + (cell.getWidth () - (headW + tailW)) * 0.5;

			if (!head.empty ())
			{
				CRect headRect (left, cell.top, left + headW, cell.bottom);
				mac::drawText (context, head.c_str (), headRect, kLeftText, fonts.headline,
				              labelColor);
			}
			CRect tailRect (left + headW, cell.top, left + headW + tailW, cell.bottom);
			mac::drawText (context, tail.c_str (), tailRect, kLeftText, fonts.signature, labelColor);
			continue;
		}

		const CFontRef font = isModelSwitch ? fonts.headline : fonts.body;
		mac::drawText (context, seg.labels[i].c_str (), cell, kCenterText, font, labelColor);
	}
}

//------------------------------------------------------------------------
// Slider. Track is 6 tall at Rg and above [kit]. Detented sliders draw
// their tick marks below the track, which is where AppKit puts them.
//------------------------------------------------------------------------
void GlassSkin::paintSlider (CDrawContext* context, const Widget& sl, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CCoord trackH = mac::kSliderTrackHeight;
	const CCoord knobD = 18.0;

	const CCoord cy = (sl.detents > 0) ? sl.r.top + 9.0 : sl.r.getCenter ().y;
	const CRect track (sl.r.left, cy - trackH * 0.5, sl.r.right, cy + trackH * 0.5);

	mac::fillSquircle (context, track, trackH * 0.5, t.fill1);

	// Drawn from the timer-eased shownNorm, not the raw target norm, so a
	// programmatic jump (automation, preset recall, double-click reset)
	// glides instead of snapping. A live drag still tracks the pointer
	// closely -- shownNorm is re-eased every 33ms, faster than it can fall
	// visibly behind a mouse move.
	const CCoord travel = sl.r.getWidth () - knobD;
	const CCoord knobX = sl.r.left + knobD * 0.5 + travel * sl.shownNorm;

	// Filled portion: from the left, or from the centre for a bipolar
	// control like Trim, where the meaningful reference is zero.
	if (sl.bipolar)
	{
		const CCoord centreX = sl.r.getCenter ().x;
		CRect filled (std::min (centreX, knobX), track.top, std::max (centreX, knobX), track.bottom);
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
	CRect knob (knobX - knobD * 0.5, cy - knobD * 0.5, knobX + knobD * 0.5, cy + knobD * 0.5);
	chipShadow (context, knob, knobD * 0.5, t.chipShadow);
	mac::fillSquircle (context, knob, knobD * 0.5, t.chipFill);
	mac::strokeSquircle (context, knob, knobD * 0.5, t.chipRing, 1.0);
}

//------------------------------------------------------------------------
// Switch: 54 x 24 capsule with a 32 x 20 capsule knob inset 2 [kit]. The
// knob is a capsule, not a circle -- that is one of the details that
// separates a 27 switch from an iOS one.
//------------------------------------------------------------------------
void GlassSkin::paintSwitch (CDrawContext* context, const Widget& sw, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CCoord radius = mac::capsuleFor (sw.r.getHeight ());

	// shownOn (timer-eased toward on ? 1 : 0) drives both the thumb's slide
	// and a cross-fade of the track fill, instead of the track colour and
	// thumb position snapping the instant the parameter flips.
	const double f = std::clamp (sw.shownOn, 0.0, 1.0);
	mac::fillSquircle (context, sw.r, radius, mac::mixColor (t.fill1, t.accent, f));

	const CCoord travel = sw.r.getWidth () - 4.0 - mac::kSwitchKnobWidth;
	CRect knob (sw.r.left + 2.0 + travel * f, sw.r.top + 2.0, 0, 0);
	knob.setWidth (mac::kSwitchKnobWidth);
	knob.setHeight (mac::kSwitchKnobHeight);

	const CCoord knobRadius = mac::capsuleFor (knob.getHeight ());
	chipShadow (context, knob, knobRadius, t.chipShadow);
	mac::fillSquircle (context, knob, knobRadius, mac::rgba (255, 255, 255, 0.98 + 0.02 * f));
	mac::strokeSquircle (context, knob, knobRadius, t.chipRing, 1.0);
}

//------------------------------------------------------------------------
// Capsule toggle button in the toolbar. It sits on glass, so it uses the
// over-glass fill set rather than the content-area one.
//------------------------------------------------------------------------
void GlassSkin::paintPill (CDrawContext* context, const Widget& pill, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CCoord radius = mac::capsuleFor (pill.r.getHeight ());

	const double f = std::clamp (pill.shownOn, 0.0, 1.0);
	mac::fillSquircle (context, pill.r, radius, mac::mixColor (t.overGlassIdle, t.accent, f));
	mac::strokeSquircle (context, pill.r, radius, mac::withAlpha (t.chipRing, 1.0 - f), 1.0);

	mac::drawText (context, pill.text.c_str (), pill.r, kCenterText, mac::Fonts::get ().body,
	              mac::mixColor (t.label1, t.accentGlyph, f));
}

//------------------------------------------------------------------------
// Borderless appearance toggle. Toolbar items are completely flat -- no
// gradient, no border, no shadow. The glyph is the half-filled circle
// macOS uses for Appearance, drawn rather than imported: SF Symbols are
// Apple-platform licensed and cannot ship in a Windows binary.
//------------------------------------------------------------------------
void GlassSkin::paintAppearanceButton (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;

	CRect glyph (sc.host.appearanceRect ());
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
// Settings button. Same borderless, flat-glyph treatment as the appearance
// toggle: three slider tracks with knobs at different positions, drawn
// rather than imported for the same licensing reason SF Symbols are out.
//------------------------------------------------------------------------
void GlassSkin::paintSettingsButton (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CColor c = mac::withAlpha (t.label1, 0.70);

	CRect r (sc.host.settingsButtonRect ());
	r.inset (5.0, 5.0);

	context->setFrameColor (c);
	context->setFillColor (c);
	context->setLineWidth (1.5);

	static constexpr double kKnobFrac[3] = {0.30, 0.65, 0.45};
	const CCoord rowH = r.getHeight () / 3.0;

	for (int i = 0; i < 3; i++)
	{
		const CCoord y = r.top + rowH * (i + 0.5);
		context->drawLine (CPoint (r.left, y), CPoint (r.right, y));

		const CCoord kx = r.left + r.getWidth () * kKnobFrac[i];
		CRect knob (kx - 2.0, y - 2.0, kx + 2.0, y + 2.0);
		auto path = VSTGUI::owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (knob);
			context->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}
	}
}

//------------------------------------------------------------------------
void GlassSkin::paintValueColumn (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const IWidgetHost& h = sc.host;

	auto value = [&] (const CRect& r, const std::string& s) {
		if (r.getWidth () > 0)
			mac::drawText (context, s.c_str (), r, kRightText, fonts.body, t.label1);
	};

	value (h.valueRect (kParamInputId), h.gainStepText (kParamInputId));
	value (h.valueRect (kParamOutputId), h.gainStepText (kParamOutputId));

	// Auto make-up applies its gain inside the processor rather than moving
	// the Output control -- driving the parameter would overwrite the
	// setting the user dialled in and write automation. Showing the live
	// amount underneath keeps it visible without touching their value.
	if (h.autoMakeupOn ())
	{
		CRect r (h.valueRect (kParamOutputId));
		r.top = r.bottom - 2;
		r.bottom = r.top + 14;
		const double makeup = h.autoMakeupDb ();
		const std::string s = (makeup >= 0.05) ? fmt ("auto +%.1f", makeup) : "auto";
		mac::drawText (context, s.c_str (), r, kRightText, fonts.caption, t.accent);
	}
	value (h.valueRect (kParamAttackId), h.attackText ());
	value (h.valueRect (kParamReleaseId), h.releaseText ());
	value (h.valueRect (kParamRatioId), h.ratioText ());
	value (h.valueRect (kParamMixId), h.mixText ());
	value (h.valueRect (kParamTrimId), h.trimText ());
}

//------------------------------------------------------------------------
// The gauge. A 240-degree arc with a 6pt track, the same weight as a
// slider track, so the two read as one system. macOS 27 has no VU meter
// to copy, so this follows the language of the progress and activity
// indicators instead of imitating a painted needle.
//------------------------------------------------------------------------
void GlassSkin::paintGauge (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const IWidgetHost& h = sc.host;
	const CRect& gaugeRect = h.gaugeRect ();

	const CPoint centre (gaugeRect.getCenter ().x, gaugeRect.top + 90.0);
	const CCoord radius = 78.0;
	const CCoord trackW = 8.0;

	constexpr double kStartAngle = 150.0;
	constexpr double kSweep = 240.0;

	CRect arcRect (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);

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

	const int mode = h.meterMode ();
	const double shown = std::clamp (h.meterShown (), 0.0, 1.0);

	// Red only where it means something: an output that has passed 0 dBFS.
	const bool overs = (mode == kMeterOut) && (normalizedToLevelDb (shown) > 0.0);
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
	const double peak = h.meterPeak ();
	if (peak > 0.01 && peak > shown + 0.01)
	{
		const double a = (kStartAngle + kSweep * peak) * 3.14159265358979 / 180.0;
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
				label = (i == 0) ? "0"
				                 : fmt ("%s%d", kMinus, static_cast<int> (kMeterGrMaxDb * f + 0.5));
			else
				label = fmt ("%+d", static_cast<int> (std::lround (normalizedToLevelDb (f))));
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
			case kMeterGR: db = h.meterGrDb (); unit = "dB GR"; break;
			case kMeterIn: db = h.meterInDb (); unit = "VU in"; break;
			default: db = h.meterOutDb (); unit = "VU out"; break;
		}

		std::string text;
		if (mode == kMeterGR)
			text = (db < 0.05) ? "0.0" : fmt ("%s%.1f", kMinus, db);
		else if (db <= kMeterLevelMinDb + 0.1)
			text = std::string (kMinus) + "\xE2\x88\x9E";   // U+221E
		else
			text = (db < 0.0) ? fmt ("%s%.1f", kMinus, -db) : fmt ("+%.1f", db);

		CRect valueRect (centre.x - 78, centre.y - 20, centre.x + 78, centre.y + 8);
		mac::drawText (context, text.c_str (), valueRect, kCenterText, fonts.largeTitle, t.label1);

		CRect unitRect (centre.x - 78, centre.y + 12, centre.x + 78, centre.y + 28);
		mac::drawText (context, unit, unitRect, kCenterText, fonts.subhead, t.label2);
	}
}

//------------------------------------------------------------------------
// The settings panel. A scrim over the whole editor plus one glass card,
// drawn live every frame rather than through the cached chrome bitmap --
// it only exists while open, so there is nothing worth caching.
//------------------------------------------------------------------------
void GlassSkin::paintSettingsOverlay (CDrawContext* context, const CRect& view,
                                      const SkinContext& sc) const
{
	const IWidgetHost& h = sc.host;
	if (!h.settingsOpen ())
		return;

	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();

	context->setFillColor (mac::rgba (0, 0, 0, t.dark ? 0.55 : 0.35));
	context->drawRect (view, kDrawFilled);

	const CRect& card = h.settingsCardRect ();
	mac::drawGlassPanel (context, card, mac::kGroupBoxRadius, t, true);

	constexpr CCoord kCardPad = 16;

	CRect title (card);
	title.left += kCardPad;
	title.right -= kCardPad;
	title.top += 18;
	title.bottom = title.top + 24;
	mac::drawText (context, "Settings", title, kLeftText, fonts.title3, t.label1);

	CRect sub (title);
	sub.top = title.bottom;
	sub.bottom = sub.top + 18;
	mac::drawText (context, "Background image", sub, kLeftText, fonts.subhead, t.label2);

	const CRect& chooseRect = h.settingsChooseRect ();
	const CRect& clearRect = h.settingsClearRect ();

	// Choose button.
	{
		const CCoord radius = mac::capsuleFor (chooseRect.getHeight ());
		mac::fillSquircle (context, chooseRect, radius, t.fill2);
		mac::strokeSquircle (context, chooseRect, radius, t.chipRing, 1.0);
		mac::drawText (context, "Choose Image...", chooseRect, kCenterText, fonts.body, t.label1);
	}

	// Clear button -- reads as disabled when there is nothing to clear.
	{
		const CCoord radius = mac::capsuleFor (clearRect.getHeight ());
		const bool hasImage = (h.backgroundImage () != nullptr);
		mac::strokeSquircle (context, clearRect, radius, t.chipRing, 1.0);
		mac::drawText (context, "Clear", clearRect, kCenterText, fonts.body,
		              hasImage ? t.label1 : t.label3);
	}

	CRect pathRect (card);
	pathRect.left += kCardPad;
	pathRect.right -= kCardPad;
	pathRect.top = chooseRect.bottom + 12;
	pathRect.bottom = pathRect.top + 16;
	const std::string& imgPath = h.backgroundImagePath ();
	const std::string pathLabel = imgPath.empty () ? "No image selected" : imgPath;
	mac::drawText (context, pathLabel.c_str (), pathRect, kLeftText, fonts.caption, t.label3);

	CRect sep (card.left + kCardPad, pathRect.bottom + 16, card.right - kCardPad,
	          pathRect.bottom + 17);
	context->setFillColor (t.separator);
	context->drawRect (sep, kDrawFilled);

	CRect rateLabel (sep.left, sep.bottom + 10, sep.left + 200, sep.bottom + 28);
	mac::drawText (context, "Refresh rate", rateLabel, kLeftText, fonts.subhead, t.label2);

	static constexpr int kRateChoices[3] = {30, 60, 120};
	CRect lastRateRect;
	for (int i = 0; i < 3; i++)
	{
		const CRect& r = h.settingsRateRect (i);
		lastRateRect = r;
		const bool selected = (h.refreshRateHz () == kRateChoices[i]);
		const CCoord radius = mac::capsuleFor (r.getHeight ());
		if (selected)
			mac::fillSquircle (context, r, radius, t.accent);
		else
			mac::strokeSquircle (context, r, radius, t.chipRing, 1.0);
		mac::drawText (context, fmt ("%d Hz", kRateChoices[i]).c_str (), r, kCenterText, fonts.body,
		              selected ? t.accentGlyph : t.label1);
	}

	CRect sep2 (card.left + kCardPad, lastRateRect.bottom + 16, card.right - kCardPad,
	           lastRateRect.bottom + 17);
	context->setFillColor (t.separator);
	context->drawRect (sep2, kDrawFilled);

	// Credits: a plain label, then the handle in the same body face as the
	// rest of the panel -- no script face here, it is a name, not a signature.
	CRect creditsLabel (sep2.left, sep2.bottom + 10, sep2.left + 60, sep2.bottom + 32);
	mac::drawText (context, "Credits", creditsLabel, kLeftText, fonts.subhead, t.label2);

	CRect creditsName (creditsLabel.right + 6, sep2.bottom + 2, card.right - kCardPad,
	                   sep2.bottom + 36);
	mac::drawText (context, "@jxxnmade on Instagram", creditsName, kLeftText, fonts.body, t.label1);

	// Done.
	{
		const CRect& closeRect = h.settingsCloseRect ();
		const CCoord radius = mac::capsuleFor (closeRect.getHeight ());
		mac::fillSquircle (context, closeRect, radius, t.accent);
		mac::drawText (context, "Done", closeRect, kCenterText, fonts.body, t.accentGlyph);
	}
}

//------------------------------------------------------------------------
// The skin registry. Hardware currently resolves to the same GlassSkin
// instance as Glass -- the hardware faceplate skin does not exist yet, and
// nothing selects it: RootView still always paints the one Glass layout
// until the faceplate/resize work lands.
//------------------------------------------------------------------------
namespace skins {

const ISkin& get (SkinId /*id*/)
{
	static const GlassSkin glass;
	return glass;
}

const std::vector<const ISkin*>& all ()
{
	static const std::vector<const ISkin*> list = [] {
		std::vector<const ISkin*> v;
		v.push_back (&get (SkinId::Glass));
		return v;
	} ();
	return list;
}

} // namespace skins

} // namespace Jaxson
