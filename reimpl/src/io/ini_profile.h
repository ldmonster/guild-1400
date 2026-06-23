#pragma once
// gilde.exe — guild::io  (MODULE: Win32 .ini profile parser)
//
// Faithful reimplementation of the kernel32 private-profile API that gilde.exe
// links against by import thunk (not a VIBE_ function — no decompile of its own):
//
//   GetPrivateProfileStringA  @0x60e6b0  (kernel32)
//   GetPrivateProfileIntA     @0x60e6ac  (kernel32)
//   WritePrivateProfileStringA@0x60e754  (kernel32)
//
// The game relies on the *documented* (MSDN) semantics of these calls. The live
// call sites that depend on this parser are:
//
//   GetPrivateProfileStringA used by:
//     0x468f6c VIBE_AiMethod_RegisterFromIni      (section=method name, keys
//              "ShortDesire%li"/"ShortChange%li"/"LongDesire%li"/"LongChange%li",
//              default="" , nSize=0x80; non-zero return == key present)
//     0x503f78 VIBE_Net_StartNetworkGame
//     0x507144 VIBE_City_LoadDefinitionIni        (many keys, nSize varies)
//     0x529074 VIBE_Menu_EnterNetworkIp
//     0x529d08 VIBE_Menu_RunMainMenu
//     0x52ccd8 VIBE_Menu_RunChoosePlayer
//     0x534bbc VIBE_GameLogic_MainEntryAndShutdown
//     0x56b834 VIBE_Config_ReadGfxAndSoundSettings (Game/stadt default "Augsburg")
//
//   GetPrivateProfileIntA used by:
//     0x52ccd8 VIBE_Menu_RunChoosePlayer
//     0x534bbc VIBE_GameLogic_MainEntryAndShutdown
//     0x56b834 VIBE_Config_ReadGfxAndSoundSettings (Gfx/Sound/Game numeric knobs)
//
//   WritePrivateProfileStringA used by:
//     0x503f78 VIBE_Net_StartNetworkGame
//     0x529074 VIBE_Menu_EnterNetworkIp
//     0x529248 VIBE_Menu_SearchNetworkGames
//     0x52ccd8 VIBE_Menu_RunChoosePlayer
//     0x56af54 VIBE_Config_WriteGfxSettings
//
// All file bytes come through shim::IFileSystem (no OS calls in game code). For
// headless tests, the buffer-parsing core operates on an in-memory .ini string.
//
// Observed caller usage: only the (section!=NULL, key!=NULL) lookup form and the
// integer form are exercised; the NULL-section / NULL-key enumeration forms are
// implemented here for MSDN fidelity but are not reached by any current caller.

#include "guild/common/types.h"
#include "shim/IFileSystem.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::io {

// ---------------------------------------------------------------------------
// Parsed model of an .ini file (the buffer-parsing core, testable in-memory).
// Sections and keys preserve insertion order (MSDN enumeration order == file
// order); lookups are ASCII case-insensitive on both section and key, matching
// the kernel32 behavior the game depends on.
// ---------------------------------------------------------------------------
struct IniKeyValue {
    std::string key;    // raw key text, leading/trailing whitespace trimmed
    std::string value;  // raw value text AFTER trim + surrounding-quote strip
};

struct IniSection {
    std::string              name;   // text between [ ] , trimmed
    std::vector<IniKeyValue> keys;
};

class IniProfile {
public:
    IniProfile() = default;

    // Parse an in-memory .ini buffer (the core). Recognizes:
    //   [section]      -> opens a section (text between brackets, trimmed)
    //   key = value    -> a key in the current section
    //   ; or # comment -> line ignored (kernel32 treats ';' as comment)
    // Leading whitespace on a line is skipped. A line with no '=' and not a
    // section header is ignored. Keys before any [section] go into an unnamed
    // section (name == ""), matching kernel32 (keys with no section are not
    // reachable by a named-section query, which is also kernel32 behavior).
    void parse(const char* data, std::size_t len);
    void parse(const std::string& text) { parse(text.data(), text.size()); }

