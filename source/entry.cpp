//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//------------------------------------------------------------------------

#include "processor.h"
#include "controller.h"
#include "cids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "Glass76"

using namespace Steinberg::Vst;
using namespace Jaxson;

//------------------------------------------------------------------------
//  VST 3 Plug-in entry point
//------------------------------------------------------------------------
BEGIN_FACTORY_DEF ("jxxnmade",
                   "https://example.com",
                   "mailto:info@example.com")

	//--- the audio processor -------------------------------------------
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kGlass76ProcessorUID),
	            PClassInfo::kManyInstances,
	            kVstAudioEffectClass,       // do not change
	            stringPluginName,           // the name FL Studio lists
	            Vst::kDistributable,
	            Glass76VST3Category,      // subcategory, from cids.h
	            FULL_VERSION_STR,
	            kVstVersionString,          // do not change
	            Glass76Processor::createInstance)

	//--- the edit controller -------------------------------------------
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kGlass76ControllerUID),
	            PClassInfo::kManyInstances,
	            kVstComponentControllerClass, // do not change
	            stringPluginName "Controller",
	            0,
	            "",
	            FULL_VERSION_STR,
	            kVstVersionString,
	            Glass76Controller::createInstance)

END_FACTORY
