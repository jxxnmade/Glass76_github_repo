//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// See skin_hardware.h. First-pass procedural metal/knob/meter rendering --
// no bitmap assets, same as GlassSkin, built from VSTGUI gradients and
// paths rather than photographs. Faithful to the classic 1176-style
// layout and control shapes; not attempting photoreal brushed-metal
// texture or knob machining detail in this pass.
//------------------------------------------------------------------------

#include "skin_hardware.h"
#include "skin_glass.h"

#include "macdraw.h"
#include "../params.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace Jaxson {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Knob sweep: 135deg (screen convention, clockwise from east/3-o'clock,
// y-down) at norm 0 -- pointing to the lower-left, "7:30" on a clock face --
// through 270deg total to 405 (=45) at norm 1, the lower-right "4:30"
// position. Standard hardware/software knob travel.
constexpr double kKnobStartDeg = 135.0;
constexpr double kKnobSweepDeg = 270.0;

} // anonymous namespace

//------------------------------------------------------------------------
// One continuous brushed-metal panel: no group boxes, the way a real rack
// unit has one silkscreened plate, not a card per section. The "brushing"
// is a handful of soft horizontal gradient bands rather than a true noise
// texture -- a reasonable procedural stand-in, not a photograph.
//------------------------------------------------------------------------
void HardwareSkin::paintBackdrop (CDrawContext* context, const CRect& view,
                                  const std::vector<Widget>& widgets, const CRect& toolbar,
                                  const CRect& titleRect, const CRect& subtitleRect,
                                  const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();

	// Base plate: dark theme = near-black brushed metal ("Blacky"), light
	// theme = brushed silver ("Bluey") -- the same light/dark toggle Glass
	// already exposes, repurposed as the two classic hardware finishes
	// rather than adding a second preference for it.
	const CColor plateTop = t.dark ? mac::rgba (58, 58, 60, 1.0) : mac::rgba (206, 208, 212, 1.0);
	const CColor plateBottom = t.dark ? mac::rgba (24, 24, 25, 1.0) : mac::rgba (158, 160, 165, 1.0);
	{
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->addRect (view);
			auto grad = owned (CGradient::create (0.0, 1.0, plateTop, plateBottom));
			if (grad)
				context->fillLinearGradient (path, *grad, CPoint (view.left, view.top),
				                             CPoint (view.left, view.bottom), false);
		}
		// Brushing: faint alternating horizontal hairlines.
		for (int i = 0; i < 24; i++)
		{
			const double f = static_cast<double> (i) / 24.0;
			const CCoord y = view.top + f * view.getHeight ();
			context->setFillColor (mac::rgba (255, 255, 255, (i % 2 == 0) ? 0.02 : 0.0));
			context->drawRect (CRect (view.left, y, view.right, y + 1.0), kDrawFilled);
		}
	}

	// Toolbar: a slightly darker inset strip, screwed-panel look via two
	// corner "screws" (small dark dots) rather than a glass fill.
	{
		context->setFillColor (mac::withAlpha (mac::rgba (0, 0, 0, 1.0), t.dark ? 0.35 : 0.12));
		context->drawRect (toolbar, kDrawFilled);
		context->setFillColor (t.separator);
		context->drawRect (CRect (toolbar.left, toolbar.bottom - 1, toolbar.right, toolbar.bottom),
		                   kDrawFilled);
		for (double x : {toolbar.left + 10.0, toolbar.right - 10.0})
		{
			CRect screw (x - 3, toolbar.getCenter ().y - 3, x + 3, toolbar.getCenter ().y + 3);
			context->setFillColor (mac::rgba (10, 10, 10, t.dark ? 0.6 : 0.35));
			auto path = owned (context->createGraphicsPath ());
			if (path)
			{
				path->addEllipse (screw);
				context->drawGraphicsPath (path, CDrawContext::kPathFilled);
			}
		}
	}

	mac::drawText (context, "Glass76", titleRect, kCenterText, fonts.headline, t.label1);
	mac::drawText (context, "FET Compressor", subtitleRect, kCenterText, fonts.subhead, t.label2);

	//--- static text (RATIO/AUTO MAKEUP labels, knob names, the meter's own
	// "Glass76" wordmark -- all plain Labels, same as Glass's static text) --
	for (const auto& w : widgets)
	{
		if (w.kind != WidgetKind::Label)
			continue;
		const CFontRef font = (w.style == 0) ? fonts.title1 : (w.style == 1) ? fonts.caption
		                                                                    : fonts.subhead;
		const CColor color = (w.style == 0) ? t.label1 : t.label2;
		mac::drawText (context, w.text.c_str (), w.r, w.align, font, color);
	}
}

