//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// Drawing primitives for the macOS 27 look: continuous (squircle) corners,
// the Liquid Glass edge stack, layered soft shadows, and text helpers.
//------------------------------------------------------------------------

#pragma once

#include "theme.h"
#include "vstgui/vstgui.h"

#include <cstdint>

namespace Jaxson {
namespace mac {

using namespace VSTGUI;

//------------------------------------------------------------------------
// Paths
//------------------------------------------------------------------------

/** A continuous-corner rounded rectangle. macOS 27 applies 0.6 corner
    smoothing system-wide, which is a superellipse, not a circular arc.
    Below ~8px radius the difference is invisible and we fall back to the
    cheaper circular path. */
SharedPointer<CGraphicsPath> squirclePath (CDrawContext* context, const CRect& rect,
                                           CCoord radius);

void fillSquircle (CDrawContext* context, const CRect& rect, CCoord radius,
                   const CColor& color);

void strokeSquircle (CDrawContext* context, const CRect& rect, CCoord radius,
                     const CColor& color, CCoord lineWidth = 1.0);

//------------------------------------------------------------------------
// Depth
//------------------------------------------------------------------------

/** A soft drop shadow built from concentric fading outlines. VSTGUI has no
    blur, so this stands in for one; it is only ever drawn into a cached
    offscreen bitmap, never per frame. */
void drawSoftShadow (CDrawContext* context, const CRect& rect, CCoord radius,
                     const CColor& color, CCoord blur, CCoord offsetY, double alpha);

/** The full Liquid Glass slab: drop shadow, fill, the inner band / tight
    edge / specular / lateral stack, and the containment ring.

    The kit's stack is eight inner shadows; VSTGUI has none, so each layer
    is reproduced as a clipped gradient or hairline. The order matters --
    the dark bands come BEFORE the highlight, which is what makes the slab
    read as having thickness rather than as a bright-bordered rectangle.

    skipFill drops step 2 (the solid glassFill) only -- shadow, edge stack
    and containment ring are unchanged, so the panel still reads as a glass
    slab, just with nothing opaque behind it. Used by the transparent-
    background preference: see GlassSkin::paintBackdrop. */
void drawGlassPanel (CDrawContext* context, const CRect& rect, CCoord radius,
                     const Theme& theme, bool withShadow = true, bool skipFill = false);

//------------------------------------------------------------------------
// Text
//------------------------------------------------------------------------

void drawText (CDrawContext* context, UTF8StringPtr text, const CRect& rect,
               CHoriTxtAlign align, const CFontRef font, const CColor& color);

CCoord textWidth (CDrawContext* context, UTF8StringPtr text, const CFontRef font);

//------------------------------------------------------------------------
// Colour analysis / recolouring
//
// Used to pull an accent colour out of a user-chosen background image
// (see RootView::setBackgroundImagePath) and apply it to the theme without
// disturbing the saturation/value tuning each token already has for
// light/dark contrast -- only the hue changes.
//------------------------------------------------------------------------

/** RGB (0-255 channels) to HSV. Hue in degrees [0,360), saturation and
    value in [0,1]. */
void rgbToHsv (uint8_t r, uint8_t g, uint8_t b, double& h, double& s, double& v);

/** Same saturation and value, new hue (degrees). Alpha is carried through
    unchanged. */
CColor withHue (const CColor& c, double hueDeg);

/** Re-tints the accent family -- the tokens the kit itself scopes to
    "sliders, switches and the gauge arc" -- to a new hue, in place. */
void applyAccentHue (Theme& t, double hueDeg);

//------------------------------------------------------------------------
// Misc
//------------------------------------------------------------------------

/** Concentric radius for a child inset inside a rounded container. The
    height/2 clamp is not optional: without it a short control inside a
    large-radius container gets a radius larger than half its height. */
inline CCoord concentricRadius (CCoord outerRadius, CCoord inset, CCoord height)
{
	const CCoord r = outerRadius - inset;
	const CCoord clamped = r < 0 ? 0 : r;
	return clamped > height / 2.0 ? height / 2.0 : clamped;
}

/** Linear per-channel interpolation, including alpha. Used to cross-fade a
    control's fill/text colour across an animated 0..1 position instead of
    snapping between two fixed colours. */
inline CColor mixColor (const CColor& a, const CColor& b, double f)
{
	f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
	auto lerp = [f] (uint8_t x, uint8_t y) {
		return static_cast<uint8_t> (x + (static_cast<double> (y) - x) * f + 0.5);
	};
	return CColor (lerp (a.red, b.red), lerp (a.green, b.green), lerp (a.blue, b.blue),
	              lerp (a.alpha, b.alpha));
}

//------------------------------------------------------------------------
} // namespace mac
} // namespace Jaxson
