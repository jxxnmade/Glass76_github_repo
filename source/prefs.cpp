//------------------------------------------------------------------------
// Copyright (c) 2026 jxxnmade
//
// See prefs.h. Path resolution is platform-specific (Windows below,
// everything else under the POSIX branch); the JSON reader/writer and the
// atomic-save logic are shared.
//------------------------------------------------------------------------

#include "prefs.h"

#include <cstdio>
#include <cstring>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif
#else
#include <cstdlib>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Jaxson {

//------------------------------------------------------------------------
// Path resolution
//------------------------------------------------------------------------
namespace {

#if defined(_WIN32)

std::string narrowUtf8 (const wchar_t* w)
{
	if (!w)
		return {};
	const int len = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return {};
	std::string out (static_cast<size_t> (len - 1), '\0');
	WideCharToMultiByte (CP_UTF8, 0, w, -1, out.data (), len, nullptr, nullptr);
	return out;
}

/** FOLDERID_Documents rather than a raw %USERPROFILE%\Documents guess --
    OneDrive Known Folder Move relocates Documents on a large share of
    Windows 11 machines, and only this call resolves the real location for
    the user actually running the DAW. KF_FLAG_CREATE materialises it if it
    is somehow missing. Falls back to environment variables, then gives up
    (empty string disables prefs for the run; the caller keeps defaults). */
std::string resolveDocumentsBase ()
{
	PWSTR raw = nullptr;
	std::string result;
	if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &raw)) && raw)
		result = narrowUtf8 (raw);
	if (raw)
		CoTaskMemFree (raw);
	if (!result.empty ())
		return result;

	wchar_t buf[MAX_PATH];
	const DWORD n = GetEnvironmentVariableW (L"USERPROFILE", buf, MAX_PATH);
	if (n > 0 && n < MAX_PATH)
	{
		const std::string base = narrowUtf8 (buf);
		if (!base.empty ())
			return base + "\\Documents";
	}

	wchar_t drive[16];
	wchar_t home[MAX_PATH];
	const DWORD nd = GetEnvironmentVariableW (L"HOMEDRIVE", drive, 16);
	const DWORD nh = GetEnvironmentVariableW (L"HOMEPATH", home, MAX_PATH);
	if (nd > 0 && nd < 16 && nh > 0 && nh < MAX_PATH)
	{
		const std::string d = narrowUtf8 (drive), h = narrowUtf8 (home);
		if (!d.empty () && !h.empty ())
			return d + h + "\\Documents";
	}

	return {};
}

constexpr char kSep = '\\';

#else  // macOS / Linux

std::string resolveDocumentsBase ()
{
	if (const char* home = std::getenv ("HOME"))
		if (*home)
			return std::string (home) + "/Documents";
	if (struct passwd* pw = getpwuid (getuid ()))
		if (pw->pw_dir && *pw->pw_dir)
			return std::string (pw->pw_dir) + "/Documents";
	return {};
}

constexpr char kSep = '/';

#endif

} // namespace

const std::string& prefs::dir ()
{
	static const std::string d = [] () -> std::string {
		const std::string base = resolveDocumentsBase ();
		if (base.empty ())
			return {};
		return base + kSep + "Glass76";
	} ();
	return d;
}

const std::string& prefs::path ()
{
	static const std::string p = [] () -> std::string {
		const std::string& d = dir ();
		if (d.empty ())
			return {};
		return d + kSep + "preferences.json";
	} ();
	return p;
}

//------------------------------------------------------------------------
// A ~180-line flat-JSON reader/writer. No dependency, no exceptions, and
// deliberately tolerant: the only thing that should ever make load() fail
// is a file that isn't a well-formed JSON object at all. A merely-unusual
// one (unknown keys, wrong-typed known keys, nested extras) degrades by
// ignoring the parts it doesn't understand, not by refusing the file.
//------------------------------------------------------------------------
namespace {

struct Value
{
	enum class Kind { String, Number, Bool, Null, Skipped } kind {Kind::Null};
	std::string s;
	double n {0.0};
	bool b {false};
};

struct Cursor
{
	const char* p;
	const char* end;

	bool eof () const { return p >= end; }
	char peek () const { return eof () ? '\0' : *p; }

