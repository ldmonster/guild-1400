# Main menu — Load Game (native save browser)

The main menu's **Load Game** path, native and playable: the menu's Load button
now opens a native save-browser screen that lists the `.SAV` files in the save
directory through `shim::IFileSystem`, and a confirmed pick is returned to the
menu caller in `NativeMenuResult` (action `kLoadGame` + the chosen save's
paths). The flow logic underneath is the 1:1 reconstruction
`gui::Menu_RunLoadGame` (@0x56a270, pre-existing in `gui/loadgame_run`); this
wave delivers its native host + the menu wiring.

## Recovered original flow (IDA, gilde.exe)

| addr | function | role |
|------|----------|------|
| 0x529d08 | `VIBE_Menu_RunMainMenu` | the Load button is the y=53 sprite widget (v85/v76); its dispatch @0x52a82d: hide the form, `if (VIBE_Menu_RunLoadGame()) dword_631614 = 1`, re-show (LABEL_55) |
| 0x56a270 | `VIBE_Menu_RunLoadGame` | form `"menu\loadgame_new"`; title rich text 0x1864 (`_OPTIONEN_MENUE_LOAD`); `Hud_BuildSliderPanel(532,360,win,130)`; `SaveBrowser_LoadSlotMetadata(form,1,scratch,slots,"gamedata/saves",0)`; frame loop: `dword_672230` -> `dword_631614=1`; click edge (`dword_672228`) -> walk the 16 x 544-byte slot rows, match the hovered widget (`dword_62D22C == *(row+4)`, occupied `row[12]`, `*(row+8) != -1`); gated by `byte_63CC40` -> `Dialog_RunMessageBox(dword_8C9A08, 257)`; on accept sprintf `"Gamedata\\Saves\\%s.SAV"` (@0x624f40) from `&row[25]` into `byte_122F530`, `word_63C740 = 10`, `dword_631614 = 1`, return 1 |
| 0x569d00 | `VIBE_SaveBrowser_LoadSlotMetadata` | init 16 rows (+4/+8 = -1); enumerate; **cap 16 files** (0x569d64); per file: VFS `"rb"` open + `Save_LoadHeaderAndThumbnail`; **drop `(hdr+4) & 2`** (partial-format saves, 0x569e1a); `FindSaveSlot` placement; stamp row ids/occupied (+8 window id, +4 label id, +12 = 1), record copy -> row+16 (name @ +25), display name -> +153; empty rows get the `dword_8C9A04` text sprintf'd with the row index (`_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER`) + rich texts 0x18D2/0x18D3 (info) / 0x18D4 (empty) |
| 0x569530 | `VIBE_SaveBrowser_EnumerateSaveFiles` | already reconstructed (`io/save_browser`) — `".SAV"` filter (@0x624f08) over `"gamedata/saves"` (@0x624f30), VFS-tree directory order |
| 0x569c50 | `VIBE_SaveBrowser_FindSaveSlot` | already reconstructed (`io/save_browser`) — name `"QUICKSAVE"` -> row 1, `"AUTOSAVE"` -> row 0, else the header slot tag `byte_649D50` (2..15); tag 1 with a non-QUICKSAVE name, tag 0 on a non-reserved name, and occupied rows are rejected (-1) |
| 0x5a7af0 | `VIBE_Save_LoadHeaderAndThumbnail` | already reconstructed (`io/save`) — supplies the header name/version/slot-tag/flag byte the slot build consumes |

Localized texts (recovered entry names, loaded by NAME from
`textbin_deutsch.BIN`): title `_OPTIONEN_MENUE_LOAD+0` (rich id 0x1864),
empty-slot `_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+0` (`dword_8C9A04` fmt, `%i` =
row index), confirm box `_OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE+0`
(`dword_8C9A08`, box id 257), per-save info `_OPTIONEN_LOAD_GAME_SLOT_INFO`
(0x18D2/0x18D3 — deferred, see gaps).

## Files

| file | contents |
|------|----------|
| `src/play/sdl_loadgame_screen.{h,cpp}` | **NEW** — the native screen. Drives `gui::Menu_RunLoadGame` (@0x56a270) through its hooks: `LoadGameBuildSlotViews` mirrors @0x569d00's load-bearing slot build over `shim::IFileSystem` via the REAL reconstructed leaves (`io::SaveBrowserEnumerateSaveFiles` @0x569530 over the VFS tree, `io::SaveLoadHeaderAndThumbnail` @0x5a7af0, `io::SaveBrowserFindSaveSlot` @0x569c50 over the real 544-byte table, the 16-file cap, the `(hdr+4)&2` gate); per-frame hooks feed click-edge / hovered-slot-widget / close from SDL mouse+keys; renders the real localized `_OPTIONEN_*` texts (16 slot rows + back row), presents through `IGraphicsDevice`. Mouse click, Up/Down + Enter, ESC cancel, optional `byte_63CC40` confirm overlay |
| `src/play/native_main_menu.{h,cpp}` | wired — `NativeMenuHooks::RunLoadGame` now runs the screen (was a first-city stand-in); ADDITIVE `NativeMenuResult` API: `Action::kLoadGame` (appended), `savePath` (real openable path, e.g. `"Resources/gamedata/Saves/Quicksave.SAV"`), `saveLoadPath` (the 1:1 `byte_122F530` string `"Gamedata\\Saves\\<name>.SAV"`), `saveName` (header name). Existing fields/values untouched |
| `src/play/sdl_menu.cpp` | owned this wave; **no change needed** — its `SdlMenuChoice::kLoadGame` mapping already matches the dispatch |

