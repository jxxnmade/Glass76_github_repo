//------------------------------------------------------------------------
// Copyright (c) 2026 Jaxson
//
// Drawing primitives for the macOS 27 look: continuous (squircle) corners,
// the Liquid Glass edge stack, layered soft shadows, and text helpers.
//------------------------------------------------------------------------

#pragma once

#include "theme.h"
#include "vstgui/vstgui.h"

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
    read as having thickness rather than as a bright-bordered rectangle. */
void drawGlassPanel (CDrawContext* context, const CRect& rect, CCoord radius,
                     const Theme& theme, bool withShadow = true);

//------------------------------------------------------------------------
// Text
//------------------------------------------------------------------------

void drawText (CDrawContext* context, UTF8StringPtr text, const CRect& rect,
               CHoriTxtAlign align, const CFontRef font, const CColor& color);

CCoord textWidth (CDrawContext* context, UTF8StringPtr text, const CFontRef font);

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

//------------------------------------------------------------------------
} // namespace mac
} // namespace Jaxson