	void skipWs ()
	{
		while (!eof () && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
			++p;
	}
};

void appendUtf8 (std::string& out, unsigned int cp)
{
	if (cp <= 0x7Fu)
	{
		out += static_cast<char> (cp);
	}
	else if (cp <= 0x7FFu)
	{
		out += static_cast<char> (0xC0u | (cp >> 6));
		out += static_cast<char> (0x80u | (cp & 0x3Fu));
	}
	else if (cp <= 0xFFFFu)
	{
		out += static_cast<char> (0xE0u | (cp >> 12));
		out += static_cast<char> (0x80u | ((cp >> 6) & 0x3Fu));
		out += static_cast<char> (0x80u | (cp & 0x3Fu));
	}
	else
	{
		out += static_cast<char> (0xF0u | (cp >> 18));
		out += static_cast<char> (0x80u | ((cp >> 12) & 0x3Fu));
		out += static_cast<char> (0x80u | ((cp >> 6) & 0x3Fu));
		out += static_cast<char> (0x80u | (cp & 0x3Fu));
	}
}

bool hex4 (Cursor& c, unsigned int& out)
{
	if (c.end - c.p < 4)
		return false;
	out = 0;
	for (int i = 0; i < 4; i++)
	{
		const char ch = c.p[i];
		out <<= 4;
		if (ch >= '0' && ch <= '9')
			out |= static_cast<unsigned int> (ch - '0');
		else if (ch >= 'a' && ch <= 'f')
			out |= static_cast<unsigned int> (ch - 'a' + 10);
		else if (ch >= 'A' && ch <= 'F')
			out |= static_cast<unsigned int> (ch - 'A' + 10);
		else
			return false;
	}
	c.p += 4;
	return true;
}

/** c.p must point at the opening quote. Consumes through the closing quote
    (inclusive). Unrecognised escapes keep their literal character rather
    than failing the file -- a hand-typed "C:\Users\..." must not corrupt
    every other field. */
bool parseString (Cursor& c, std::string& out)
{
	if (c.peek () != '"')
		return false;
	++c.p;
	out.clear ();
	while (true)
	{
		if (c.eof ())
			return false;
		const char ch = *c.p;
		if (ch == '"')
		{
			++c.p;
			return true;
		}
		if (static_cast<unsigned char> (ch) < 0x20)
			return false;   // raw control character is not valid JSON
		if (ch != '\\')
		{
			out += ch;
			++c.p;
			continue;
		}
		++c.p;   // consume backslash
		if (c.eof ())
			return false;
		const char esc = *c.p;
		switch (esc)
		{
			case '"': out += '"'; ++c.p; break;
			case '\\': out += '\\'; ++c.p; break;
			case '/': out += '/'; ++c.p; break;
			case 'b': out += '\b'; ++c.p; break;
			case 'f': out += '\f'; ++c.p; break;
			case 'n': out += '\n'; ++c.p; break;
			case 'r': out += '\r'; ++c.p; break;
			case 't': out += '\t'; ++c.p; break;
			case 'u':
			{
				++c.p;
				unsigned int cp = 0;
				if (!hex4 (c, cp))
					return false;
				if (cp >= 0xD800u && cp <= 0xDBFFu)
				{
					bool combined = false;
					if (c.end - c.p >= 6 && c.p[0] == '\\' && c.p[1] == 'u')
					{
						Cursor save = c;
						c.p += 2;
						unsigned int low = 0;
						if (hex4 (c, low) && low >= 0xDC00u && low <= 0xDFFFu)
						{
							appendUtf8 (out, 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u));
							combined = true;
						}
						else
						{
							c = save;
						}
					}
					if (!combined)
						appendUtf8 (out, 0xFFFDu);   // lone high surrogate
				}
				else if (cp >= 0xDC00u && cp <= 0xDFFFu)
				{
					appendUtf8 (out, 0xFFFDu);       // lone low surrogate
				}
				else
				{
					appendUtf8 (out, cp);
				}
				break;
			}
			default:
				out += esc;
				++c.p;
				break;
		}
	}
}

/** c.p must point at '{' or '['. Skips the whole nested value, respecting
    strings, so a bracket inside a string never confuses the matcher. This
    is the forward-compat hatch: a future field that is an object or array
    is silently skipped rather than breaking the file for this build. */
bool skipNested (Cursor& c)
{
	if (c.peek () != '{' && c.peek () != '[')
		return false;
	int depth = 0;
	while (!c.eof ())
	{
		const char ch = *c.p;
		if (ch == '"')
		{
			std::string dummy;
			if (!parseString (c, dummy))
				return false;
			continue;   // parseString already advanced past the closing quote
		}
		if (ch == '{' || ch == '[')
			depth++;
		else if (ch == '}' || ch == ']')
			depth--;
		++c.p;
		if (depth == 0)
			return true;
	}
	return false;
}

bool parseNumber (Cursor& c, double& out)
{
	const char* start = c.p;
	if (c.peek () == '-')
		++c.p;
	if (c.eof () || *c.p < '0' || *c.p > '9')
		return false;
	while (!c.eof () && *c.p >= '0' && *c.p <= '9')
		++c.p;
	if (!c.eof () && *c.p == '.')
	{
		++c.p;
		if (c.eof () || *c.p < '0' || *c.p > '9')
			return false;
		while (!c.eof () && *c.p >= '0' && *c.p <= '9')
			++c.p;
	}
	if (!c.eof () && (*c.p == 'e' || *c.p == 'E'))
	{
		++c.p;
		if (!c.eof () && (*c.p == '+' || *c.p == '-'))
			++c.p;
		if (c.eof () || *c.p < '0' || *c.p > '9')
			return false;
		while (!c.eof () && *c.p >= '0' && *c.p <= '9')
			++c.p;
	}
	// std::from_chars is locale-independent -- a host that has set the C
	// locale to something with a comma decimal separator must not corrupt
	// numeric fields the way strtod() would.
	const auto res = std::from_chars (start, c.p, out);
	return res.ec == std::errc {};
}

bool parseValue (Cursor& c, Value& v)
{
	c.skipWs ();
	if (c.eof ())
		return false;
	const char ch = c.peek ();
	if (ch == '"')
	{
		v.kind = Value::Kind::String;
		return parseString (c, v.s);
	}
	if (ch == '{' || ch == '[')
	{
		v.kind = Value::Kind::Skipped;
		return skipNested (c);
	}
	if (ch == 't')
	{
		if (c.end - c.p >= 4 && std::strncmp (c.p, "true", 4) == 0)
		{
			c.p += 4;
			v.kind = Value::Kind::Bool;
			v.b = true;
			return true;
		}
		return false;
	}
	if (ch == 'f')
	{
		if (c.end - c.p >= 5 && std::strncmp (c.p, "false", 5) == 0)
		{
			c.p += 5;
			v.kind = Value::Kind::Bool;
			v.b = false;
			return true;
		}
		return false;
	}
	if (ch == 'n')
	{
		if (c.end - c.p >= 4 && std::strncmp (c.p, "null", 4) == 0)
		{
			c.p += 4;
			v.kind = Value::Kind::Null;
			return true;
		}
		return false;
	}
	if (ch == '-' || (ch >= '0' && ch <= '9'))
	{
		v.kind = Value::Kind::Number;
		return parseNumber (c, v.n);
	}
	return false;
}

/** Ignored entirely when the key is unknown, or when a known key's value
    is not the type expected -- both are treated as "don't understand this
    part", never as a reason to reject the whole file. */
void applyField (const std::string& key, const Value& v, Glass76Prefs& out)
{
	if (key == "version" && v.kind == Value::Kind::Number)
		out.version = static_cast<int32_t> (v.n);
	else if (key == "skin" && v.kind == Value::Kind::String)
		out.skin = v.s;
	else if (key == "appearance" && v.kind == Value::Kind::String)
		out.appearance = v.s;
	else if (key == "refreshRateHz" && v.kind == Value::Kind::Number)
		out.refreshRateHz = static_cast<int32_t> (v.n);
	else if (key == "backgroundImage" && v.kind == Value::Kind::String)
		out.backgroundImage = v.s;
}

bool parseObject (Cursor& c, Glass76Prefs& out)
{
	c.skipWs ();
	if (c.peek () != '{')
		return false;
	++c.p;
	c.skipWs ();
	if (c.peek () == '}')
	{
		++c.p;
		return true;   // empty object: every field keeps its default
	}
	while (true)
	{
		c.skipWs ();
		std::string key;
		if (!parseString (c, key))
			return false;
		c.skipWs ();
		if (c.peek () != ':')
			return false;
		++c.p;
		Value v;
		if (!parseValue (c, v))
			return false;
		applyField (key, v, out);

		c.skipWs ();
		const char ch = c.peek ();
		if (ch == ',')
		{
			++c.p;
			c.skipWs ();
			if (c.peek () == '}')   // trailing comma before close
			{
				++c.p;
				return true;
			}
			continue;
		}
		if (ch == '}')
		{
			++c.p;
			return true;
		}
		return false;   // anything else here is a structural error
	}
}

std::string jsonEscape (const std::string& s)
{
	std::string out;
	out.reserve (s.size () + 8);
	for (unsigned char ch : s)
	{
		switch (ch)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (ch < 0x20)
				{
					char buf[8];
					std::snprintf (buf, sizeof (buf), "\\u%04x", ch);
					out += buf;
				}
				else
				{
					out += static_cast<char> (ch);
				}
				break;
		}
	}
	return out;
}

} // namespace

