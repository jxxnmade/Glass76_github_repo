//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// Widget -- the unified widget model.
//
// Until now RootView kept six separate vectors, one per control shape
// (cards, static text, segmented controls, sliders, switches, pills).
// Painting, hit testing and per-frame animation each walked whichever
// subset of those six they cared about, in their own order. That worked
// for one hand-built layout, but a skin system needs one ordered list
// every skin can walk the same way -- so the six collapse into one Widget
// vector here, tagged by WidgetKind.
//
// A Widget carries every field any shape might need; which ones are
// meaningful depends on `kind` (documented per-field below), the same way
// the six original structs each only used the fields that shape needed.
// Order in the vector is construction order, exactly as the old six
// vectors were in construction order -- painting and hit testing filter it
// by kind rather than relying on position, since the shapes still paint
// and hit-test in a specific per-kind priority (see editor.cpp), not
// simply front-to-back.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/crect.h"
#include "vstgui/lib/cdrawdefs.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Jaxson {

//------------------------------------------------------------------------
enum class WidgetKind : uint8_t
{
	Card,        // group-box background panel. `text` is its title.
	Label,       // static text. `text`, `align`, `style` (0 section title
	             // / 1 form label / 2 caption).
	Segmented,   // horizontal button row, one of N selected. `labels`,
	             // `capsule`, `step`, `shownStep`.
	Slider,      // continuous or stepped horizontal track. `detents`,
	             // `snap`, `bipolar`, `norm`, `defaultNorm`, `shownNorm`.
	Switch,      // two-state toggle, sliding-capsule style. `on`, `shownOn`.
	Pill,        // two-state toggle, labelled-capsule style. `text` is its
	             // caption. `on`, `shownOn`.
};

/** Widgets with no parameter ID (Card, Label) leave `id` at this value. */
constexpr Steinberg::Vst::ParamID kNoParam = static_cast<Steinberg::Vst::ParamID> (-1);

//------------------------------------------------------------------------
struct Widget
{
	WidgetKind kind {WidgetKind::Label};
	VSTGUI::CRect r;                              // design-space rect
	Steinberg::Vst::ParamID id {kNoParam};

	//--- Card (title) / Label (its own text) / Pill (its caption) --------
	std::string text;
	VSTGUI::CHoriTxtAlign align {VSTGUI::kLeftText};   // Label only
	int style {0};                                     // Label only

	//--- Segmented ----------------------------------------------------------
	std::vector<std::string> labels;
	bool capsule {true};
	int step {0};
	double shownStep {0.0};   // eased toward step each timer tick

	//--- Slider -----------------------------------------------------------
	int detents {0};           // tick marks under the track, 0 = none
	bool snap {false};         // stop only on the ticks
	bool bipolar {false};      // fill from the centre instead of the left
	double norm {0.0};
	double defaultNorm {0.0};
	double shownNorm {0.0};    // eased toward norm each timer tick

	//--- Switch / Pill ------------------------------------------------------
	bool on {false};
	double shownOn {0.0};      // eased toward on ? 1 : 0 each timer tick
};

} // namespace Jaxson
