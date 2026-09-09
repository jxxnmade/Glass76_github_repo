//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// Glass76Prefs -- the user-global preferences file.
//
// Appearance, skin, background image and refresh rate are meant to survive
// across DAW projects and plug-in instances, not just live inside whatever
// project happens to be open. The host's own state stream (see
// Glass76Controller::getState/setState) is per-project, so it cannot do
// that job; this is a small flat JSON file in the user's own Documents
// folder instead, at:
//
//   <Documents>/Glass76/preferences.json
//
// No JSON library is reachable from this target -- rapidjson lives inside
// VSTGUI but its include path is not propagated out, and its use there is
// internal to UIDescription. The schema here is intentionally flat (one
// object, no nesting), so a ~180-line hand-rolled reader/writer is enough
// and is exercised directly by tools/offline_test.cpp (--prefs-test),
// with no VSTGUI dependency at all.
//
// Every function here is safe to call from the UI/main thread only, never
// throws, and fails soft: a missing or malformed file is treated the same
// as "no preferences yet" rather than an error the caller has to handle.
//------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>

namespace Jaxson {

//------------------------------------------------------------------------
struct Glass76Prefs
{
	int32_t version {1};
	std::string skin {"hardware"};       // "hardware" | "glass"
	std::string appearance {"dark"};     // "light" | "dark"
	int32_t refreshRateHz {30};          // snapped to {30, 60, 120} on load
	std::string backgroundImage;         // absolute path, or empty for none
	int32_t scalePercent {100};          // snapped to {25, 50, 100, 150, 200} on load
	bool transparentBackground {false};  // skip the opaque backdrop/card fills entirely
};

namespace prefs {

/** <Documents>/Glass76 -- resolved once per process and cached. Empty if
    Documents could not be resolved on this machine (prefs are then
    disabled for the whole run: load()/save() both just return false). */
const std::string& dir ();

/** dir() + "/preferences.json". Empty under the same condition as dir(). */
const std::string& path ();

/** Parses preferences.json. Returns false -- leaving `out` completely
    untouched -- if the file is missing, unreadable, or malformed in any
    structural way. The caller should keep its built-in defaults in that
    case, never apply a partially-parsed result. */
bool load (Glass76Prefs& out);

/** Serializes and atomically writes `prefs` to preferences.json, creating
    <Documents>/Glass76 first if needed. Returns false on any failure (for
    example a read-only Documents folder); the caller should stop retrying
    for the rest of the session rather than hammering the disk. */
bool save (const Glass76Prefs& prefs);

/** Opaque last-write-time tick count for preferences.json, or 0 if the
    file does not exist / cannot be stat'd. Two calls compare equal iff
    the file has not changed on disk between them -- used to detect edits
    made by another Glass76 instance without re-parsing on every poll. */
int64_t mtime ();

//--- exposed for tools/offline_test.cpp's --prefs-test ------------------
// Pure string <-> struct conversions, no filesystem access.
std::string serialize (const Glass76Prefs& prefs);
bool parse (const std::string& json, Glass76Prefs& out);

} // namespace prefs
} // namespace Jaxson
