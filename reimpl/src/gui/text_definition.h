#pragma once
// guild::gui::text — the localized-text DEFINITION-FILE driver and the
//                    per-label ".dat" line compiler that sit ABOVE the binary
//                    ".res" reader (gui/text_load.{h,cpp}).
//
// Two recovered originals live here:
//
//   VIBE_Text_LoadDefinitionFile   @0x44b8f4 (615 b) — the ".def" driver.
//     Opens "<game>gilde_text.def" (a plain-text list), reads it line by line
//     (VIBE_Text_ReadLine @0x5e9e30), and for every non-comment line that
//     contains an #include directive extracts the quoted file name, strips its
//     extension, and remembers it. After the whole file is read it calls
//     VIBE_Text_LoadTextFile @0x44dba0 (REUSED, gui/text_load.h) for each
//     collected name in order, which opens "textbin_<lang>\\<name>.res" through
//     the VFS and appends that file's strings to the global text array (TextDb).
//
//   VIBE_Text_ParseLabelDefinition @0x44b2e0 (1469 b) — the per-line ".dat"
//     compiler. The shipped install ships only precompiled ".res" members, so
//     this dev-time authoring path is exercised by the developer tools; it is
//     still in the binary and reachable from engine init, and is reconstructed
//     1:1 here. It parses ONE comma/quote/semicolon-separated ".dat" source line
//     into one text-array entry (see ParseLabelDefinition below for the exact
//     token grammar) and appends it to the TextDb with a synthesized key
//     "<filePrefix>+<localIndex>" and a gender tag.
//
// ===== the ".def" line grammar (recovered, gilde_text.def) ==================
//   // ...            a comment line — any line that CONTAINS "//" is ignored
//                     (strstr(line,"//") != 0). [loc_5CB930 == strstr]
//   #outputpath "..." / #headerfile "..."   — directive lines with no #include;
//                     ignored (strstr(line,"#include") == 0).
//   #include "Name.ext"   — collected: the first '"' after "#include" opens the
//                     name, the next '"' closes it; the substring is then cut at
//                     its first '.' (StrChr(name,'.')), so "Text_C_Personen.dat"
//                     and "Gesetze\\Text_G0.dat" become "Text_C_Personen" and
//                     "Gesetze\\Text_G0". The ext-stripped name is what
//                     Text_LoadTextFile later turns into "textbin_<lang>\\<name>.res".
//
// ===== the ".dat" label line grammar (recovered) ===========================
// One source line, tokenized on the delimiter set ",\";\n" (comma, double-quote,
// semicolon, newline). [VIBE_Text_StrtokWhitespace @0x5e9cd0, REUSED]
//
//   token[0]   the gender selector, trimmed and matched case-insensitively:
//                "(m)" -> tag 0,  "(w)" -> tag 1,  "(s)" -> tag 2,
//                anything else -> tag 0.   (the recovered defaults)
//   token[1..] one per language column (max 4), each a value spec:
//                <prefix>[<singular>/<plural>]<suffix>
//              where the optional bracketed part carries a singular/plural pair:
//                - if a '[' ... '/' ... ']' is present:
//                    v65 = text before '['         (prefix)
//                    v63 = text between '[' and '/' (singular core)
//                    v64 = text between '/' and ']' (plural   core)
//                    v62 = text after ']'           (suffix)
//                - if a '[' ... ']' with no '/' is present, the same core is used
//                  for both singular and plural (v63 == v64) and v62 is the suffix.
//                - if there is NO '[', the whole token is the singular AND plural
//                  value (v65 holds it, v63/v64/v62 empty).
//              The singular form  is sprintf("%s%s%s", v65, v63, v62);
//              the plural   form  is sprintf("%s%s%s", v65, v64, v62).
//
// The entry's stored STRING is the per-language singular forms, then the
// per-language plural forms, each language separated by '|':
//     sing_lang0 | sing_lang1 | ... || plur_lang0 | plur_lang1 | ...
// (the original concatenates the singular block then the plural block into one
// 4096-byte blob slot at dword_62EB2C + (id<<12), '|'-joined; the renderers split
// it back out by language column at draw time).
//
// File IO routes through the IFileSystem-backed VFS shim, never the OS directly,
// exactly as the original routed through VIBE_File_OpenStream / VIBE_Vfs_*.