//------------------------------------------------------------------------
void HardwareSkin::paintWidget (CDrawContext* context, const Widget& w, const SkinContext& sc) const
{
	switch (w.kind)
	{
		case WidgetKind::Knob: paintKnob (context, w, sc); break;
		case WidgetKind::Segmented: paintSegmented (context, w, sc); break;
		case WidgetKind::Switch: paintSwitch (context, w, sc); break;
		case WidgetKind::Pill: paintPill (context, w, sc); break;
		case WidgetKind::Slider:   // Hardware's layout never creates one
		case WidgetKind::Card:
		case WidgetKind::Label:
			break;
	}
}

//------------------------------------------------------------------------
// A metal dome (radial gradient, off-centre highlight), a pointer line at
// the value's angle, and the live value text underneath -- no printed tick
// ring in this pass (see the class comment: not attempting the reference's
// full engraved-scale fidelity procedurally). detents/bipolar/norm are the
// same fields Slider already uses; only the shape and the drag gesture
// differ (see RootView::applyKnobFrom).
//------------------------------------------------------------------------
void HardwareSkin::paintKnob (CDrawContext* context, const Widget& w, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const CRect& r = w.r;
	const CCoord cx = r.getCenter ().x;
	const CCoord cy = r.getCenter ().y;
	const CCoord radius = std::min (r.getWidth (), r.getHeight ()) * 0.5;

	{
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (r);
			const CColor hi = t.dark ? mac::rgba (92, 90, 86, 1.0) : mac::rgba (245, 245, 247, 1.0);
			const CColor lo = t.dark ? mac::rgba (14, 13, 12, 1.0) : mac::rgba (140, 140, 145, 1.0);
			auto grad = owned (CGradient::create (0.0, 1.0, hi, lo));
			if (grad)
				context->fillRadialGradient (path, *grad, CPoint (cx - radius * 0.35, cy - radius * 0.35),
				                             radius * 1.35, CPoint (cx, cy));
		}
	}

	// Containment ring.
	{
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (r);
			context->setFrameColor (mac::rgba (0, 0, 0, t.dark ? 0.6 : 0.35));
			context->setLineWidth (1.5);
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	}

	// Specular highlight -- a soft bright patch upper-left, like a dome
	// catching a single light source.
	{
		CRect hi (cx - radius * 0.65, cy - radius * 0.75, cx - radius * 0.1, cy - radius * 0.2);
		context->setFillColor (mac::withAlpha (mac::rgba (255, 255, 255, 1.0), t.dark ? 0.10 : 0.30));
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (hi);
			context->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}
	}

	// Pointer.
	{
		const double angleDeg = kKnobStartDeg + std::clamp (w.shownNorm, 0.0, 1.0) * kKnobSweepDeg;
		const double rad = angleDeg * kPi / 180.0;
		const CCoord p0 = radius * 0.20, p1 = radius * 0.82;
		const CPoint a (cx + std::cos (rad) * p0, cy + std::sin (rad) * p0);
		const CPoint b (cx + std::cos (rad) * p1, cy + std::sin (rad) * p1);
		context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
		context->setLineWidth (std::max (2.0, radius * 0.06));
		context->setFrameColor (mac::rgba (255, 255, 255, t.dark ? 0.95 : 1.0));
		context->drawLine (a, b);
	}

	// Live value text, centred just under the knob.
	std::string text;
	switch (w.id)
	{
		case kParamInputId:
		case kParamOutputId: text = sc.host.gainStepText (w.id); break;
		case kParamAttackId: text = sc.host.attackText (); break;
		case kParamReleaseId: text = sc.host.releaseText (); break;
		case kParamMixId: text = sc.host.mixText (); break;
		case kParamTrimId: text = sc.host.trimText (); break;
		default: break;
	}
	if (!text.empty ())
	{
		CRect valueR (r.left - 20, r.bottom + 20, r.right + 20, r.bottom + 36);
		mac::drawText (context, text.c_str (), valueR, kCenterText, fonts.caption, t.label2);
	}
}

