# Wave-16 TRUE 1:1 binary diff — W16-SCREENS (menu + screen flow)

MCP was LIVE. Decompiled every screen-flow function in the brief and compared
line-for-line against the `src/play/` reconstruction (and the `gui/` run-logic it
drives, read-only since those are other clusters). **Result: the whole cluster is
VERIFIED-1:1 — no divergences found, no source/golden changes required.** All
cluster tests rebuilt and pass in the normal `build/`.

## Functions decompiled + verified this wave

| addr | function | verdict |
|------|----------|---------|
| 0x529d08 | `VIBE_Menu_RunMainMenu` | VERIFIED-1:1 (button layout + dispatch) |
| 0x534bbc | `VIBE_GameLogic_MainEntryAndShutdown` (outer loop) | VERIFIED-1:1 (mode_fsm priority) |
| 0x52e4e0 | `VIBE_Menu_ChooseCharacterIntroVariant` (difficulty radio) | VERIFIED-1:1 |
| 0x52d684 | `VIBE_Menu_RunChooseHistory` (history picker) | VERIFIED-1:1 |
| 0x52e3d8 | `VIBE_Menu_ChooseCharacterIntro` (spine router) | VERIFIED-1:1 |
| 0x52ccd8 | `VIBE_Menu_RunChoosePlayer` (identity wizard) | VERIFIED-1:1 |
| 0x569d00 | `VIBE_SaveBrowser_LoadSlotMetadata` (loadgame slots) | VERIFIED-1:1 |
| 0x56cc44 | `VIBE_Menu_RunOptionsGame` | VERIFIED-1:1 (ranges + save order) |
| 0x56c21c | `VIBE_Menu_RunOptionsGfx` | VERIFIED-1:1 (ranges + inverted gamma) |
| 0x56c808 | `VIBE_Menu_RunOptionsSfx` | VERIFIED-1:1 (ranges) |

## Detailed line-for-line confirmations

### Main menu layout (0x529d08) — `gui/main_menu.cpp` + `native_main_menu.cpp`
- The eight `VIBE_Widget_AddSpriteToWindow(32, Y, 174, …)` calls: x = **32**
  (`kMainMenuButtonX`), sprite id **174** (`_BUTTON_RED`), y-table
  **{10,53,96,139,182,225,268,311}** — matches `kMainMenuButtons` exactly (43px
  pitch). `RadioGroup_Create(8, …)` ⇒ 8 buttons. Item→widget mapping verified:
  v80(y=10)=New Game(EnterChooseCity), v85(y=53)=Load(RunLoadGame),
  v82(y=96)=Multiplayer(ChooseNetworkMode), v73(y=139)=Game Options(RunOptionsGame),
  v86(y=182)=Gfx Options(RunOptionsGfx), v87(y=225)=Sfx Options(RunOptionsSfx),
  v83(y=268)=Credits(RunCreditsScroll), v84(y=311)=Quit
  (dword_63CC30=1/dword_63CC48=1). The table in `gui/main_menu.cpp` matches.
- `MenuButtonScreenRect` (`native_main_menu.cpp:458`): the `(int)(x*sx+0.5)`
  round-to-nearest is **host framebuffer upscale of the fixed design layout**
  (rule 3 presentation), NOT an engine float→int. The original positions the form
  with `Window_PositionAtCoord_Thunk(form, 3)` (mode-3 centre) at native res with
  integer-literal coordinates — there is **no engine ConvertX / truncation site in
  the menu layout** to model. Round-to-nearest is correct for a scale factor.

### Difficulty radio (0x52e4e0)
6-member `RadioGroup_Create(6, id0)`; seed `dword_63C744 = (u8)byte_12335BA`;
OK(1210)/Enter(28) commits the active member k∈{0..4} (member 6 is decoration);
close/ESC sets dword_631614=1 ret 0; back(1155) ret 0; persists
`byte_12335BA = dword_63C744`. Matches `gui/choosecharacter_intro_run` +
`sdl_charintro_screen` exactly.

### History picker (0x52d684)
`RadioGroup_Create(4, …)`; seed `dword_12335AC` switch **{0→2, 1→0, 2→1}**;
commit id0→flag1, id1→flag2, **id2 OR Enter(28)→flag0** (`v34 || byte_67225C==28`);
re-select `History_SetActiveFlag`→idx via `dword_633920>>24` {0→2,1→0,2→1}; back
1155. Matches `gui/choosehistory_run` (SeedIndex / FlagToIndex / flag mapping).
The post-pick character spine (Dialog→ChoosePlayer→ChooseCharacterIntro→
ChooseCharacter|ChooseProfession, with the v9/v10 retry) is host-boundary modeled
(`RunCharacterSpine`) as documented — control flow matches the decompile.

### Spine router (0x52e3d8)
Radio-of-2 visual; return is purely button-driven: v2 inits **0**, OK(1210)/Enter→
**+1**, close/ESC(1)→**−1**, back(1155)→stays 0. Matches
`Menu_RunChooseCharacterIntro`.

