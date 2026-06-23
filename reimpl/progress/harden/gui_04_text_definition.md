# Harden sweep — gui_04 text_definition / text_load (MCP live, DISASM = reference)

Chunk files: `src/gui/text_definition.cpp`, `src/gui/text_load.cpp` (+ their headers,
`tests/unit/text_definition_test.cpp`).

Method: decompiled AND disassembled every provenance'd function; diffed line-for-line
against the binary. DISASM is the reference of record (Hex-Rays collapsed all the
register-arg `__usercall` helpers and the hand-rolled scan loops). Tests green.

## text_definition.cpp

| Addr | Function | Verdict |
|------|----------|---------|
| 0x5e9cd0 | StrtokWhitespace (REUSED via text_strtok.h) | VERIFIED-1:1 (out of chunk; only referenced) |
| 0x5d3ef0 | VIBE_Util_StrChr (`StrChr`) | **FIXED** — see below |
| 0x44b268 | VIBE_String_SkipLeadingSpaces | VERIFIED-1:1 |
| 0x44b2ac | VIBE_String_TrimTrailingSpaces | VERIFIED-1:1 |
| 0x44b2e0 | VIBE_Text_ParseLabelDefinition | VERIFIED-1:1 (with 2 documented defensive guards) |
| 0x44b587 | (the `++dword_62EB30` blob-length tail of 0x44b2e0) | VERIFIED-1:1 |
| 0x5cb8f0 | VIBE_Util_StrCmpNoCase | VERIFIED-1:1 |
| 0x5d9360 | VIBE_Util_StrNCopyPad | VERIFIED-1:1 |
| 0x44b8f4 | VIBE_Text_LoadDefinitionFile (`ParseDefinitionIncludes`/`LoadDefinitionFile`) | **FIXED** — see below |

### FIXED — 0x5d3ef0 `VIBE_Util_StrChr` returned FIRST, must return LAST
`StrChr` wrapped `std::strchr` (first occurrence) with a comment claiming "first".
Disasm 0x5d3ef0:
```
v2 = 0; do { if (a2 == *a1) v2 = a1; } while (*a1++); return v2;
```
It walks the WHOLE string, overwriting the result each match → returns the **last**
occurrence (and returns the terminator when searching for `\0`). Rewrote the body to
the faithful last-occurrence scan (compares the target as a byte, matching `a2@<dl>`).

### FIXED — 0x44b8f4 extension strip cut at FIRST dot, must cut at LAST dot
The `.def` include-name extension strip used `name.find('.')` (first dot). The original
(0x44bad2) NUL-terminates the name at the closing quote then calls
`VIBE_Util_StrChr(name, '.')` — which returns the **LAST** `.` (per the fix above) —
and writes a NUL there. Changed to strip at the last dot via the corrected `StrChr`.
Observably identical for every shipped name (each has exactly one dot, the `.dat`/`.res`
extension), but now 1:1 for multi-dot names. Added regression test
`TextDef.ParseIncludesStripsAtLastDot` (`"Text.v2.final.dat"` -> `"Text.v2.final"`).

### VERIFIED-1:1 — 0x44b2e0 ParseLabelDefinition (full disasm diff)
Confirmed against disasm, every branch and length:
- Gender token[0] only (var_1C==0 path): copy tok->v61, `SkipLeadingSpaces`->ecx,
  `TrimTrailingSpaces(ecx)`, then `StrCmpNoCase(ecx, "(m)"/"(w)"/"(s)")` — all three
  compares use the skip-trimmed pointer (0x44b3bc / 0x44b5a6 / 0x44b5c3). tag default 0,
  matched once. Source matches.
- Value tokens -> column `v69-1` (the strtok counter, gender = index 0). sprintf target
  `&v60[10*v69-10]` = `v60 + 160*(col)`; plural into `&v59 + 160*col`. Source `col=langIndex-1`.
- Bracket grammar verified arm-by-arm:
  - prefix = text before `[` (only when `[`!=p): `StrNCopyPad(v65,p,lbrack-p)` (0x44b6e7).
  - slash branch: single = `core..slash` len `slash-lbrack-1` (0x44b742); rbrack scanned
    from slash; `if(!rbrack||!v67) return 0` (0x44b76d); plural = `slash+1..rbrack` len
    `rbrack-slash-1` (0x44b7a2); suffix = `rbrack+1..end`, copied iff `strlen!=0` (0x44b7b3).
  - no-slash branch: rbrack scanned from `[`; `if(!rbrack||!v67) return 0` (0x44b800);
    single = `core..rbrack` len `rbrack-lbrack-1` (0x44b836); plural = `core+1..rbrack`
    len `rbrack-core-1` (0x44b867) — the documented "plural drops first core char" quirk;
    suffix same as slash branch. All lengths match source exactly.
  - no-bracket: whole trimmed token -> prefix (v65) (0x44b620 copy). Source matches.
