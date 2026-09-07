//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#include "macdraw.h"

#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/win32/win32factory.h"

#include <cmath>
#include <string>
#include <vector>

namespace Jaxson {
namespace mac {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kCornerSegments = 14;

/** One quarter of a superellipse, |x|^n + |y|^n = 1, sampled as a
    polyline. At 14 segments over a 12px corner every segment is well
    under a pixel, so it renders as a smooth continuous curve. */
void addSuperellipseCorner (CGraphicsPath& path, CCoord cx, CCoord cy, CCoord rx, CCoord ry,
                            double startAngle, double n, bool firstPoint)
{
	const double e = 2.0 / n;
	for (int i = 0; i <= kCornerSegments; i++)
	{
		const double t = startAngle + (kPi * 0.5) * (static_cast<double> (i) / kCornerSegments);
		const double c = std::cos (t);
		const double s = std::sin (t);
		const double x = cx + rx * (c < 0 ? -std::pow (-c, e) : std::pow (c, e));
		const double y = cy + ry * (s < 0 ? -std::pow (-s, e) : std::pow (s, e));
		if (firstPoint && i == 0)
			path.beginSubpath (CPoint (x, y));
		else
			path.addLine (CPoint (x, y));
	}
}

} // anonymous namespace

//------------------------------------------------------------------------
SharedPointer<CGraphicsPath> squirclePath (CDrawContext* context, const CRect& rect, CCoord radius)
{
	CCoord r = radius;
	const CCoord maxR = std::min (rect.getWidth (), rect.getHeight ()) * 0.5;
	if (r > maxR)
		r = maxR;
	if (r < 0)
		r = 0;

	// Two cases take the plain circular path:
	//
	//  - below 8px, where the superellipse and the arc are visually
	//    identical and the built-in path is cheaper and better hinted;
	//  - capsules, where the radius is already half the short side. Corner
	//    smoothing does not apply to the capsule families (search fields,
	//    switches, sliders, Lg/XL buttons and segmented controls) -- their
	//    ends are true semicircles, and running a superellipse through them
	//    produces a visibly squared-off pill.
	if (r < 8.0 || r >= maxR - 0.01)
	{
		auto* p = context->createRoundRectGraphicsPath (rect, r);
		return owned (p);
	}

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return nullptr;

	const double n = kCornerSmoothingExponent;
	const CCoord l = rect.left, t = rect.top, rr = rect.right, b = rect.bottom;

	// Corners, clockwise from the top-left, each as a quarter superellipse.
	addSuperellipseCorner (*path, l + r, t + r, r, r, kPi, n, true);          // TL
	path->addLine (CPoint (rr - r, t));
	addSuperellipseCorner (*path, rr - r, t + r, r, r, -kPi * 0.5, n, false); // TR
	path->addLine (CPoint (rr, b - r));
	addSuperellipseCorner (*path, rr - r, b - r, r, r, 0.0, n, false);        // BR
	path->addLine (CPoint (l + r, b));
	addSuperellipseCorner (*path, l + r, b - r, r, r, kPi * 0.5, n, false);   // BL
	path->closeSubpath ();

	return path;
}

//------------------------------------------------------------------------
void fillSquircle (CDrawContext* context, const CRect& rect, CCoord radius, const CColor& color)
{
	if (rect.getWidth () <= 0 || rect.getHeight () <= 0 || color.alpha == 0)
		return;
	auto path = squirclePath (context, rect, radius);
	if (!path)
		return;
	context->setFillColor (color);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);
}

