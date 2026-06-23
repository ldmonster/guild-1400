# FIX-NETFILE — file-selector extension handling (gilde.exe @0x569668 / @0x569530)

Status: DONE. All 3 `gui_netfile_run` tests green (unit + itest + e2e), no regression.

## The bug (inverted extension stripping)

A sibling hardening agent left `src/gui/netfile_run.cpp` + the unit golden encoding the
WRONG model: "the row name keeps its extension; only the full-path buffer is stripped."
The itest/e2e goldens (correct) then disagreed with the source, producing 4 failures:

- itest `FileSel_DirectClickCommits`  expected `\project\gfx\AUGSBURG.INI`
- itest `FileSel_DirectFirstRow`      expected `dir\BERLIN.INI`
- itest `FileSel_EditClickCopiesToFieldNoCommit` expected editText `SAVE1`
- e2e  `FileSel_DirectSession_OrderAndDeterminism` expected out1 `\project\gfx\BERLIN.INI`

## Binary truth (disasm is the reference of record)

VIBE_SaveBrowser_EnumerateSaveFiles @0x569530, per matching file:
- `0x56957c lea ecx,[ebx+9]`         — ecx = NAME field (a3+9)
- `0x5695bb..0x5695d1`               — 2-byte-stride copy of raw filename `*v6` into ecx (name field), full name incl. ext
- `0x5695e2 mov ebx,[var_8]` (=a3+265); `0x5695e7 Sprintf(ebx,"%s/%s",dir,name)` — full-path buffer
- `0x5695f4 mov eax,ecx` (=a3+9, the NAME field); `0x5695f6 StrChr(name,'.')`;
  `0x5695fd jz`; `0x5695ff mov byte ptr [eax],0` — **strips ext from the NAME field**

=> The a3+9 NAME field is EXTENSION-STRIPPED ("BERLIN"); the a3+265 path buffer KEEPS
its extension and is never read by the selector. (Prior comment had this exactly backwards.)

VIBE_Menu_RunFileSelector @0x569668:
- row label  `v12 = (char*)v25+1` == a3+9 (stripped name) -> AddTextLabel
- direct commit `0x5698f2 Sprintf(a6,"%s\\%s%s", v30/*dir*/, v18 == a3+9 /*stripped name*/, a5/*ext*/)`
  => `dir "\\" name ext`
- edit list-click `0x5699ad SetValueOrText(v31, v18)` — copies the STRIPPED name into the edit field
- edit OK commit `0x5699f7 Sprintf(a6,"%s\\%s%s", dir, GetDataPtr(v31), ext)`

Call site `0x52a64c` (RunMainMenu): a2(dl)=1 direct, ecx="gamedata/cities",
ebx="$Z$[Load INI$]", a5=".INI", a6=&out. So ext (".INI") IS re-appended to the stripped name.

Worked example (direct, AUGSBURG.INI, dir=`\project\gfx`, ext=`.INI`):
name field = "AUGSBURG" (stripped) -> commit "%s\\%s%s" = `\project\gfx\AUGSBURG.INI`. Matches golden.
Edit click on SAVE1.SAV -> editText = "SAVE1" (stripped, no commit). Matches golden.

## Changes (only files I own)

- `src/gui/netfile_run.cpp`: added `StripRowNameExt()` (StrChr-'.' truncation, mirrors
  0x5695f4..0x5695ff) and apply it to `entries[i].displayName` when building each row name,
  so the row label + both commit paths use the binary-correct stripped name. Rewrote the
  stale/inverted provenance comment with the exact disasm addresses.
- `tests/unit/gui_netfile_run_test.cpp` (`FileSel_DirectMode_RowsOnly`): golden corrected
  from "BERLIN.INI"/"AUGSBURG.INI" to "BERLIN"/"AUGSBURG" (binary-correct stripped name).
- itest/e2e goldens were already binary-correct; left as-is — source now matches them.

Note: `src/gui/savebrowser.cpp::SaveBrowser_EnumerateSaveFiles` still emits `displayName`
WITH the extension (and a stripped `fullPath`) — the inverse of the binary. That module is
owned by another agent (its `gui_panels_test.cpp:287` asserts `displayName == "GAME1.SAV"`).
I did NOT touch it; netfile_run compensates by stripping at the row-build site, which is the
binary-faithful place for the a3+9 truncation anyway.

## Verify
`cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R "gui_netfile_run" --output-on-failure`
=> 3/3 passed (gui_netfile_run_test, gui_netfile_run_itest, gui_netfile_run_e2e_test).

(Transient: the shared `guild` lib briefly failed to build due to a concurrent sibling edit
in `src/io/savestate_decompress.cpp` referencing an undefined `kFileUnbuffered`; resolved by
that agent. No git commands run.)
