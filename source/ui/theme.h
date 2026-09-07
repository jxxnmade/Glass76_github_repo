//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// macOS 27 design tokens.
//
// Every value below is traced to references/macos-27.md in the
// macos-ui-on-windows skill, which is measured from Apple's own macOS 27
// UI kit. Nothing here is carried over from the Aqua tables -- the two
// systems disagree on window background, control sizes, radii, accent
// colour and label alphas, so mixing them is a visible error.
//
// The one place we deliberately depart from the kit is noted inline.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/vstgui.h"

namespace Jaxson {
namespace mac {

using VSTGUI::CColor;
using VSTGUI::CCoord;

//------------------------------------------------------------------------
// Colour construction. macOS text and fill hierarchy is alpha over the
// backdrop, never a set of grey hexes, so everything is built this way.
//------------------------------------------------------------------------
inline CColor rgba (int r, int g, int b, double a)
{
	const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
	return CColor (static_cast<uint8_t> (r), static_cast<uint8_t> (g),
	               static_cast<uint8_t> (b), static_cast<uint8_t> (clamped * 255.0 + 0.5));
}

inline CColor withAlpha (const CColor& c, double a)
{
	CColor out = c;
	const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
	out.alpha = static_cast<uint8_t> (clamped * 255.0 + 0.5);
	return out;
}

//------------------------------------------------------------------------
// Size classes [kit]. Radius is always height / 4; the capsule families
// (and buttons/segmented controls at Lg and XL) use height / 2 instead.
//------------------------------------------------------------------------
static constexpr CCoord kSizeMn = 16;
static constexpr CCoord kSizeSm = 20;
static constexpr CCoord kSizeRg = 24;
static constexpr CCoord kSizeLg = 28;
static constexpr CCoord kSizeXl = 36;

inline CCoord radiusFor (CCoord height) { return height / 4.0; }
inline CCoord capsuleFor (CCoord height) { return height / 2.0; }

/** Corner smoothing is 0.6 across the whole kit. Expressed as a
    superellipse exponent for the path generator in macdraw. */
static constexpr double kCornerSmoothingExponent = 4.2;

//------------------------------------------------------------------------
// Metrics [kit]
//------------------------------------------------------------------------
static constexpr CCoord kUnifiedToolbarHeight = 52;   // control height + 16
static constexpr CCoord kContentInset = 12;           // 12 in 27, not Aqua's 20
static constexpr CCoord kGroupBoxRadius = 12;
static constexpr CCoord kCheckboxLabelGap = 5;
static constexpr CCoord kSwitchWidth = 54;            // 54 x 24, knob 32 x 20 inset 2
static constexpr CCoord kSwitchHeight = 24;
static constexpr CCoord kSwitchKnobWidth = 32;
static constexpr CCoord kSwitchKnobHeight = 20;
static constexpr CCoord kSliderTrackHeight = 6;       // Rg / Lg / XL
static constexpr CCoord kSegmentLabelInset = 6;

//------------------------------------------------------------------------
// The token set. Two instances, light and dark; nothing reads a hex
// directly at a call site.
//------------------------------------------------------------------------
struct Theme
{
	bool dark {false};

	//--- surfaces ---------------------------------------------------
	CColor windowBg;
	CColor washA;        // faint accent bleed, top-left
	CColor washB;        // faint indigo bleed, bottom-right

	//--- labels, six levels [kit] -----------------------------------
	CColor label1;
	CColor label2;
	CColor label3;
	CColor label4;

	//--- fills, five levels [kit] -----------------------------------
	CColor fill1;
	CColor fill2;
	CColor fill3;
	CColor fill4;

	CColor separator;

	//--- accent [kit]: 27 blue, not Aqua's #007AFF ------------------
	CColor accent;
	CColor accentPressed;
	CColor accentGlyph;

	//--- Liquid Glass edge stack [kit] ------------------------------
	CColor glassFill;      // the two kit fills composited
	CColor glassBand;      // dark inner bands, top and bottom
	CColor glassTight;     // tight inner edges
	CColor glassSpecular;  // the bright hairline 2px down from the top
	CColor glassRing;      // 0.5px containment ring
	CColor glassLateral;   // left / right inner edge light
	CColor glassShadow;    // outer drop shadow