//------------------------------------------------------------------------
void strokeSquircle (CDrawContext* context, const CRect& rect, CCoord radius,
                     const CColor& color, CCoord lineWidth)
{
	if (rect.getWidth () <= 0 || rect.getHeight () <= 0 || color.alpha == 0)
		return;
	// Inset by half the line width so the stroke lands inside the shape,
	// the way an inner containment ring does.
	CRect r (rect);
	r.inset (lineWidth * 0.5, lineWidth * 0.5);
	auto path = squirclePath (context, r, radius - lineWidth * 0.5);
	if (!path)
		return;
	context->setFrameColor (color);
	context->setLineWidth (lineWidth);
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

//------------------------------------------------------------------------
void drawSoftShadow (CDrawContext* context, const CRect& rect, CCoord radius,
                     const CColor& color, CCoord blur, CCoord offsetY, double alpha)
{
	if (blur <= 0 || alpha <= 0)
		return;

	// A gaussian blur approximated by stacked outlines: each ring adds a
	// little alpha, and the total falls off with the square of the distance.
	const int steps = static_cast<int> (std::min (blur, static_cast<CCoord> (18)));
	if (steps <= 0)
		return;

	for (int i = steps; i >= 1; i--)
	{
		const double f = static_cast<double> (i) / steps;   // 1 at the outside
		const CCoord grow = blur * f;
		CRect r (rect);
		r.inset (-grow, -grow);
		r.offset (0, offsetY);
		// Quadratic falloff, normalised so the stack sums to `alpha`.
		const double layerAlpha = alpha * (1.0 - f) * (1.0 - f) * 2.4 / steps;
		fillSquircle (context, r, radius + grow, withAlpha (color, layerAlpha));
	}
}

//------------------------------------------------------------------------
void drawGlassPanel (CDrawContext* context, const CRect& rect, CCoord radius,
                     const Theme& theme, bool withShadow)
{
	if (rect.getWidth () <= 2 || rect.getHeight () <= 2)
		return;

	//--- 1. outer drop shadow ---------------------------------------
	// The kit's floating-panel shadow is 0 18px 46px at 0.25. These cards
	// sit close to their backdrop rather than over a wallpaper, so the
	// shadow is scaled to match that distance.
	if (withShadow)
	{
		drawSoftShadow (context, rect, radius, theme.glassShadow, 10.0, 3.0,
		                theme.dark ? 0.40 : 0.13);
	}

	auto path = squirclePath (context, rect, radius);
	if (!path)
		return;

	//--- 2. the fill ------------------------------------------------
	context->setFillColor (theme.glassFill);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);

	//--- 3. the inner edge stack ------------------------------------
	// Order is the kit's: dark bands first, THEN the highlight. That order
	// is what makes the slab read as having thickness; a single bright top
	// border does not produce the effect.
	//
	// Each layer is a gradient filled through the panel's own path, so the
	// continuous corners shape it. VSTGUI's clip is rectangular, so a
	// clipped rect would show square corners at the top of the band.
	const CCoord bandDepth = std::min<CCoord> (8.0, rect.getHeight () * 0.25);
	const double bandAlpha = theme.dark ? 0.22 : 0.10;

	// 3a. dark band, top inner edge
	{
		auto grad = owned (CGradient::create (0.0, 1.0, withAlpha (theme.glassBand, bandAlpha),
		                                      withAlpha (theme.glassBand, 0.0)));
		if (grad)
			context->fillLinearGradient (path, *grad, CPoint (rect.left, rect.top),
			                             CPoint (rect.left, rect.top + bandDepth), false);
	}
	// 3b. dark band, bottom inner edge
	{
		auto grad = owned (CGradient::create (0.0, 1.0, withAlpha (theme.glassBand, 0.0),
		                                      withAlpha (theme.glassBand, bandAlpha)));
		if (grad)
			context->fillLinearGradient (path, *grad, CPoint (rect.left, rect.bottom - bandDepth),
			                             CPoint (rect.left, rect.bottom), false);
	}

	// 3c. lateral edge light -- separate shadows with x offsets in the kit.
	// This is what makes the slab read as glass instead of frosted plastic.
	const CCoord lateralWidth = 2.5;
	const double lateralAlpha = theme.dark ? 0.45 : 0.60;
	{
		auto grad = owned (CGradient::create (0.0, 1.0,
		                                      withAlpha (theme.glassLateral, lateralAlpha),
		                                      withAlpha (theme.glassLateral, 0.0)));
		if (grad)
			context->fillLinearGradient (path, *grad, CPoint (rect.left, rect.top),
			                             CPoint (rect.left + lateralWidth, rect.top), false);
	}
	{
		auto grad = owned (CGradient::create (0.0, 1.0,
		                                      withAlpha (theme.glassLateral, 0.0),
		                                      withAlpha (theme.glassLateral, lateralAlpha)));
		if (grad)
			context->fillLinearGradient (path, *grad, CPoint (rect.right - lateralWidth, rect.top),
			                             CPoint (rect.right, rect.top), false);
	}

	// 3d. the specular highlight: one bright hairline two pixels down.
	{
		const CCoord inset = radius * 0.9;
		CRect line (rect.left + inset, rect.top + 2.0, rect.right - inset, rect.top + 3.0);
		if (line.getWidth () > 0)
		{
			context->setFillColor (withAlpha (theme.glassSpecular, theme.dark ? 0.20 : 0.85));
			context->drawRect (line, kDrawFilled);
		}
	}

	// 3e. tight inner edges, top and bottom
	{
		context->setFillColor (withAlpha (theme.glassTight, theme.dark ? 0.30 : 0.10));
		CRect top (rect.left + radius * 0.6, rect.top, rect.right - radius * 0.6, rect.top + 1.0);
		context->drawRect (top, kDrawFilled);
		CRect bottom (rect.left + radius * 0.6, rect.bottom - 1.0, rect.right - radius * 0.6,
		              rect.bottom);
		context->drawRect (bottom, kDrawFilled);
	}

	//--- 4. containment ring ----------------------------------------
	strokeSquircle (context, rect, radius, theme.glassRing, 1.0);
}

