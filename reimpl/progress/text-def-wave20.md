# Wave-20 — the localized-text DEFINITION-FILE driver + per-label .dat compiler

**Agent:** W20-TEXT · **MCP:** live (`gilde.exe`, imagebase 0x400000) · analysis + new module.

Reconstructs the two text-loader functions that sit ABOVE the already-reconstructed
binary `.res` reader (`gui/text_load.{h,cpp}` from wave-18 + `sim/name_tables`):

| addr | name | bytes | verdict |
|------|------|------:|---------|
| 0x44b8f4 | VIBE_Text_LoadDefinitionFile | 615 | reconstructed 1:1 |
| 0x44b2e0 | VIBE_Text_ParseLabelDefinition | 1469 | reconstructed 1:1 |

New module: `src/gui/text_definition.{h,cpp}` (namespace `guild::gui::text`).

---

## The corrected loader topology (the wave-18 naming was conflated)

Three DISTINCT functions, now all pinned by disasm:

```
VIBE_App_InitEngineAndScriptCommands @0x528560
  -> sprintf("\project\game\gilde_text.def")
  -> VIBE_Text_LoadDefinitionFile @0x44b8f4      <-- THIS WAVE (the .def driver)
       reads gilde_text.def line by line (VIBE_Text_ReadLine @0x5e9e30),
       collects every  #include "Name.ext"  (ext stripped), then per name:
  -> VIBE_Text_LoadTextFile @0x44dba0            (the BINARY .res reader; wave-18
       opens "textbin_<lang>\<name>.res" via the VFS and appends its compiled
       strings to the global text array dword_8C36B0 / byte_8D36B0 / byte_767EB0.)

VIBE_Text_BuildTextArray @0x44bb5c               (the dev-time .dat COMPILER)
  -> VIBE_Text_ParseLabelDefinition @0x44b2e0    <-- THIS WAVE (per-.dat-line)
       compiles ONE human-authored .dat source line into one text-array entry.
```

> **Naming note (no edit made):** the wave-18 `BuildTextArray()` in
> `src/gui/text_load.cpp` is labelled `@0x44bb5c` but actually reconstructs the
> *binary `.res` parse* performed inside `VIBE_Text_LoadTextFile @0x44dba0`. The
> real `0x44bb5c` is the `.txt`/`.dat` compiler that drives `ParseLabelDefinition`.
> I did not rename it (not my owned file, and the binary-parse body is correct and
> widely depended on); I document the discrepancy here so the registry is accurate.

---

## VIBE_Text_LoadDefinitionFile @0x44b8f4 — the .def grammar (recovered)

`loc_5CB930` is `strstr`; `VIBE_Util_StrChr @0x5d3ef0` is `strchr`. Per line:

- **`//` anywhere → comment, skip** (`strstr(line,"//") != 0`).
- else **must contain `#include`** (`strstr(line,"#include")`), else skip
  (handles the `#outputpath "..."` / `#headerfile "..."` / blank lines).
- the first `"` at/after `#include` opens the name; the next `"` closes it; the
  quoted name is cut at its **first `.`** (`strchr(name,'.')`, ext strip). So
  `"Text_C_Personen.dat"` → `Text_C_Personen` and `"Gesetze\Text_G0.dat"` →
  `Gesetze\Text_G0` (the subdir backslash survives — no `.` before the ext).
- collected into a 64 KB / 512-byte-stride buffer ⇒ **max 128 includes**.
- after EOF, calls `VIBE_Text_LoadTextFile(name)` for each in order; **bails at
  the first failure** (the original breaks its load loop on the first `0` return).
  On success sets the "text loaded" flag `dword_B537B4 = 1`.

Reconstructed as `ParseDefinitionIncludes(text,len)` (pure parse → ordered name
list) + `LoadDefinitionFile(text,len, loadOne, loadedFlag)` (the driver with a
per-file loader callback, so the .def parse is testable without a VFS while the
exact call order + early-out are preserved).

Verified against the **real** `europe_guild_1400_original/gilde_text.def`:
`Text_A_Allgemein`, `Text_B_Menues`, `Text_C_Personen`, …, `Gesetze\Text_CA_Aemterinfos`.

## VIBE_Text_ParseLabelDefinition @0x44b2e0 — the .dat line grammar (recovered)

One source line, tokenized on the delimiter set `",\";\n"` (comma / double-quote /
semicolon / newline) by `VIBE_Text_StrtokWhitespace @0x5e9cd0` (REUSED). Helper
leaves recovered + reused: `SkipLeadingSpaces @0x44b268`, `TrimTrailingSpaces
@0x44b2ac`, `StrCmpNoCase @0x5cb8f0` (ASCII A-Z fold only), `StrNCopyPad @0x5d9360`
(copy ≤n, then NUL-pad the field), `Sprintf @0x5cba00`. `SetGrayColorThunk
@0x5c6af0` is a dword-aligned memset.

- **token[0] = gender selector**, trimmed + compared case-insensitively:
  `(m)`→tag 0, `(w)`→tag 1, `(s)`→tag 2, anything else→tag 0. (`byte_767EB0[id]`.)
