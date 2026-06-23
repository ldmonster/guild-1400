# Wave-18 — first-name / dynasty-name string tables (the dword_8C400C/8C4320/8C4508 boundary)

## The boundary, and the correction

The deferral (newgame-apply.md / person_create.h / gap-person-create-wave17.md)
described three runtime first-name POINTER tables, "all-zero in the static image,
**filled from localized text resources by 0x530e50**", with `ParentFirstName`
defaulting to `""`:

- `dword_8C400C` — male first names (191, `RandomModulo(0xBF)`)
- `dword_8C4320` — female first names (112, `RandomModulo(0x70)`)
- `dword_8C4508` — dynasty / family-female names (149, `RandomModulo(0x95)`)

**The "0x530e50 fills them" premise was wrong.** `VIBE_GameLogic_CleanupTurnHandlers
@0x530e50` *reads* the tables (NPC rename @0x530f6a male / @0x531063 female); it
never writes them. xrefs to all three constants are READ-only, from four functions:
`VIBE_Person_CreateAndSpawn @0x58da70`, `VIBE_Command_EnqueueInheritanceTransfer
@0x5336f0`, `VIBE_GameLogic_CleanupTurnHandlers @0x530e50`,
`VIBE_MeisterAi_SelectMoodColor @0x4664d8`. No code stores to the table base
(verified: `find_bytes "0C 40 8C 00"` / `20 43 8C 00` / `08 45 8C 00` hit only the
reader operands).

## What the tables actually are (the recovered truth)

They are **fixed-index slices of the single global localized-text pointer array
`dword_8C36B0`** (the "text array" — guild::gui::text::TextDb). Relative to that
base:

| table        | address    | (addr-0x8C36B0)/4 | id  | count | draw |
|--------------|------------|-------------------|-----|-------|------|
| male 8C400C  | 0x8C400C   | 599               | 599 | 191   | 0xBF |
| female 8C4320| 0x8C4320   | 796               | 796 | 112   | 0x70 |
| dynasty 8C4508| 0x8C4508  | 918               | 918 | 149   | 0x95 |

Confirmed against `VIBE_Text_LoadTextFile @0x44dba0` (disasm @0x44de06), which fills
`dword_8C36AC[baseId+1+i] == dword_8C36B0[baseId+i]` (the +4/-4 alias of the same
array). So `dword_8C400C[k] == dword_8C36B0[599+k]`, etc.

## The real loader chain (rule 7, the live call tree)

```
VIBE_App_InitEngineAndScriptCommands @0x528560
  -> sprintf("%s%s.def", "\project\game\", "gilde_text")  (= gilde_text.def)
  -> VIBE_Text_LoadDefinitionFile @0x44b8f4
       parses the def's quoted ("label" "file.ext") line pairs, strips the ext,
       and per entry calls:
  -> VIBE_Text_LoadTextFile @0x44dba0  (name = e.g. "Text_C_Personen")
       sprintf("textbin_%s\\%s.res", aGerman, name)         (aGerman default "german")
       VfsOpenFile(...)  -> a .res MEMBER of the shipped textbin_<lang>.BIN PKZIP
       reads the compiled .res and appends its strings into dword_8C36B0 at the
       file's declared baseIndex (header +4).
```

`aGerman` (0x62eae4) is a runtime-writable buffer set at startup from `Gilde.INI`
`[General] Language` with DEFAULT "german" (`VIBE_GameLogic_MainEntryAndShutdown
@0x534bbc`, `GetPrivateProfileStringA`, default-arg "german"). The shipped Russian
install has no `Language=` key, so it stays "german"; the path
`german\textbin_german\Text_C_Personen.res` resolves through the VFS directory
mounts to the shipped `Resources/textbin_deutsch.BIN` member `Text_C_Personen.res`.

### The .res binary format (byte-for-byte — matches gui/text_load.h exactly)

