//------------------------------------------------------------------------
// Throwaway visual-verification tool, not part of the shipped plug-in.
//
// Renders RootView offscreen (no host, no window) and dumps the pixels as
// a raw BGRA file so a small PowerShell script can turn it into a PNG for
// human inspection. Built only when GLASS76_BUILD_UI_SNAPSHOT is ON.
//
// Usage: glass76_ui_snapshot.exe <outDir> [backgroundImagePath]
//   Writes <outDir>\snapshot_dark.raw (with the background image applied,
//   if one was given) and <outDir>\snapshot_light.raw (always the default
//   beige theme, no image). Each .raw is `uint32 width, uint32 height`
//   followed by width*height BGRA8 bytes, top-down.
//------------------------------------------------------------------------

#include "../source/ui/editor.h"
#include "../source/ui/macdraw.h"
#include "../source/ui/theme.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/win32/win32factory.h"
#include "vstgui/lib/vstguiinit.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include <objbase.h>
#include <windows.h>

using namespace VSTGUI;
using namespace Jaxson;

// sdk.lib's moduleinit.obj expects the plug-in's DLL entry point (normally
// public.sdk/source/main/dllmain.cpp) to define this. This tool has no DLL
// entry point, so it provides the symbol directly.
void* moduleHandle = nullptr;

namespace {

bool dumpBGRA (CBitmap* bitmap, const char* path)
{
	if (!bitmap)
		return false;
	auto access = owned (CBitmapPixelAccess::create (bitmap));
	if (!access)
		return false;

	const uint32_t w = access->getBitmapWidth ();
	const uint32_t h = access->getBitmapHeight ();

	FILE* f = nullptr;
	if (fopen_s (&f, path, "wb") != 0 || !f)
		return false;

	std::fwrite (&w, sizeof (uint32_t), 1, f);
	std::fwrite (&h, sizeof (uint32_t), 1, f);

	CColor c;
	for (uint32_t y = 0; y < h; y++)
	{
		for (uint32_t x = 0; x < w; x++)
		{
			access->setPosition (x, y);
			access->getColor (c);
			const uint8_t px[4] = {c.blue, c.green, c.red, c.alpha};
			std::fwrite (px, 1, 4, f);
		}
	}
	std::fclose (f);
	return true;
}

bool renderAndDump (bool dark, const std::string& path, const std::string& bgImagePath)
{
	auto offscreen = COffscreenContext::create (
	    CPoint (RootView::kPanelWidth, RootView::kPanelHeight), 1.0);
	if (!offscreen)
	{
		std::printf ("could not create offscreen context\n");
		return false;
	}

	// SkinId::Glass: the only content that exists to snapshot. The view is
	// constructed at exactly its design size, so RootView's design-space
	// fit transform is the identity here -- this stays the byte-identical
	// safety net stage 2 set up.
	RootView view (nullptr, SkinId::Glass, CRect (0, 0, RootView::kPanelWidth, RootView::kPanelHeight));
	view.setAppearance (dark ? 1 : 0);
	if (!bgImagePath.empty ())
		view.setBackgroundImagePath (bgImagePath);

	offscreen->beginDraw ();
	offscreen->setDrawMode (kAntiAliasing);
	view.draw (offscreen);
	offscreen->endDraw ();

	auto bitmap = offscreen->getBitmap ();
	if (!dumpBGRA (bitmap, path.c_str ()))
	{
		std::printf ("dump failed for %s\n", path.c_str ());
		return false;
	}
	std::printf ("wrote %s\n", path.c_str ());
	return true;
}

} // anonymous namespace

int main (int argc, char** argv)
{
	// The D2D offscreen backend creates its render target through WIC, which
	// needs COM initialised -- a real host already has this done for other
	// reasons, so nothing in VSTGUI does it for a standalone tool like this.
	CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);

	VSTGUI::init (GetModuleHandle (nullptr));

	// A real plug-in picks this up from setupVSTGUIBundleSupport(), which the
	// host only calls when a VSTGUIEditor actually opens inside a loaded VST3
	// bundle -- this standalone tool never does either, so without this call
	// the private DirectWrite collection is never populated and every font
	// silently falls back to a system substitute (Segoe UI / Segoe Script)
	// instead of the bundled Inter/Inter Display/Allura. GLASS76_RESOURCE_DIR
	// is resource/, the same folder CMake copies into Contents/Resources.
	if (auto* win32 = VSTGUI::getPlatformFactory ().asWin32Factory ())
		win32->setResourceBasePath (GLASS76_RESOURCE_DIR);

	std::string outDir = (argc > 1) ? argv[1] : ".";
	std::string bgImagePath = (argc > 2) ? argv[2] : "";

	bool ok = true;
	ok &= renderAndDump (true, outDir + "\\snapshot_dark.raw", bgImagePath);
	ok &= renderAndDump (false, outDir + "\\snapshot_light.raw", "");

	VSTGUI::exit ();
	return ok ? 0 : 1;
}
