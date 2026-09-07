//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Jaxson {

//------------------------------------------------------------------------
// Class IDs. NEVER change these once the plug-in has shipped or been loaded
// in a saved project -- the host stores them and will not find the plug-in
// again if they change.
//------------------------------------------------------------------------
static const Steinberg::FUID kGlass76ProcessorUID (0x0C643C49, 0x5D4D4863, 0xAB1B3F9A, 0xE1C54AFE);
static const Steinberg::FUID kGlass76ControllerUID (0x629726BF, 0x66854F4C, 0x94FBFD84, 0x3840DEC4);

// Subcategory string reported to the host. See PlugType in
// pluginterfaces/vst/ivstaudioprocessor.h for the full list.
#define Glass76VST3Category "Fx|Dynamics"

//------------------------------------------------------------------------
} // namespace Jaxson