Reused (extern, never redefined): `gui/loadgame_run` (`Menu_RunLoadGame`
@0x56a270 — the pre-existing 1:1 body + its `LoadGameRun_MatchSlot` slot scan),
`gui/loadgame` (`LoadGame_BuildPath` — the @0x56a3f5 sprintf), `io/save_browser`,
`io/save`, `io/vfs`, `io/vfs_tree`, `io/archive_mount`, `gui/text_load` +
`gui/text/textdb`, `play/session_save` (`kSaveBrowseDir`), `play/menu_assets`,
`render/text_raster`.

## Tests

* `tests/unit/sdl_loadgame_screen_test.cpp` — suite **SdlLoadGameScreen**, 9 cases:
  `SlotBuild_PlacementRules` (QUICKSAVE->1 / AUTOSAVE->0 / tag rows / duplicate +
  tag-0 + fake-QUICKSAVE rejection / `.SAV` filter / corrupt-header skip),
  `SlotBuild_PartialGateAndFileCap` (the 1:1 `(hdr+4)&2` drop vs the bridge
  default; the 16-file cap with 18 seeded saves), `MousePickConfirms` (the full
  result protocol: `loadPath == "Gamedata\Saves\QUICKSAVE.SAV"`, real `savePath`,
  `word_63C740 == 10`), `ClickOnEmptyRowDoesNothing`, `KeyboardPickConfirms`
  (Down skips empty rows, Enter confirms), `EscCancels`,
  `EmptySaveDirListsNothing`, `ConfirmGateAcceptAndCancel` (box-257 gate),
  `LayoutHitTestMatchesRows` (800x600 + 320x240).
* `tests/integration/menu_loadgame_flow_itest.cpp` — suite **MenuLoadGameFlow**,
  3 cases: `PickSaveFromMenu` (menu -> Load click -> slot pick ->
  `NativeMenuResult{kLoadGame, savePath, saveLoadPath, saveName}`),
  `CancelReturnsToMenu` (ESC in the browser falls back to the menu — the
  LABEL_55 re-show — then Quit works), `NoSavesCancelKeepsMenuAlive`.
* Regression: `native_main_menu_itest`, `sdl_menu_test`, `sdl_menu_itest`,
  `gui_loadgame_run_test`, `gui_loadgame_run_itest`, `session_save_test` re-run
  clean (counts in the wave report).

## Named gaps / bridges (rule 8 — documented, not faked)

* **Partial-save gate bridge**: the original browser DROPS partial-format saves
  (`(hdr+4) & 2`, @0x569e1a). Today `play::SaveLiveWorld` writes exactly that
  partial format because the FULL `.SAV` tail is still a named gap
  (progress/session-save.md) — with the strict gate our own quicksaves would
  never list. `LoadGameScreenConfig::includePartialSaves` (default **true**)
  admits them; `false` is the byte-faithful gate (unit-tested both ways).
  Remove the toggle when the FULL save tail lands.
* **Form chrome / thumbnails / info columns**: the pixel-exact
  `menu\loadgame_new` form window, the 160x120 save thumbnails
  (`word_13CED76` raw-bitmap widgets @0x569f61) and the rich-text date/wealth
  info columns (0x18D2/0x18D3 with the `"%i%c"` day formatting @0x56a008) are
  deferred with the forms/rich-text modules — the screen renders the real
  localized title/labels over the reconstructed panel style the sibling native
  screens use.
* **Secondary obj-id match branch** (@0x56a444..0x56a479, `dword_75BF08`
  hover-retarget): modeled in `gui::LoadGameRun_MatchSlot` as documented there
  (it never selects a different row); unchanged this wave.

## Wave-2 session contract (documented, NOT implemented here)

When `RunNativeMainMenu` returns `action == NativeMenuResult::kLoadGame`:

* `savePath` is the picked save's real-cased path **relative to the same root
  the menu's `IFileSystem` uses** (gameDir on the live app), e.g.
  `"Resources/gamedata/Saves/Quicksave.SAV"` — feed it to
  `play::LoadLiveWorld(fs, savePath)` (the quickload machinery) to restore the
  live world, then enter the session WITHOUT `ApplyNewGameParams`
  (`params.started` is NOT armed on this path).
* `saveLoadPath` carries the original's `byte_122F530` string 1:1
  (`"Gamedata\\Saves\\<name>.SAV"`) and `saveName` the header name, for parity
  logging / future full-session dispatch (`word_63C740 == 10` was already set
  inside the gui run).
* Until wave-2 lands, `apps/guild_run.cpp` treats the result as "not kPlayCity"
  and loops back to the menu (no behavior break).