#include "gui/text/textdb.h"
#include "guild/common/types.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace guild::gui::text {

using guild::u8;
using guild::u32;

inline constexpr int kDefMaxIncludes = 128;   // v9/v33 buffer: 0x10000/0x200 == 128
inline constexpr int kDefNameStride  = 512;   // v33 stride (0x200) per collected name
inline constexpr int kLabelMaxLangs  = 4;     // v60/v59: 640 bytes / 160-byte stride
inline constexpr int kLabelLangStride = 160;  // OWORD*10 == 160 bytes per language slot

// gilde.exe 0x44b8f4 — parse the contents of a "gilde_text.def" buffer into the
// ordered list of ext-stripped include names. Pure (no IO): mirrors the original
// line loop + #include/quote/StrChr('.') extraction. Comment lines (containing
// "//") and lines with no "#include" are skipped. Returns the names in file
// order (capped at kDefMaxIncludes, as the original's 64 KB / 512 B buffer).
std::vector<std::string> ParseDefinitionIncludes(const char* text, std::size_t len);

// gilde.exe 0x44b8f4 — VIBE_Text_LoadDefinitionFile. The full driver: parse the
// .def buffer for #include names, then call `loadOne(name)` for each in order.
// Stops (returns false) the moment a `loadOne` returns false (the original bails
// out of its load loop at the first VIBE_Text_LoadTextFile failure). On success
// (every include loaded, or no includes) returns true and sets the "text loaded"
// flag (dword_B537B4) modeled by `*loadedFlag` when provided.
//
// `loadOne` is the per-file loader; in the engine this is
//   [](const std::string& name){ return Text_LoadTextFile(name.c_str(), db); }
// (Text_LoadTextFile @0x44dba0, REUSED) — separating it keeps the .def parse
// testable without a VFS while preserving the exact call order / early-out.
bool LoadDefinitionFile(const char* text, std::size_t len,
                        const std::function<bool(const std::string&)>& loadOne,
                        bool* loadedFlag = nullptr);

// ---------------------------------------------------------------------------
// VIBE_Text_ParseLabelDefinition @0x44b2e0 — per-".dat"-line compiler state.
//
// The original keeps its cursor in module globals; we model them explicitly so
// the compiler is testable in isolation and free of hidden state:
//   count      == dword_62EB24  (current entry index / total entries)
//   blobLen    == dword_62EB30  (running total of blob bytes written)
//   fileBase[] == dword_76BEAC  (first entry index of each include file)
//   filePrefix[]== byte_A136B0  (the label-name prefix per include file, 80-byte
//                                records; key becomes "<prefix>+<localIndex>")
// ---------------------------------------------------------------------------
struct LabelCompilerState {
    // Per include-file: the entry index where this file started, and the label
    // prefix used to synthesize keys. Index by the `fileIndex` argument.
    std::vector<int>         fileBase;     // dword_76BEAC
    std::vector<std::string> filePrefix;   // byte_A136B0[80*i]

    int count   = 0;   // dword_62EB24
    int blobLen = 0;   // dword_62EB30

    // Begin a new include file `fileIndex` with key prefix `prefix`. Records the
    // current entry index as that file's base (dword_76BEAC[fileIndex] = count).
    void BeginFile(int fileIndex, const std::string& prefix);
};

// gilde.exe 0x44b2e0 — parse ONE ".dat" source line `line` belonging to include
// file `fileIndex`, append the resulting entry to `db`, and advance `st`.
// Returns true on success (an entry was added) and false on a malformed bracket
// spec ('[' without a matching ']' when a value was expected) — matching the
// original's `return 0` early-outs. The stored string is the '|'-joined singular
// block followed by the plural block (see header grammar above); the key is
// "<filePrefix>+<localIndex>" and the tag is the gender selector (0/1/2).
bool ParseLabelDefinition(const char* line, int fileIndex,
                          LabelCompilerState& st, TextDb& db);

} // namespace guild::gui::text
