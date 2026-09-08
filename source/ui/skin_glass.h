//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// GlassSkin -- the macOS 27 "Liquid Glass" rendering of the current
// slider/card layout, ported out of RootView's old draw* methods without
// changing a single pixel. See skin.h for what a skin is and is not
// responsible for.
//------------------------------------------------------------------------

#pragma once

#include "skin.h"

namespace Jaxson {

//------------------------------------------------------------------------
class GlassSkin : public ISkin
{
public:
	SkinId id () const override { return SkinId::Glass; }
	const char* name () const override { return "Glass"; }

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
	void paintSettingsOverlay (VSTGUI::CDrawContext* context, const VSTGUI::CRect& view,
	                           const SkinContext& sc) const override;

private:
	void paintSegmented (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintSlider (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintSwitch (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
	void paintPill (VSTGUI::CDrawContext*, const Widget&, const SkinContext&) const;
};

} // namespace Jaxson