### Player wizard (0x52ccd8)
Page machine v2∈0..5 over the 7-window `Menu\CHOOSEPLAYER` form: page0 Vorname
(text), 1 Nachname (text), 2 Geschlecht (radio2), 3 Glauben (radio2), 4 Wappen
(8 buttons, value=1342+i), 5 confirm (auto-commit, v70=1). `[Network]` identity
read on entry (`Wappen+1342`, Geschlecht, Glauben) and written back on exit.
Close/back steps a page when v2>0 else exits 0. Matches `gui/chooseplayer_run`
(pageCount 6, confirmPage 5, wappenCount 8) + `sdl_chooseplayer_screen`.

### Loadgame slot metadata (0x569d00)
16×544-byte slot table; rows +4/+8 init −1; enumerate then **cap 16** (0x569d64);
per file VFS "rb" + `Save_LoadHeaderAndThumbnail`; **drop `(hdr+4)&2`** partial
saves (0x569e1a); `FindSaveSlot` placement; stamp +8/+4 window-id, +12=1, name
copy to +25/+153; empty rows get the `dword_8C9A04` `_..._SLOT_INFO_LEER` fmt with
the row index. All load-bearing logic matches `LoadGameBuildSlotViews`. (Recovered
detail: the occupied-slot info panel is `AddChildWindow(0, 130*slot, 130, …)` and
the empty-slot panel `…, 130*slot, 120, …`; thumbnails are `word_13CED76` raw
160-wide bitmaps gated on `dword_649D4C >= 0x10028`; info columns via 0x18D2/0x18D3
rich strings with the `"%i%c"` day format — these are the **form chrome /
thumbnail / info-column items already documented as deferred** in
progress/menu-loadgame.md, not divergences.)

### Options ranges, save-back order, inverted gamma (0x56cc44 / 0x56c21c / 0x56c808)
Every `SetValueOrText(child, min, max, value)` range verified against
`OptionsRowsFor`, and every OK-path `GetDataPtr`→global write verified against
`RowsToConfig`, in the originals' child build order:
- **Game**: speed 0..160, mouse 0..500, scroll 0..100, camera 0..100, invert
  (built→hidden→forced 0), show-cursor/show-building 0..1, panel-mode 0..4,
  help-events/hints/panel-help 0..1. Save-back globals + order match.
- **Gfx**: cur_res 0..{0|1|2 from caps}, details/texture/floor-lod/shadow/
  camera-limits 0..2, mipmaps/lod-handling 0..1, gamma slider **`SetValueOrText(50,
  100, 100 − fogPlane)`** with save-back **`fogPlane = 50 − (live − 50)`** — the
  reconstruction's inverted-gamma seed/save match byte-for-byte. The two-step
  cur_res re-apply (WriteGfxSettings → ApplyGfxSettings → on res change, write
  again → ReadGfxAndSoundSettings) matches `RunOptionsScreen`'s kGfx OK path.
- **Sfx**: master/sfx/msx/speech 0..127, msx_freq 0..4 (5 quality lines). Match.

`OptionKind::kStep/kCycle` "step" increments (16/50/10/…) and the
`ActuateValue` wrap-to-min are **host click-to-step affordances** — the original
uses a continuous scroll widget (`SetScrollLimit`) with no discrete step; the
persisted byte/dword values and their ranges are byte-identical. Documented, not a
divergence.

`VIBE_Coord_ConvertX` **is** used in the Sfx live drag-preview
(0x56cadf: `v14 = v22*v23; ConvertX; (int)v14`) — it truncates — but that is the
**live audio-preview** application while dragging, not a persisted/observable
value; the native screen has no live float-volume preview, so nothing to model and
no truncate-vs-round divergence in the cluster.

### mode_fsm transition table (0x534bbc outer loop)
The real outer loop (0x535358): `RunMainMenu` → if `dword_63CC38`
(restart-display) break+re-init display → else at LABEL_97 `if (word_63C740 &&
!dword_63CC48)` run `InitOrLoadSession` → `if (dword_63CC48)` quit (terminal).
Priority **restart-display, run-session, quit**. `ModeFsm::NextMode` lifts this via
`app::MenuMainDecide` (kQuit / kRestartDisplay→MainMenu / kRunSession→
ModeForSessionFlags / kRestartMenu) — faithful. `ModeForSessionFlags` priority
NewGame(&1) > LoadSave(&2) > Network(&4): verified against the arming sites — New
Game `word_63C740 |= 1`, Load `word_63C740 = 10` (kLoadSave|kHistory), Network &4.
VERIFIED-1:1.

## Wave-12 NEEDS-MCP queue
The wave-12 hardening report recorded **"None found — no behavioral MCP question"**
for this cluster. This wave confirms that: every wave-12 memory-safety bound left
the observable output on valid input byte-identical, and the now-live decompile
shows no control-flow/constant/table divergence anywhere in the cluster.

## Build / tests (normal build/, all green)
Rebuilt + ran the cluster targets:
- mode_fsm_test — 66 checks
- sdl_options_screen_test — 114 checks
- sdl_loadgame_screen_test — 275 checks
- chooseplayer_run_test — 20 checks
- choosehistory_run_test — 44 checks
- charintro_markup_test — 21 checks
- menu_recon_transition_test — 75 checks

All pass, 0 failures. No source or golden changes were needed (the cluster was
already 1:1); this wave is a verification pass that closes the screen-flow diff.
