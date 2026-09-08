//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// ISkin -- a skin paints; it owns no state of its own between calls.
//
// RootView keeps owning everything a skin must never touch directly:
// layout, hit testing, animation, the timer, and the chrome bitmap cache.
// A skin is handed a SkinContext (the theme, and an IWidgetHost view onto
// whatever live state it needs to render) and paints into whatever
// CDrawContext it is given. That separation is what lets a second skin
// exist later without RootView caring which one is active beyond calling
// through this interface.
//
// IWidgetHost is RootView's side of that contract: read-only accessors
// for the state a skin cannot get any other way (the live meter reading,
// the settings-overlay geometry, formatted value text, and so on). It is
// deliberately shaped around what the current UI actually needs to paint
// itself, not a speculative general model -- several of these accessors
// (paintValueColumn, paintGauge's arc-specific reading, the settings
// overlay's exact rects) belong to controls this plug-in is going to
// replace with the 1176 faceplate in a later stage, and are expected to
// shrink or disappear then.
//------------------------------------------------------------------------

#pragma once

#include "theme.h"
#include "widget.h"

#include "vstgui/lib/crect.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <string>
#include <vector>

namespace VSTGUI {
class CDrawContext;
class CBitmap;
}

namespace Jaxson {

//------------------------------------------------------------------------
enum class SkinId : int32_t
{
	Hardware = 0,
	Glass = 1,
};

//------------------------------------------------------------------------
class IWidgetHost
{
public:
	virtual ~IWidgetHost () = default;

	//--- background image, for the wallpaper a skin draws behind itself ---
	virtual VSTGUI::CBitmap* backgroundImage () const = 0;   // nullptr if none
	virtual const std::string& backgroundImagePath () const = 0;

	//--- meter --------------------------------------------------------------
	virtual double meterShown () const = 0;   // smoothed 0..1
	virtual double meterPeak () const = 0;    // smoothed 0..1 peak-hold
	virtual double meterGrDb () const = 0;
	virtual double meterInDb () const = 0;
	virtual double meterOutDb () const = 0;
	virtual int meterMode () const = 0;       // kMeterGR / kMeterIn / kMeterOut
	virtual bool autoMakeupOn () const = 0;
	virtual double autoMakeupDb () const = 0;
	virtual const VSTGUI::CRect& gaugeRect () const = 0;

	//--- formatted value text and its readout rects ------------------------
	virtual std::string gainStepText (Steinberg::Vst::ParamID id) const = 0;
	virtual std::string attackText () const = 0;
	virtual std::string releaseText () const = 0;
	virtual std::string ratioText () const = 0;
	virtual std::string mixText () const = 0;
	virtual std::string trimText () const = 0;
	virtual const VSTGUI::CRect& valueRect (Steinberg::Vst::ParamID id) const = 0;

	//--- widget-level state a skin cannot derive on its own ----------------
	virtual bool isDisabled (const Widget&) const = 0;

	//--- toolbar buttons ----------------------------------------------------
	virtual const VSTGUI::CRect& appearanceRect () const = 0;
	virtual const VSTGUI::CRect& settingsButtonRect () const = 0;

	//--- settings overlay ---------------------------------------------------
	virtual bool settingsOpen () const = 0;
	virtual int refreshRateHz () const = 0;
	virtual const VSTGUI::CRect& settingsCardRect () const = 0;
	virtual const VSTGUI::CRect& settingsChooseRect () const = 0;
	virtual const VSTGUI::CRect& settingsClearRect () const = 0;
	virtual const VSTGUI::CRect& settingsCloseRect () const = 0;
	virtual const VSTGUI::CRect& settingsRateRect (int index) const = 0;
	virtual const VSTGUI::CRect& settingsSkinRect () const = 0;
	virtual SkinId currentSkinId () const = 0;
};

//------------------------------------------------------------------------
struct SkinContext
{
	const mac::Theme& theme;
	const IWidgetHost& host;
	bool dark {true};
};

//------------------------------------------------------------------------
class ISkin
{
public:
	virtual ~ISkin () = default;

	virtual SkinId id () const = 0;
	virtual const char* name () const = 0;

	/** The static backdrop -- window background, wallpaper (the user's own
	    image, or the wash-gradient stand-in), toolbar, every Card and every
	    Label widget. Painted once into RootView's cached chrome bitmap, not
	    every frame. */
	virtual void paintBackdrop (VSTGUI::CDrawContext* context, const VSTGUI::CRect& view,
	                            const std::vector<Widget>& widgets, const VSTGUI::CRect& toolbar,
	                            const VSTGUI::CRect& titleRect, const VSTGUI::CRect& subtitleRect,
	                            const SkinContext& sc) const = 0;

	/** Every interactive widget (Segmented, Slider, Switch, Pill), painted
	    live every frame -- these carry per-frame animation state. */
	virtual void paintWidget (VSTGUI::CDrawContext* context, const Widget& widget,
	                          const SkinContext& sc) const = 0;

	virtual void paintAppearanceButton (VSTGUI::CDrawContext* context,
	                                    const SkinContext& sc) const = 0;
	virtual void paintSettingsButton (VSTGUI::CDrawContext* context,
	                                  const SkinContext& sc) const = 0;
	virtual void paintValueColumn (VSTGUI::CDrawContext* context, const SkinContext& sc) const = 0;
	virtual void paintGauge (VSTGUI::CDrawContext* context, const SkinContext& sc) const = 0;
	virtual void paintSettingsOverlay (VSTGUI::CDrawContext* context, const VSTGUI::CRect& view,
	                                   const SkinContext& sc) const = 0;
};

namespace skins {

/** Singletons, no allocation. Hardware currently resolves to the same
    instance as Glass -- there is nothing else to hand back until the
    hardware faceplate skin exists. */
const ISkin& get (SkinId id);
const std::vector<const ISkin*>& all ();

} // namespace skins
} // namespace Jaxson