//------------------------------------------------------------------------
void drawText (CDrawContext* context, UTF8StringPtr text, const CRect& rect,
               CHoriTxtAlign align, const CFontRef font, const CColor& color)
{
	if (!text || !font)
		return;
	context->setFont (font);
	context->setFontColor (color);
	context->drawString (text, rect, align, true);
}

//------------------------------------------------------------------------
CCoord textWidth (CDrawContext* context, UTF8StringPtr text, const CFontRef font)
{
	if (!text || !font)
		return 0;
	context->setFont (font);
	return context->getStringWidth (text);
}

//------------------------------------------------------------------------
// Font resolution.
//
// SF Pro cannot ship in a Windows binary, so we walk a substitute chain.
// Inter is the closest match and is picked up automatically if it is
// installed, or dropped into the bundle's Contents/Resources/Fonts folder
// -- VSTGUI scans that directory at start-up. Segoe UI Variable Text is
// the fallback that is always present on Windows 11.
//------------------------------------------------------------------------
const Fonts& Fonts::get ()
{
	static Fonts fonts = [] () {
		// Work around an upstream VSTGUI bug that silently disables bundled
		// fonts in every VST3 plug-in.
		//
		// setupVSTGUIBundleSupport() hands Win32Factory::setResourceBasePath()
		// a path with no trailing separator ("...\Contents\Resources").
		// setBasePath() normalises it for resource loading, but the raw string
		// is what gets passed on to D2DFont::initialize(), which appends
		// "Fonts\\*" -- producing "...\Contents\ResourcesFonts\*". That
		// directory does not exist, the scan returns nothing, and the custom
		// font collection ends up empty with no error anywhere.
		//
		// Reading the path back gives the normalised form (with the trailing
		// separator) and setting it again re-runs the font scan against the
		// correct directory. Harmless if the bug is ever fixed upstream.
		if (auto* win32 = getPlatformFactory ().asWin32Factory ())
		{
			if (auto base = win32->getResourceBasePath ())
				win32->setResourceBasePath (*base);
		}

		std::vector<std::string> available;
		getPlatformFactory ().getAllFontFamilies ([&] (const std::string& name) {
			available.push_back (name);
			return true;
		});

		auto has = [&available] (const char* name) {
			for (const auto& f : available)
			{
				if (f == name)
					return true;
			}
			return false;
		};

		auto firstAvailable = [&has] (std::initializer_list<const char*> chain) {
			for (const char* candidate : chain)
			{
				if (has (candidate))
					return candidate;
			}
			return "Segoe UI";   // always present on Windows
		};

		// SF ships as two optical sizes and so does Inter. Display is drawn
		// tighter and is meant for 20px and up; using it for a 10px caption
		// (or Text for a 26px title) is the wrong half of the family.
		const char* textFamily = firstAvailable (
		    {"Inter", "SF Pro Text", "Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI"});
		const char* displayFamily = firstAvailable (
		    {"Inter Display", "Inter", "SF Pro Display", "Segoe UI Variable Display",
		     "Segoe UI Variable", "Segoe UI"});
		// A genuine script face for the "Glass76 Signature" wordmark. These
		// are Microsoft/foundry-licensed system fonts, not something this
		// plug-in can bundle the way Inter is bundled, so it is a pure
		// substitute chain -- Segoe Script ships with Windows itself and is
		// the most likely to actually be present.
		const char* signatureFamily = firstAvailable (
		    {"Segoe Script", "Brush Script MT", "Lucida Handwriting",
		     "Monotype Corsiva", "Segoe Print"});

		Fonts f;
		f.family = textFamily;
		f.displayFamily = displayFamily;
		f.usingInter = (std::string (textFamily).rfind ("Inter", 0) == 0);

		f.largeTitle = owned (new CFontDesc (displayFamily, 26, kNormalFace));
		f.title1 = owned (new CFontDesc (displayFamily, 22, kNormalFace));
		f.title3 = owned (new CFontDesc (textFamily, 15, kNormalFace));
		f.headline = owned (new CFontDesc (textFamily, 13, kBoldFace));
		f.body = owned (new CFontDesc (textFamily, 13, kNormalFace));
		f.bodyEmph = owned (new CFontDesc (textFamily, 13, kBoldFace));
		f.callout = owned (new CFontDesc (textFamily, 12, kNormalFace));
		f.subhead = owned (new CFontDesc (textFamily, 11, kNormalFace));
		f.subheadEmph = owned (new CFontDesc (textFamily, 11, kBoldFace));
		f.caption = owned (new CFontDesc (textFamily, 10, kNormalFace));
		f.signature = owned (new CFontDesc (signatureFamily, 17, kNormalFace));
		f.signatureSmall = owned (new CFontDesc (signatureFamily, 15, kNormalFace));
		return f;
	}();

	return fonts;
}

//------------------------------------------------------------------------
} // namespace mac
} // namespace Jaxson