//------------------------------------------------------------------------
bool prefs::parse (const std::string& json, Glass76Prefs& out)
{
	Cursor c {json.data (), json.data () + json.size ()};

	// A BOM is not valid JSON but Notepad writes one without asking.
	if (c.end - c.p >= 3 && static_cast<unsigned char> (c.p[0]) == 0xEF &&
	    static_cast<unsigned char> (c.p[1]) == 0xBB && static_cast<unsigned char> (c.p[2]) == 0xBF)
		c.p += 3;

	// Parse into a scratch copy: `out` must never end up holding a
	// partially-applied result of a file that turned out to be malformed
	// partway through.
	Glass76Prefs candidate;
	if (!parseObject (c, candidate))
		return false;
	out = candidate;
	// Trailing bytes after the closing '}' are ignored on purpose.
	return true;
}

//------------------------------------------------------------------------
std::string prefs::serialize (const Glass76Prefs& p)
{
	std::string out;
	out += "{\n";
	out += "  \"version\": " + std::to_string (p.version) + ",\n";
	out += "  \"skin\": \"" + jsonEscape (p.skin) + "\",\n";
	out += "  \"appearance\": \"" + jsonEscape (p.appearance) + "\",\n";
	out += "  \"refreshRateHz\": " + std::to_string (p.refreshRateHz) + ",\n";
	out += "  \"backgroundImage\": \"" + jsonEscape (p.backgroundImage) + "\"\n";
	out += "}\n";
	return out;
}

