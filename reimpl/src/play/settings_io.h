#pragma once
// =============================================================================
// guild::play — settings load/save over the shim filesystem (the live
// persistence path of the native options screens).
//
// LOAD: the real reader — config::ReadGfxAndSoundSettings (gilde.exe 0x56b834,
//   VIBE_Config_ReadGfxAndSoundSettings) — over an INI file read through
//   shim::IFileSystem (the GetPrivateProfile* substitute, rule 4).
//
// SAVE: the real serializer — app::ConfigWriteGfxSettings (gilde.exe 0x56af54,
//   VIBE_Config_WriteGfxSettings) — emitting its exact (section, key, value)
//   sequence into an in-memory WritePrivateProfileStringA merge
//   (IniWriteProfileString below), then writing the merged text back through
//   shim::IFileSystem. The original calls WritePrivateProfileStringA(section,
//   key, value, byte_122F638) per key; the OS API's observable file semantics
//   (update one key in place, preserve every other line of the file) are
//   reproduced 1:1 here at the shim boundary.
//
// WritePrivateProfileStringA merge semantics implemented (per the Win32
// contract, cross-checked against Wine's profile.c):
//   * section and key matching is CASE-INSENSITIVE;
//   * if the key exists in the section, only that line is rewritten as
//     "<key>=<value>" — the file's ORIGINAL key spelling is preserved (Win32
//     keeps the stored key name and replaces the value), every other line
//     (comments, blank lines, other keys/sections) is preserved byte-for-byte;
//   * if the key is absent, "key=value" is inserted at the END of the section
//     (after its last non-blank line, before the next "[section]" header);
//   * if the section is absent, "[section]" + "key=value" are appended at the
//     end of the file;
//   * lines written/inserted use the file's existing line-ending convention
//     (CRLF if the file contains CRLF or is empty — Win32 writes CRLF — else LF).
// =============================================================================
#include "config/ini.h"   // GfxSettings / SoundSettings / GameSettings
#include <string>

namespace guild::shim { class IFileSystem; }

namespace guild::play {

// The three settings blocks gilde.INI's [Gfx]/[Sound]/[Game] sections carry —
// the in-memory mirror of the byte_1233510.. settings globals.
struct SettingsBundle {
    config::GfxSettings   gfx;
    config::SoundSettings sound;
    config::GameSettings  game;
};

// Load `iniName` (e.g. "Gilde.INI") from `fs` and parse it with the real
// reader (config::ReadGfxAndSoundSettings @0x56b834). Returns true if the file
// was found and read; on false `out` holds the reader's defaults (exactly what
// the original gets when GetPrivateProfile* runs against a missing file).
bool LoadSettings(shim::IFileSystem& fs, const std::string& iniName, SettingsBundle& out);

// One WritePrivateProfileStringA(section, key, value, <file>) applied to the
// file text `iniText`; returns the merged text (semantics documented above).
// Exposed so the merge can be golden-tested in isolation.
std::string IniWriteProfileString(std::string iniText, const std::string& section,
                                  const std::string& key, const std::string& value);

// Persist `s` into `iniName` through `fs`: reads the existing file (if any),
// runs the REAL serializer app::ConfigWriteGfxSettings (@0x56af54) with an
// IniWriteProfileString sink — so the emitted sections, key order and value
// formatting are the original's, and every line the serializer does not own
// ([General], [Network], comments, fog/screen_x/... keys) survives untouched —
// then writes the merged text back. Returns false only if the file cannot be
// opened for writing.
bool SaveSettings(shim::IFileSystem& fs, const std::string& iniName, const SettingsBundle& s);

} // namespace guild::play