    // Win32 GetPrivateProfileStringA core (section!=NULL, key!=NULL form).
    // Looks up [section] key, returns the trimmed/unquoted value, or `defValue`
    // (which may be NULL -> treated as "") if section or key is absent. Copies
    // up to nSize-1 chars + a NUL into `out`; returns the number of chars
    // copied EXCLUDING the terminating NUL (MSDN semantics). If nSize==0 or
    // out==NULL, copies nothing and returns 0. The source string is itself
    // truncated to nSize-1 chars when it does not fit.
    guild::u32 getString(const char* section, const char* key,
                         const char* defValue, char* out, guild::u32 nSize) const;

    // Win32 form with key==NULL: fill `out` with all key names in `section`,
    // each NUL-terminated, the list terminated by an extra NUL (double-NUL).
    // Returns chars copied excluding the FINAL terminating NUL (MSDN: "the
    // number of characters copied to the buffer, not including the terminating
    // null character"). Truncates to fit nSize, always double-NUL terminating.
    guild::u32 getSectionKeyNames(const char* section, char* out,
                                  guild::u32 nSize) const;

    // Win32 form with section==NULL: fill `out` with all section names, each
    // NUL-terminated, double-NUL terminated overall. Same return semantics.
    guild::u32 getSectionNames(char* out, guild::u32 nSize) const;

    // Win32 GetPrivateProfileIntA core. Parses the value for [section] key as a
    // signed decimal integer (optional sign, leading decimal digits; parsing
    // stops at the first non-digit), returns it as an int. If absent or the
    // value has no leading digits, returns `defValue`. Matches kernel32: a
    // negative parsed result is returned as-is (the documented "unsigned"
    // return type is just the bit pattern).
    int getInt(const char* section, const char* key, int defValue) const;

    // Win32 WritePrivateProfileStringA core (in-memory mutation):
    //   section!=NULL, key!=NULL, value!=NULL -> set/insert key=value
    //   section!=NULL, key!=NULL, value==NULL -> delete the key
    //   section!=NULL, key==NULL              -> delete the whole section
    // Returns true (kernel32 returns nonzero on success).
    bool setString(const char* section, const char* key, const char* value);

    // Serialize back to .ini text ("[section]\r\nkey=value\r\n" blocks). Used by
    // the WritePrivateProfileStringA entry point to write the file back out.
    std::string serialize() const;

    const std::vector<IniSection>& sections() const { return sections_; }

private:
    std::vector<IniSection> sections_;

    const IniSection* findSection(const char* name) const;
    IniSection*       findSectionMut(const char* name);
};

// ---------------------------------------------------------------------------
// Win32-shaped entry points (route file I/O through shim::IFileSystem).
//
// These mirror the kernel32 signatures the game imports. The host filesystem
// is passed explicitly (the integrator binds the process-wide IFileSystem used
// by the VFS); `nullptr` makes the file behave as absent (-> default value),
// matching kernel32 when the .ini file does not exist.
// ---------------------------------------------------------------------------

// GetPrivateProfileStringA @0x60e6b0. section/key may be NULL (enumeration
// forms). Reads `fileName` via `fs`, parses, and applies the String semantics.
guild::u32 GetPrivateProfileStringA(guild::shim::IFileSystem* fs,
                                    const char* section, const char* key,
                                    const char* defValue, char* out,
                                    guild::u32 nSize, const char* fileName);

// GetPrivateProfileIntA @0x60e6ac.
int GetPrivateProfileIntA(guild::shim::IFileSystem* fs, const char* section,
                          const char* key, int defValue, const char* fileName);

// WritePrivateProfileStringA @0x60e754. Reads the existing file (if any),
// applies the mutation, writes it back. Returns true on success.
bool WritePrivateProfileStringA(guild::shim::IFileSystem* fs,
                                const char* section, const char* key,
                                const char* value, const char* fileName);

} // namespace guild::io
