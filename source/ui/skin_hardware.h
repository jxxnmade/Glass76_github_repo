//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// HardwareSkin -- stage 5's 1176-style faceplate: a single continuous
// brushed-metal panel, chicken-head knobs for the continuous parameters,
// vertical button stacks for Ratio and Meter select, and an analog VU
// meter. Adapted from the classic 1176/CLA-76 hardware layout -- the knob/
// meter/ratio-stack arrangement is generic vintage-compressor language, not
// anything belonging to a specific plug-in vendor -- and branded as
// Glass76, not copying any other product's chrome, logo or names.
//
// The settings overlay is intentionally NOT reskinned here: it is a modal
// utility dialog, not part of the faceplate, so both skins share GlassSkin's
// implementation of it (see paintSettingsOverlay below) rather than paying
// for a second copy of that geometry-heavy code for a dialog most users see
// rarely and which looks fine regardless of which faceplate is underneath
// it.
//------------------------------------------------------------------------

#pragma once

#include "skin.h"

namespace Jaxson {

//------------------------------------------------------------------------
class HardwareSkin : public ISkin
{
public:
	SkinId id () const override { return SkinId::Hardware; }
	const char* name () const override { return "Hardware"; }

	void paintBackdrop (VSTGUI::CDrawContext* context, const VSTGUI::CRect& view,
	                    const std::vector<Widget>& widgets, const VSTGUI::CRect& toolbar,
	                    const VSTGUI::CRect& titleRect, const VSTGUI::CRect& subtitleRect,
	                    const SkinContext& sc) const override;

	void paintWidget (VSTGUI::CDrawContext* context, const Widget& widget,
	                  const SkinContext& sc) const override;

	void paintAppearanceButton (VSTGUI::CDrawContext* context,
	                            const SkinContext& sc) const override;
	void paintSettingsButton (VSTGUI::CDrawContext* context, const SkinContext& sc) const override;
	void paintValueColumn (VSTGUI::CDrawContext* context, const SkinContext& sc) const override;
	void paintGauge (VSTGUI::CDrawContext* context, const SkinContext& sc) const override;

	/** Delegates to GlassSkin -- see the class comment above. */
	void paintSettingsOverlay (VSTGUI::CDrawContext* context, const VSTGUI::CRect& view,
	                           const SkinContext& sc) const override;

private:
	void paintKnob (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintSegmented (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintSwitch (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintPill (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
};

} // namespace Jaxson