- Blob assembly (LABEL_9): 4 singular slots (v60, stride 160, `while v13!=v61`) then 4
  plural slots (v59, stride 160, `while v23!=v60`), each `strcat("|")` then appended to
  the global blob; `dword_62EB30 += strlen(slot_with_pipe)` per slot, then `++dword_62EB30`
  (0x44b587). Source's `blobLen += seg.size()` (incl '|') for all 8 + `+1` is identical.
- Key: `sprintf("%s+%i", byte_A136B0[80*fileIndex], count - dword_76BEAC[fileIndex])`,
  tag stored to `byte_767EB0[count]`, then `++count`. Source matches.
- Two DEFENSIVE GUARDS in source (NOT in original, never triggered by shipped data,
  which has <=4 language columns and well-formed tokens): (1) `col >= kLabelMaxLangs`
  skips extra columns instead of overrunning the 640-byte stack arrays; (2) the gender
  null-`SkipLeadingSpaces` result falls back to the scratch field instead of the
  original's null-deref crash. Both prevent UB without changing observable output on
  valid input.

## text_load.cpp

| Addr | Function | Verdict |
|------|----------|---------|
| 0x44bb5c | VIBE_Text_BuildTextArray (.res byte layout half) | VERIFIED-1:1 (layout); compiler/UI/file-write front-end deferred = BOUNDARY |
| 0x44dba0 | VIBE_Text_LoadTextFile | VERIFIED-1:1 (path + read order); slot-table/IO = BOUNDARY |
| 0xFFFFFFFF sentinel | `SlurpStream` read-error stop | VERIFIED-1:1 |

### VERIFIED-1:1 — .res byte layout (0x44bb5c / 0x44dba0)
Header/section read ORDER in the original LoadTextFile (0x44dba0):
entryCount(@+0) -> baseIndex(slot+96,@+4) -> lastIndex(slot+100,@+8) -> offset[entryCount]
(u32) -> name[entryCount][0x50] -> tag[entryCount](u8) -> blobSize(u32) -> blob.
`BuildTextArray` consumes exactly this order/stride (offTable, names@0x50, tags, blobSize,
blob). Entry i lands at `dword_8C36B0[baseIndex+i]` — confirmed: the original stores
`dword_8C36AC[++j]` with j starting at baseIndex and `dword_8C36AC` = `dword_8C36B0 - 4`,
so the first store is `dword_8C36B0[baseIndex]`. Source places at TextDb index `baseIndex+i`
with empty placeholders filling [0,baseIndex). Names are NUL-padded 0x50 fields
(`FieldName`). VERIFIED.

### VERIFIED-1:1 — 0xFFFFFFFF sentinel
Disasm of `VIBE_Vfs_ReadStream` (0x4514ac) returns `v7 = -1` (0xFFFFFFFF) on every error
path (null handle / bad flags / null buffer) and `0` on zero-length; a good read returns
the byte count. `SlurpStream` stops on both `0` and `0xFFFFFFFFu` — faithful. VERIFIED.

### BOUNDARY (Rule 8 / rules 3-5 + data-not-in-tree)
- `Text_LoadTextFile` routes the OPEN/READ/CLOSE through the `IFileSystem`-backed VFS
  (`VfsOpenFile`/`VfsReadStream`/`VfsCloseStream`) instead of `VIBE_Vfs_OpenFile`+the OS,
  exactly as mandated. The per-file 112-byte slot records (`byte_77BEB0`), the 28-stride
  `dword_77BF10/14/18` pointer tables, the `VIBE_Memory_AllocDebug` allocations and the
  `dword_62EB24` global count are the original's module-global runtime state; the reimpl
  models the data in `TextDb` and slurps the stream into a buffer that `BuildTextArray`
  parses with the IDENTICAL byte layout. No analogue introduced — the consumed bytes match.
- `BuildTextArray`'s `.txt` source-compiler / MessageBox UI / `.res` file-WRITE side
  (2106 instrs at 0x44bb5c) stays deferred; the runtime data-model half (the `.res` ->
  TextDb read) is what is reconstructed and verified.
- The `.def` driver's include CAP differs: the original copies into a 64 KB buffer at
  stride 512 and HARD-FAILS (`return 0`) on the 129th name (after `v9 > 0x10000`); the
  pure `ParseDefinitionIncludes` helper caps cleanly at 128 (`kDefMaxIncludes`). Identical
  for <=128 includes (gilde_text.def ships far fewer); documented, not a data divergence.

## Counts
- Functions reviewed: 11 provenance'd (9 in text_definition.cpp, 2 in text_load.cpp) + sentinel.
- VERIFIED-1:1: 9.  FIXED: 2 (StrChr last-occurrence; .def last-dot strip).  BOUNDARY: as noted.
- Tests: text_definition_test (added `ParseIncludesStripsAtLastDot`), gui_text_test,
  real_text_driver_test, text_definition_e2e_test, real_text_driver_e2e_test — ALL PASS.