//------------------------------------------------------------------------
// Vertical or horizontal button stack -- same param/step model as Glass's
// Segmented, just painted as a stack of individually-outlined rectangular
// buttons (the reference's Ratio/Meter-select look) instead of a single
// capsule trough with a sliding chip.
//------------------------------------------------------------------------
void HardwareSkin::paintSegmented (CDrawContext* context, const Widget& seg, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const int n = static_cast<int> (seg.labels.size ());
	if (n <= 0)
		return;

	const bool disabled = sc.host.isDisabled (seg);

	for (int i = 0; i < n; i++)
	{
		CRect r (seg.r);
		if (seg.vertical)
		{
			const CCoord h = seg.r.getHeight () / n;
			r.top = seg.r.top + h * i;
			r.bottom = r.top + h - 4.0;
		}
		else
		{
			const CCoord w = seg.r.getWidth () / n;
			r.left = seg.r.left + w * i;
			r.right = r.left + w - 4.0;
		}

		const bool selected = (i == seg.step);
		context->setFillColor (selected ? (t.dark ? mac::rgba (235, 235, 238, 1.0)
		                                          : mac::rgba (250, 250, 250, 1.0))
		                                : mac::rgba (0, 0, 0, t.dark ? 0.35 : 0.15));
		context->drawRect (r, kDrawFilled);
		context->setFrameColor (mac::rgba (0, 0, 0, t.dark ? 0.6 : 0.3));
		context->setLineWidth (1.0);
		context->drawRect (r, kDrawStroked);

		const CColor textColor = disabled ? t.label3
		                        : selected ? mac::rgba (20, 20, 20, 1.0)
		                                   : t.label1;
		mac::drawText (context, seg.labels[static_cast<size_t> (i)].c_str (), r, kCenterText,
		              fonts.caption, textColor);
	}
}

//------------------------------------------------------------------------
void HardwareSkin::paintSwitch (CDrawContext* context, const Widget& w, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const CRect& r = w.r;
	const CCoord radius = mac::capsuleFor (r.getHeight ());
	const bool on = w.shownOn > 0.5;

	mac::fillSquircle (context, r, radius, on ? t.accent : mac::rgba (0, 0, 0, t.dark ? 0.4 : 0.2));
	mac::strokeSquircle (context, r, radius, mac::rgba (0, 0, 0, t.dark ? 0.6 : 0.3), 1.0);

	const CCoord knobD = r.getHeight () - 4.0;
	const CCoord travel = r.getWidth () - knobD - 4.0;
	const CCoord kx = r.left + 2.0 + travel * w.shownOn;
	CRect knob (kx, r.top + 2.0, kx + knobD, r.bottom - 2.0);
	context->setFillColor (mac::rgba (245, 245, 245, 1.0));
	auto path = owned (context->createGraphicsPath ());
	if (path)
	{
		path->addEllipse (knob);
		context->drawGraphicsPath (path, CDrawContext::kPathFilled);
	}
}

//------------------------------------------------------------------------
// Comp Off: the reference's one control that stands apart from its group
// with colour (red) rather than the group's own selected/unselected look --
// a Pill, not part of the Meter Segmented set, same as buildLayout places it.
//------------------------------------------------------------------------
void HardwareSkin::paintPill (CDrawContext* context, const Widget& w, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const CRect& r = w.r;
	const bool on = w.shownOn > 0.5;

	context->setFillColor (on ? mac::rgba (196, 40, 40, 1.0) : mac::rgba (0, 0, 0, t.dark ? 0.35 : 0.15));
	context->drawRect (r, kDrawFilled);
	context->setFrameColor (mac::rgba (0, 0, 0, t.dark ? 0.6 : 0.3));
	context->setLineWidth (1.0);
	context->drawRect (r, kDrawStroked);
	mac::drawText (context, w.text.c_str (), r, kCenterText, fonts.caption,
	              on ? mac::rgba (255, 255, 255, 1.0) : t.label1);
}

//------------------------------------------------------------------------
void HardwareSkin::paintAppearanceButton (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	CRect glyph (sc.host.appearanceRect ());
	glyph.inset (6.0, 6.0);

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addArc (glyph, 90.0, 270.0, true);
	path->closeSubpath ();
	context->setFillColor (mac::withAlpha (t.label1, 0.70));
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);

	auto ring = owned (context->createGraphicsPath ());
	if (ring)
	{
		ring->addEllipse (glyph);
		context->setFrameColor (mac::withAlpha (t.label1, 0.70));
		context->setLineWidth (1.5);
		context->drawGraphicsPath (ring, CDrawContext::kPathStroked);
	}
}

//------------------------------------------------------------------------
void HardwareSkin::paintSettingsButton (CDrawContext* context, const SkinContext& sc) const
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
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->addEllipse (knob);
			context->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}
	}
}

//------------------------------------------------------------------------
// No separate value column: every knob prints its own live value directly
// underneath itself (see paintKnob), the way a real hardware unit's single
// printed scale per knob works, rather than Glass's shared right-aligned
// digital readout column.
//------------------------------------------------------------------------
void HardwareSkin::paintValueColumn (CDrawContext* /*context*/, const SkinContext& /*sc*/) const {}