```
+0x00 u32 entryCount
+0x04 u32 baseIndex        (global text-array id of this file's first string)
+0x08 u32 lastIndex        (baseIndex + entryCount - 1)
+0x0C u32 offset[entryCount]   (byte offset of each string into the blob)
      u8  name[entryCount][80]  (per-entry key, 0x50 stride, byte_8D36B0)
      u8  tag[entryCount]       (byte_767EB0; 0xFF plain, 9..18 {rN})
+X    u32 blobSize
+X+4  u8  blob[blobSize]        (NUL-terminated strings)
```
Real `Text_C_Personen.res` in `textbin_deutsch.BIN`: entryCount=806, baseIndex=272,
lastIndex=1077 — so the name ids 599..1066 fall inside it (male id 599 = file
string index 327). The baseIndex is read from the header, so the loader is correct
for whichever textbin archive the host resolves (the older base `textbin.BIN` has
baseIndex 262 and a different layout — the patched deutsch one is the canonical
default-language asset; its slice boundaries are clean: male ends "Öerres", female
ends female names, dynasty are surnames).

## What CLOSED (reconstructed 1:1, wired)

New module `src/sim/name_tables.{h,cpp}`:

- `NameTables_LoadFromResBuffer(data,len)` — parse a compiled Text_C_Personen.res
  via the already-reconstructed `guild::gui::text::BuildTextArray @0x44bb5c` into a
  TextDb (the `dword_8C36B0` model) and populate the slices. Requires the file to
  span the name id ranges (the Personen file does).
- `NameTables_LoadFromArchive(fs, binPath)` — the faithful runtime mirror: open the
  textbin PKZIP with the reconstructed `guild::io::ZipArchive` and `ExtractByName`
  (= `VIBE_Zip_LocateFileByName @0x5eafbc` + `OpenCurrentFile @0x5eb2d0`), then load
  the bytes. (Used by hosts that have the real install but no VFS dir mounts wired.)
- `NameAt(kind, index)` / `MaleNameAt` / `FemaleNameAt` / `DynastyNameAt` — the
  `dword_8C400C[index]` / `8C4320[index]` / `8C4508[index]` slice reads, with the
  inert `""` fallback (not loaded / OOR / empty slot) the original behavior implies.

Wired (additive, greens kept): `NewGameApplyHooks::ParentFirstName`
(`src/play/newgame_apply.cpp`) now returns `sim::NameAt(female ? female : male,
index)` when the tables are loaded, else `""` — so over the real install the
parent first-name stamps (`0x533834`/`0x533889`) get real localized names, and the
headless tests (no asset) keep the inert `""` and stay green.

## Handoff (not my owned files — documented, not edited)

- `src/sim/person_create.cpp` `PersonCreateHooks::firstName` (the CreateAndSpawn
  name draws at +0x20 dynasty / +0x30 male|female) can be pointed at
  `sim::NameAt` the same way (gender 0 male / 1 female / 2 dynasty already match the
  `NameKind` enum). Left to the person_create owner; the default `nullptr` hook is
  unchanged so existing behavior/greens are preserved.
- `src/io/vfs.cpp` `VfsOpenFile` resolves only direct `.BIN` paths (first member);
  the `german\textbin_german\` directory-mount resolution
  (`VIBE_Vfs_ResolvePath`/`VIBE_Vfs_AddFileByPath`) is the out-of-scope VFS tree.
  Until those mounts are wired, the engine path `Text_LoadTextFile("Text_C_Personen")`
  cannot resolve the member by virtual path; `NameTables_LoadFromArchive` provides
  the faithful direct-archive equivalent.

## Tests

- `tests/unit/name_tables_test.cpp` — 4 tests / 34 checks: the slice anchors
  computed from the literal table addresses vs 0x8C36B0 (599/796/918) and the draw
  widths (0xBF/0x70/0x95); the parse + slice read on a synthetic Personen.res that
  embeds each absolute id in its string; bounds + inert `""` fallback; short/wrong
  file does not load.
- `tests/e2e/name_tables_e2e_test.cpp` — 2 tests / 465 checks, GUARDED on
  `Resources/textbin_deutsch.BIN` (skips cleanly when absent; honors
  GUILD_GAME_DIR). Loads the real archive member through ZipArchive+BuildTextArray,
  asserts every male/female slot is a real name, golden-pins the exact CP1251 bytes
  at the six slice-boundary ids, and pins the ONE genuinely-empty dynasty slot
  (id 967, index 49) the shipped archive ships; idempotent reload.
- Dependent suites kept green: `newgame_apply_test` (142), `newgame_diff_w15_test`
  (16), `newgame_result_itest` (23).