- **token[1..] = one per language column (max 4)**, each `prefix[single/plural]suffix`:
  - scan for `[`; if absent the **whole trimmed token** is the value (goes in the
    `prefix` slot, single/plural/suffix empty).
  - if `[` present: `prefix` = text before `[`; scan from `[` for `/`:
    - `[single/plural]` → `single = (`[`+1 .. `/`)`, `plural = (`/`+1 .. `]`)`;
      missing `]` ⇒ **return 0** (malformed).
    - `[core]` (no `/`) → `single = (`[`+1 .. `]`)` (full contents),
      `plural = (`[`+2 .. `]`)` — **the recovered quirk: plural drops the first
      contents char**; missing `]` ⇒ **return 0**.
    - `suffix` = text after `]` (only if non-empty).
  - the **singular form** = `sprintf("%s%s%s", prefix, single, suffix)`,
    the **plural form** = `sprintf("%s%s%s", prefix, plural, suffix)`.
- **stored string** = the per-language singular block then the per-language plural
  block, each language `'|'`-terminated (`asc_61899C`), all 8 slots emitted:
  `s0|s1|s2|s3|p0|p1|p2|p3|`. Written into the 4096-byte blob slot
  `dword_62EB2C + (dword_62EB24<<12)`; `dword_62EB30` += the byte total + 1.
- **key** = `sprintf("%s+%i", byte_A136B0[80*(file-1)], id - dword_76BEAC[file])`
  ⇒ `"<filePrefix>+<localIndexWithinFile>"` (`byte_8D36B0[80*id]`).
- advances `dword_62EB24` (entry count). Returns 1.

Modeled as `LabelCompilerState` (explicit cursor: `count`==dword_62EB24,
`blobLen`==dword_62EB30, `fileBase[]`==dword_76BEAC, `filePrefix[]`==byte_A136B0)
+ `ParseLabelDefinition(line, fileIndex, st, db)` appending to a `TextDb`.

---

## Callees — reconstructed vs genuine leaves

Reconstructed inline (leaf string/mem helpers, all 1:1 from disasm):
`strstr`/`strchr` (loc_5CB930 / StrChr@0x5d3ef0), `SkipLeadingSpaces@0x44b268`,
`TrimTrailingSpaces@0x44b2ac`, `StrCmpNoCase@0x5cb8f0`, `StrNCopyPad@0x5d9360`,
`SetGrayColorThunk@0x5c6af0`(memset). REUSED (already in tree):
`StrtokWhitespace@0x5e9cd0` (`gui/text/text_strtok`), `BuildTextArray`/
`Text_LoadTextFile` (`gui/text_load`), `TextDb` (`gui/text/textdb`).
Genuine boundaries NOT reconstructed (rule 8 / out of scope): `VIBE_Text_ReadLine
@0x5e9e30` (CRT getc cooked-text stream; my .def parser walks an in-memory buffer
line by line equivalently), `VIBE_File_OpenStream@0x5d4488` / `VIBE_Vfs_*`
(file IO — supplied by the `loadOne` callback / the wave-18 VFS path),
`MessageBoxA` (Win32 → the original's "Could not open Textfiledefinition!" error
box; surfaced as the driver returning false).

---

## Wiring (rule 13) + handoff

- **Owned + wired:** `text_definition.{h,cpp}` is self-contained and exercised by
  the unit + e2e suites. `LoadDefinitionFile`'s `loadOne` callback is the seam the
  engine uses; over the real install the e2e drives it with
  `BuildTextArray(zip.ExtractByName(name+".res"))` — the faithful
  `VIBE_Text_LoadTextFile` member-resolution.
- **Handoff (NOT my files — documented, not edited):**
  - `src/app/real_text_driver.cpp` `LoadRealTextDb()` currently walks **all**
    `.res` members of the mounted textbin in central-directory order. The faithful
    behavior is to walk them in **gilde_text.def order**: it should call
    `gui::text::ParseDefinitionIncludes(defBytes)` and, for each name, open
    `<name>.res` + `BuildTextArray` (skip/stop policy per
    `VIBE_Text_LoadDefinitionFile`). One-line swap; left to the `app/` owner.
  - `src/app/wiring.cpp` `RealSubsystems::textLoadDefinitionFile()` (the
    `gamelogic.h` hook for `0x44b8f4`) can now delegate to
    `gui::text::LoadDefinitionFile(defBytes, len, loadOne, &flag)` instead of its
    deferral stub, with `loadOne = Text_LoadTextFile`.

---

## Tests

- `tests/unit/text_definition_test.cpp` — 17 tests / 51 checks (no asset):
  .def include extraction (basic, ext-strip + subdir, `//` comment dominance,
  no-quote/no-include skips, CRLF tolerance), driver order + first-failure
  early-out; .dat gender tags + key synthesis, no-bracket single-language blob,
  multi-language columns, `[single/plural]`, the no-`/` `[core]` plural-drop
  quirk, malformed-bracket `return 0`, per-file localIndex reset.
- `tests/e2e/text_definition_e2e_test.cpp` — 2 tests / 14 checks, GUARDED on
  `gilde_text.def` + `Resources/textbin_deutsch.BIN` (skips cleanly when absent;
  honors `GUILD_GAME_DIR`): parses the REAL def (pins the first three includes +
  the Gesetze subdir entry), then drives the ordered load from the real
  textbin_deutsch.BIN PKZIP (≥10 core members parse, TextDb >1000 entries).

Both suites built + run green standalone (the shared-library link is currently
blocked by an unrelated concurrent agent's untracked `src/sim/command_leaves.cpp`,
which references an undeclared `RunActionOrFree` — not touched per ownership rules;
my two translation units `-fsyntax-only` clean and link/run isolated).