//------------------------------------------------------------------------
// The analog VU meter: a cream face (fixed colour regardless of app theme,
// like a real meter's face is fixed regardless of the rack's finish),
// printed scale ticks, a red zone above 0, and a needle pivoting from
// bottom-centre -- reusing the exact same smoothed meterShown()/meterPeak()
// state Glass's own circular gauge reads, just rendered as a needle sweep
// instead of an arc fill.
//------------------------------------------------------------------------
void HardwareSkin::paintGauge (CDrawContext* context, const SkinContext& sc) const
{
	const mac::Theme& t = sc.theme;
	const auto& fonts = mac::Fonts::get ();
	const IWidgetHost& h = sc.host;
	const CRect& face = h.gaugeRect ();

	// Face.
	context->setFillColor (mac::rgba (230, 222, 194, 1.0));
	context->drawRect (face, kDrawFilled);
	context->setFrameColor (mac::rgba (20, 18, 14, 1.0));
	context->setLineWidth (2.0);
	context->drawRect (face, kDrawStroked);

	// Needle pivots from just below the face, on its horizontal centre.
	// Angle is measured from straight up (0deg), clockwise positive -- NOT
	// the usual "from east" math convention -- since that maps directly
	// onto screen coordinates (y grows downward) without the sign errors
	// that convention invites here: up is simply (0, -len), and a positive
	// angle leans right, both exactly as written below.
	const CPoint pivot (face.getCenter ().x, face.bottom + 14.0);
	const CCoord needleLen = face.getHeight () * 0.98;
	constexpr double kStartDeg = -60.0;   // pointing up-left
	constexpr double kSweepDeg = 120.0;   // sweeping to up-right

	auto pointAt = [&] (double angleFromUpDeg, CCoord len) {
		const double rad = angleFromUpDeg * kPi / 180.0;
		return CPoint (pivot.x + std::sin (rad) * len, pivot.y - std::cos (rad) * len);
	};

	// Printed scale: a handful of ticks across the sweep, red past 0.
	{
		static constexpr double kTicks[8] = {0.0, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 1.0};
		for (double f : kTicks)
		{
			const double deg = kStartDeg + kSweepDeg * f;
			const CPoint p0 = pointAt (deg, needleLen * 0.72);
			const CPoint p1 = pointAt (deg, needleLen * 0.82);
			context->setFrameColor (f > 0.72 ? mac::rgba (170, 30, 30, 1.0) : mac::rgba (30, 26, 20, 1.0));
			context->setLineWidth (1.5);
			context->drawLine (p0, p1);
		}
	}

	const double shown = std::clamp (h.meterShown (), 0.0, 1.0);
	const int mode = h.meterMode ();
	const bool overs = (mode == kMeterOut) && (normalizedToLevelDb (shown) > 0.0);

	const CPoint tip = pointAt (kStartDeg + kSweepDeg * shown, needleLen);
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->setLineWidth (2.0);
	context->setFrameColor (overs ? mac::rgba (200, 20, 20, 1.0) : mac::rgba (25, 22, 18, 1.0));
	context->drawLine (pivot, tip);

	// Pivot cap.
	CRect cap (pivot.x - 4, pivot.y - 4, pivot.x + 4, pivot.y + 4);
	context->setFillColor (mac::rgba (25, 22, 18, 1.0));
	auto path = owned (context->createGraphicsPath ());
	if (path)
	{
		path->addEllipse (cap);
		context->drawGraphicsPath (path, CDrawContext::kPathFilled);
	}

	// Mode label, printed on the face like a real meter's own legend.
	static const char* const kModeLabel[3] = {"dB GR", "IN", "OUT"};
	CRect modeR (face.left, face.top + 6, face.right, face.top + 20);
	mac::drawText (context, kModeLabel[std::clamp (mode, 0, 2)], modeR, kCenterText, fonts.caption,
	              mac::rgba (60, 54, 40, 1.0));

	// Power/status LED above the meter, matching the reference's small
	// round indicator -- decorative, not bound to a parameter.
	CRect led (face.getCenter ().x - 6, face.top - 26, face.getCenter ().x + 6, face.top - 14);
	context->setFillColor (t.accent);
	auto ledPath = owned (context->createGraphicsPath ());
	if (ledPath)
	{
		ledPath->addEllipse (led);
		context->drawGraphicsPath (ledPath, CDrawContext::kPathFilled);
	}
}

//------------------------------------------------------------------------
// The settings overlay is shared with Glass -- see the class comment in
// skin_hardware.h for why this delegates rather than re-implementing the
// same geometry-heavy dialog a second time.
//------------------------------------------------------------------------
void HardwareSkin::paintSettingsOverlay (CDrawContext* context, const CRect& view,
                                         const SkinContext& sc) const
{
	static const GlassSkin glass;
	glass.paintSettingsOverlay (context, view, sc);
}

} // namespace Jaxson