//------------------------------------------------------------------------
bool prefs::load (Glass76Prefs& out)
{
	const std::string& p = path ();
	if (p.empty ())
		return false;

	std::ifstream is (std::filesystem::u8path (p), std::ios::binary);
	if (!is)
		return false;
	std::string content ((std::istreambuf_iterator<char> (is)), std::istreambuf_iterator<char> ());
	if (content.empty ())
		return false;

	return parse (content, out);
}

//------------------------------------------------------------------------
bool prefs::save (const Glass76Prefs& p)
{
	namespace fs = std::filesystem;

	const std::string& dirStr = dir ();
	const std::string& finStr = path ();
	if (dirStr.empty () || finStr.empty ())
		return false;

	const fs::path dirPath = fs::u8path (dirStr);
	const fs::path finPath = fs::u8path (finStr);
	const fs::path tmpPath = fs::u8path (finStr + ".tmp");

	std::error_code ec;
	fs::create_directories (dirPath, ec);   // first-run creation; "already exists" is fine

	{
		std::ofstream os (tmpPath, std::ios::binary | std::ios::trunc);
		if (!os)
			return false;
		const std::string body = serialize (p);
		os.write (body.data (), static_cast<std::streamsize> (body.size ()));
		os.flush ();
		if (!os)
		{
			std::error_code rmEc;
			fs::remove (tmpPath, rmEc);
			return false;
		}
	}

	fs::rename (tmpPath, finPath, ec);
	if (ec)
	{
		// Something (an AV scanner, a second instance) may be holding the
		// target open. One retry: remove then rename. Still atomic from the
		// point after the remove, which is the best this situation allows.
		std::error_code rmEc;
		fs::remove (finPath, rmEc);
		ec.clear ();
		fs::rename (tmpPath, finPath, ec);
	}
	if (ec)
	{
		std::error_code rmEc;
		fs::remove (tmpPath, rmEc);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------
int64_t prefs::mtime ()
{
	const std::string& p = path ();
	if (p.empty ())
		return 0;
	std::error_code ec;
	const auto t = std::filesystem::last_write_time (std::filesystem::u8path (p), ec);
	if (ec)
		return 0;
	return static_cast<int64_t> (t.time_since_epoch ().count ());
}

} // namespace Jaxson
