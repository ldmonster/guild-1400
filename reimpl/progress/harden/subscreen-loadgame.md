# Sub-screen 1:1 VIEW — Load Game (`Загрузить игру`)

Screen fn: **VIBE_Menu_RunLoadGame @0x56a270**. This hardens the *view* of the
load-game sub-screen to the real 1:1 render (real background, real form
geometry, real `_FONT`/CP1251 localized labels). The data/flow/result contract
was already 1:1-pinned and is untouched.

## What changed

`src/play/sdl_loadgame_screen.cpp` / `.h`:

1. **Background** — now `_OPTIONEN_PIC` (gfx #1770, 800×600), the options/menu
   sub-screen backdrop, with res variants `_OPTIONEN_PIC_1024`/`_1152` chosen by
   `fbW` (falls back to the base 800×600 image if a variant is absent). Was the
   default `_MENUE_BACKGROUND`. Drawn full-screen via `MenuAssets::Load(..., bgName)`
   + `DrawBackground`.
2. **NATIVE + CENTER-TRANSLATE** — `LoadGameComputeLayout` no longer scales widget
   sizes by `fbW/800`/`fbH/600`. It computes `ox=(W-800)/2, oy=(H-600)/2`
   (PositionAtCoord mode-2 @0x41d964) and lays every widget at its real form
   coordinate + (ox,oy). Body window `px/py/pw/ph` = the real WIN1 rect
   `(168,184,390,493)`; row stride `rowH = 16` (the real `AddChildWindow h=16`,
   not a scaled value). New `ox/oy` fields on `LoadGameScreenLayout`.
3. **Slot list** — one row per save slot inside body WIN1, real localized text in
   the real `_FONT` (CP1251) at native scale 1: occupied rows show the in-game
   save name (row+25, the `AddTextLabel` text @0x569ebd); empty rows show the
   localized placeholder `_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER` sprintf'd with the
   row index (dword_8C9A04 fmt @0x56a0fb, "Свободный слот %i"). Hover/selection
   tint the row band for pointer feedback.
4. **Parchment panel** — body backing is `_TOOL_TIP_BIG` shape0 (#1784, 475×301
   parchment) centered over WIN1 when art is present; flat plate otherwise.
5. **Title** — rich string `0x1864` (`_OPTIONEN_MENUE_LOAD`, "Загрузить игру")
   centered, real `_FONT`.
6. **Confirm overlay** — `_OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE` (RunMessageBox
   257, byte_63CC40 gate) drawn in the real font.
7. **No OK/Back buttons** — confirmed from the disasm: `Menu_RunLoadGame` builds
   **no** button row (no `Hud_BuildButtonRow`, no `_OPTIONEN_BUTTONS`). The real
   screen closes on ESC (`dword_672230`) and loads on a row click. A synthetic
   back hit box is kept off the visible area only for the host/tests; it is not
   rendered as a button.
8. New texts `slotInfo0/slotInfo1` (`_OPTIONEN_LOAD_GAME_SLOT_INFO+0/+1`, rich
   strings 0x18D2/0x18D3) resolved by name for the occupied-row info columns.

Asset-less / headless path is preserved: when `gameDir` is empty or the
gfx/font are absent, the screen falls back to the flat plate + 6px ASCII raster,
so all headless tests still pass.

## Addresses disasm'd (IDA MCP, module gilde.exe)

| addr | fn | what it confirmed |
|------|----|-------------------|
| 0x56a270 | VIBE_Menu_RunLoadGame | form `menu\loadgame_new`; PositionAtCoord(mode 2); title rich 0x1864 in window 2; `Hud_BuildSliderPanel(532,360,win1,…,130)`; **no button row**; ESC=`dword_672230`; click=`dword_672228`; `Gamedata\Saves\%s.SAV`→byte_122F530; word_63C740=10 |
| 0x569d00 | VIBE_SaveBrowser_LoadSlotMetadata | 16×544 table; enumerate cap 16; (hdr+4)&2 drop; rows = `AddChildWindow(0,130*slot,130,winW-16,16,…)` h=16 + `AddTextLabel(168,…)`; occupied text=name (row+25); empty fmt=dword_8C9A04; rich 0x18D2/0x18D3/0x18D4 |
| 0x4bd388 | VIBE_Hud_BuildSliderPanel | the slot-list container set up in WIN1 |
| 0x41a598 | VIBE_Window_AddChildWindow | arg order (x=ax, y=dx, w=cx, h=bx, …, parent) → confirmed 16px row height |

Form rects (WIN0 128,136,449,575 flags0x11; WIN1 168,184,390,493 parentIdx1)
from MENU-SUBSCREENS-GROUNDTRUTH (forms.BIN FRM2).

## Preserved (unchanged contract)

`kLoadGameSlotCount = 16`, `kLoadGameSlotWidgetBase = 0x4C00` (+4/+8 per row),
`LoadGameScreenResult` (savePath/loadPath/saveName/sessionFlags), the
confirm/cancel semantics, `sessionFlags = 10` on a confirmed pick, the strict
(hdr+4)&2 partial gate + 16-file cap in `LoadGameBuildSlotViews`.

## Tests

`tests/unit/sdl_loadgame_screen_test.cpp` — **299 checks, 0 failures**
(`GUILD_GAME_DIR` set). Added:
- `NativeFormGeometry` — pins `ox/oy=0` at 800×600, WIN1 rect (168,184,390,493),
  `rowH=16`, and that 1024×768 center-translates without scaling widget sizes.
- `RealSlotInfoCaptionsResolve` — `_OPTIONEN_LOAD_GAME_SLOT_INFO+0/+1` and the
  `..._LEER` %i format resolve from the shipped textbin (asset-guarded, skips
  cleanly without `GUILD_GAME_DIR`).

`RealTextsLoadWhenAssetsPresent` confirms real CP1251 strings:
`title='Загрузить игру'`, `empty='Свободный слот %i'`.

`tests/integration/menu_loadgame_flow_itest.cpp` — **26 checks, 0 failures** (kept green).
`tests/unit/oneone_screens_wave14_test.cpp` — **72 checks, 0 failures** (kept green;
`kLoadGameSlotCount=16` / `kLoadGameSlotWidgetBase=0x4C00` pins intact).

## Build status

`cmake --build build --target sdl_loadgame_screen_test menu_loadgame_flow_itest
oneone_screens_wave14_test -j` — all three build clean, no warnings.

## Deferred (named, not faked — rule 8)

- The 160×120 save thumbnails (word_13CED76 raw-bitmap widgets) and the rich-text
  date/wealth info columns 0x18D2/0x18D3 are not pixel-reconstructed; the row info
  captions are resolved (slotInfo0/1) but the date/wealth values come from the
  thumbnail/header blob that is still a named gap (see progress/session-save.md).
- Scroll behaviour of the slider panel (Hud_BuildSliderPanel scroll) is not
  modelled; all 16 rows render statically (they fit the 493px body at 16px stride).

---

## UPDATE — full 1:1 view (thumbnails + scrollable rows + scrollbar)

User feedback: "menu on Загрузить игру not 1:1". Re-disassembled the real screen and
fixed the VIEW, which had been deferred (flat 16px text rows).

Disassembled:
- `VIBE_Menu_RunLoadGame @0x56a270` — confirmed `Hud_BuildSliderPanel(532,360,..)` (the
  scrollbar) + `SaveBrowser_LoadSlotMetadata` build.
- `VIBE_SaveBrowser_LoadSlotMetadata @0x569d00` — the REAL per-row layout: each occupied
  slot is a **130px-tall** child window (`AddChildWindow(0, 130*slot, ..)`) holding a
  **160×120 RGB save thumbnail** (`Widget_AddRawBitmapToWindow(0,0,160,word_13CED76)`)
  plus the save name label at **x+168** (`Object_AddTextLabel(168,..)`, gold color 66)
  and a date/wealth sub-line (rich `0x18D2`/`0x18D3`); empty slots show a blank thumbnail
  box + "Свободный слот N" + `0x18D4`. The 16 rows scroll inside the body viewport.
- `VIBE_Window_AddChildWindow @0x41a598` — arg order (x,y,w,h,flags,win); confirmed the
  130px y-stride and the body-relative offsets.
- `io::SaveLoadHeaderAndThumbnail @0x5a7af0` already returns the raw 0xE100 (160×120×3)
  thumbnail — the old code passed `nullptr` and discarded it.

Implemented (sdl_loadgame_screen.{h,cpp}):
- Decode each save's 160×120 thumbnail + capture header playerName(+0x88)/wealth(+0x54).
- New layout: 130px rows, 160×120 thumbnail at the row left, name+info at x+168, a
  scroll viewport clipped to the screen, a scrollbar at x=532 (`Hud_BuildSliderPanel`),
  scroll-follows-selection. Native size + center-translate only (never scaled).
- Render: `_OPTIONEN_PIC` background, dark panel, real thumbnails (blank box for empty),
  gold save name, date/wealth line, localized empty-slot caption, gold title `0x1864`.
- Tests updated to the new API (RowRect(i,scroll,..)/HitRow(..,scroll); 800×600 native).
  Visual verified over real assets (GUILD_LOADGAME_DUMP) — 16 scrollable rows with
  thumbnail placeholders + "Свободный слот N" + scrollbar.

Suite: 1552/1552 green.