	//--- over-glass control fills [kit] -----------------------------
	// Children of a glass container use their own, higher-alpha fill set.
	// Using the content-area values on glass is a visible error.
	CColor overGlassIdle;
	CColor overGlassClicked;
	CColor overGlassDisabled;

	//--- chips, knobs, wells ----------------------------------------
	CColor chipFill;       // selected segment / slider knob
	CColor chipRing;
	CColor chipShadow;
	CColor wellFill;       // recessed track
	CColor focusRingOuter; // 3.5px at alpha 0.25 [kit]
	CColor focusRingInner; // 1px at alpha 0.15 [kit]
};

//------------------------------------------------------------------------
inline Theme makeLightTheme ()
{
	Theme t;
	t.dark = false;

	// Creme, not pure white -- the kit's #FFFFFF reads as cold and clinical
	// next to a warm beige accent, so the whole surface is warmed with it.
	t.windowBg = rgba (250, 246, 237, 1.0);
	// A plug-in window has no desktop behind it, so the glass has nothing
	// to sample. These two very faint washes stand in for the wallpaper
	// bleed a real macOS 27 window picks up -- without them the glass
	// panels have no backdrop to separate themselves from. Warm gold/beige
	// instead of the kit's blue/indigo, to match the accent below.
	t.washA = rgba (196, 154, 91, 0.10);
	t.washB = rgba (214, 178, 128, 0.08);

	t.label1 = rgba (0, 0, 0, 0.85);
	t.label2 = rgba (0, 0, 0, 0.50);
	t.label3 = rgba (0, 0, 0, 0.25);
	t.label4 = rgba (0, 0, 0, 0.10);

	t.fill1 = rgba (0, 0, 0, 0.10);
	t.fill2 = rgba (0, 0, 0, 0.08);
	t.fill3 = rgba (0, 0, 0, 0.05);
	t.fill4 = rgba (0, 0, 0, 0.03);

	t.separator = rgba (60, 60, 67, 0.29);

	// Warm beige/tan accent -- sliders, switches and the gauge arc -- in
	// place of the kit's cold #0088FF.
	t.accent = rgba (181, 136, 74, 1.0);
	t.accentPressed = rgba (153, 112, 58, 1.0);
	t.accentGlyph = rgba (255, 255, 255, 1.0);

	// Kit: fill rgba(255,255,255,0.7) over rgba(191,191,191,0.1). Tinted
	// creme rather than neutral white to match the warmer surface.
	t.glassFill = rgba (250, 247, 239, 0.72);
	t.glassBand = rgba (39, 39, 39, 1.0);
	t.glassTight = rgba (39, 39, 39, 1.0);
	t.glassSpecular = rgba (255, 255, 255, 1.0);
	t.glassRing = rgba (219, 219, 219, 0.5);
	t.glassLateral = rgba (219, 219, 219, 1.0);
	t.glassShadow = rgba (0, 0, 0, 1.0);

	t.overGlassIdle = rgba (0, 0, 0, 0.12);
	t.overGlassClicked = rgba (0, 0, 0, 0.20);
	t.overGlassDisabled = rgba (0, 0, 0, 0.05);

	t.chipFill = rgba (250, 246, 236, 0.95);
	t.chipRing = rgba (0, 0, 0, 0.06);
	t.chipShadow = rgba (0, 0, 0, 1.0);
	t.wellFill = rgba (0, 0, 0, 0.10);

	t.focusRingOuter = rgba (181, 136, 74, 0.25);
	t.focusRingInner = rgba (181, 136, 74, 0.15);

	return t;
}

//------------------------------------------------------------------------
inline Theme makeDarkTheme ()
{
	Theme t;
	t.dark = true;

	// A warm near-black rather than the kit's neutral #1E1E1E, so the whole
	// panel leans beige instead of cold grey. This is the default appearance.
	t.windowBg = rgba (30, 28, 25, 1.0);
	t.washA = rgba (214, 178, 128, 0.16);
	t.washB = rgba (181, 140, 90, 0.12);

	// Dark primary is solid white in 27, not 85% [kit].
	t.label1 = rgba (255, 255, 255, 1.0);
	t.label2 = rgba (255, 255, 255, 0.55);
	t.label3 = rgba (255, 255, 255, 0.25);
	t.label4 = rgba (255, 255, 255, 0.10);

	t.fill1 = rgba (255, 255, 255, 0.10);
	t.fill2 = rgba (255, 255, 255, 0.08);
	t.fill3 = rgba (255, 255, 255, 0.05);
	t.fill4 = rgba (255, 255, 255, 0.03);

	// The kit carries one separator value for both appearances, which is
	// nearly invisible on a #1E1E1E ground. Fill 4 is the closest token
	// that actually reads, so dark uses that instead.
	t.separator = rgba (255, 255, 255, 0.12);

	// Warm beige/tan accent -- sliders, switches and the gauge arc -- in
	// place of the kit's cold #0091FF.
	t.accent = rgba (214, 178, 128, 1.0);
	t.accentPressed = rgba (190, 155, 108, 1.0);
	t.accentGlyph = rgba (255, 255, 255, 1.0);

	t.glassFill = rgba (28, 26, 23, 0.55);
	t.glassBand = rgba (52, 52, 52, 1.0);
	t.glassTight = rgba (103, 103, 103, 1.0);
	t.glassSpecular = rgba (255, 255, 255, 0.2);
	t.glassRing = rgba (166, 166, 166, 0.8);
	t.glassLateral = rgba (166, 166, 166, 1.0);
	t.glassShadow = rgba (0, 0, 0, 1.0);

	t.overGlassIdle = rgba (255, 255, 255, 0.14);
	t.overGlassClicked = rgba (255, 255, 255, 0.22);
	t.overGlassDisabled = rgba (255, 255, 255, 0.05);

	t.chipFill = rgba (112, 106, 98, 0.95);
	t.chipRing = rgba (255, 255, 255, 0.10);
	t.chipShadow = rgba (0, 0, 0, 1.0);
	t.wellFill = rgba (0, 0, 0, 0.30);

	t.focusRingOuter = rgba (214, 178, 128, 0.25);
	t.focusRingInner = rgba (214, 178, 128, 0.15);

	return t;
}

//------------------------------------------------------------------------
// Typography [kit]. Sizes and leadings match the Aqua ramp; the weights
// do not. 13pt is the default for everything interactive.
//
// SF Pro is licensed for Apple platforms only, so it cannot ship in a
// Windows binary. Fonts::resolve() walks a substitute chain at start-up
// and reports what it actually got -- a font that silently falls back to
// Segoe UI breaks every metric while nothing errors.
//------------------------------------------------------------------------
struct Fonts
{
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> largeTitle;  // 26 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> title1;      // 22 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> title3;      // 15 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> headline;    // 13 bold
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> body;        // 13 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> bodyEmph;    // 13 emphasized
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> callout;     // 12 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> subhead;     // 11 regular
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> subheadEmph; // 11 emphasized
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> caption;     // 10 regular

	/** The cursive wordmark for "Glass76 Signature". Allura (SIL OFL),
	    bundled in resource/Fonts the same way Inter is -- see macdraw.cpp
	    for the substitute chain used if it somehow fails to load. */
	VSTGUI::SharedPointer<VSTGUI::CFontDesc> signature;      // 17, model switch

	/** What actually resolved, for the start-up check. A font that fails
	    to load and silently falls back breaks every metric while nothing
	    errors, so the result is recorded rather than assumed. */
	VSTGUI::UTF8String family;         // text optical size
	VSTGUI::UTF8String displayFamily;  // >= 20px optical size
	bool usingInter {false};

	static const Fonts& get ();
};

//------------------------------------------------------------------------
} // namespace mac
} // namespace Jaxson
