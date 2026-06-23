# Main-menu labels + variable-width buttons (1:1)

Agent: MENU-LABELS-DIMS. Makes the main-menu buttons genuinely 1:1 — real
localized labels from the install's TextDb, real `_FONT` glyph rendering, and
per-label variable button widths from the engine's own metrics.

Owned: `src/play/native_main_menu.{cpp,h}`, `src/play/menu_assets.{cpp,h}`,
`tests/unit/menu_font_test.cpp`, `tests/unit/oneone_screens_wave14_test.cpp`.

---

## 1. Button -> label mapping (gilde.exe 0x529d08)

`VIBE_Menu_RunMainMenu @0x529d08` builds the 8 sprite buttons with
`VIBE_Widget_AddSpriteToWindow(x=32, y=row, gfx=0xAE/174=_BUTTON_RED, label=ecx, win)`.
The label arg (ecx) of each call is a `char*` read from a fixed global slot
(verified in the disasm at 0x529efe..0x52a0da). Those globals are slots of one
contiguous localized-string array at `0x8C9854` (the SAME array the in-game
options menu `VIBE_Menu_RunOptionsMain @0x56dccc` reads, 0x56dd63..; and the gfx
options `@0x56c21c` reads its tail 0x8C9888+). The array is the `_OPTIONEN_MENUE_*`
group of `Text_O_Optionen.res` in file order (no name strings exist in the EXE;
the strings are looked up by index — the array slot order matches the .res order
1:1, confirmed across the three readers).

| row y | global       | array idx | text key (Text_O_Optionen) | RU string        | btn W |
|------:|--------------|----------:|----------------------------|------------------|------:|
| 10    | dword_8C9870 | 7         | `_OPTIONEN_MENUE_NEW+0`       | Нова° игра      | 121  |
| 53    | dword_8C9854 | 0         | `_OPTIONEN_MENUE_LOAD+0`      | Загрузить игру  | 149  |
| 96    | dword_8C9874 | 8         | `_OPTIONEN_MENUE_NETWORK+0`   | Сетева° игра    | 134  |
| 139   | dword_8C985C | 2         | `_OPTIONEN_MENUE_GAME+0`      | Настройки игры  | 158  |
| 182   | dword_8C9860 | 3         | `_OPTIONEN_MENUE_GFX+0`       | Настройки графики | 184 |
| 225   | dword_8C9864 | 4         | `_OPTIONEN_MENUE_SFX+0`       | Настройки звука | 161  |
| 268   | dword_8C9878 | 9         | `_OPTIONEN_MENUE_CREDITS+0`   | Авторы          | 94   |
| 311   | dword_8C987C | 10        | `_OPTIONEN_MENUE_PROG_EXIT+0` | Выход из игры   | 154  |

(array idx = (addr - 0x8C9854)/4; idx0=LOAD, 1=SAVE, 2=GAME, 3=GFX, 4=SFX,
5=ZURUECK, 6=EXIT, 7=NEW, 8=NETWORK, 9=CREDITS, 10=PROG_EXIT — exactly the
`_OPTIONEN_MENUE_*` order in Text_O_Optionen, base index 6244.)

Labels are resolved AT RUNTIME from `Resources/textbin_deutsch.BIN` (fallback
`textbin.BIN`) via `io::ArchiveMount` + `gui::text::BuildTextArray` +
`TextDb::FindIndex("_OPTIONEN_MENUE_*")` — the install's localized strings (this
install is the Russian build, title "ГИЛЬДИЯ"); CP1251. Nothing is hardcoded.
Implemented in `play::ResolveMainMenuLabels`.

The `°` shown above is the CP1251 byte 0xFF as it round-trips through the python
dump; the real byte is used unchanged by the width calc and renderer.

## 2. _FONT metrics + VIBE_Property_Get (0x4152cc)

`_FONT` = gfx record 66 in `gfx/gilde.gfx` (off 444638, size 151073). It is a
SHAPBANK with 255 shapes. Glyph for char code `ch` == shape `ch` directly:
`VIBE_Coord_Transform @0x5d8b00` returns `blob + *(u32*)(blob + 0x45 + 4*ch)`,
and `0x45` is the SHAPBANK shape-offset table — so CP1251 (0xC0..0xFF) maps to
shapes 192..255. Per-glyph metric fields in the shape header:

* `+6`  (u16) width    (= advance + 1; the drawn bitmap width)
* `+22` (u16) kern     (leftBearing, subtracted)
* `+26` (u16) advance  (pen step)
* `font+46` (u16)      line height (dword_69FFB0)

Spacing globals (statically initialised in the IDB): `dword_62D274 = 2`
(tracking), `dword_62D270 = 8` (space extra).

`VIBE_Property_Get @0x4152cc` (text pixel width) reconstructed 1:1 in
`MenuFont::MeasureWidth`: for each char (skip '~'==126): if i>0 and prev != ' '
subtract kern; if ' ' add `2 + 8 + adv`, else add `2 + adv`; finally add a
trailing `2`.

`VIBE_Property_Set @0x4159dc` (the draw pen) reconstructed in `MenuFont::DrawText`:
kern subtracted on EVERY char; glyph blitted at the running pen; advance after;
space adds 8. 13 of the 255 glyphs carry nonzero kern (e.g. 'g'=2, 'p'/'s'=1,
Cyrillic 'а'=2), which the measurement already accounts for. Glyphs are rendered
from their real RLE shape bitmaps (recoloured to the text colour, alpha-keyed),
not a fake font.

## 3. Variable-width buttons (VIBE_Object_RecomputeSize 0x41b164)

Sprite kind 9 (the `_BUTTON_RED` label button): width =
`VIBE_Property_Get(label) + capL + capR + 4`, where capL/capR are the widths of
shapes 0 and 1 of the gfx record (`VIBE_Coord_Transform(rec,0/1)`, `u16(shape+6)`).
`_BUTTON_RED` shapes = [12,12,100,12,12,100] h=33 -> caps 12 each -> `+28`.
`MenuFont::ButtonWidth(label) = MeasureWidth + 12 + 12 + 4`. Height stays 33.

`MenuButtonScreenRect(designY, fbW, fbH, designW=124)` now takes the measured
design width (default 124 = the 3-slice nominal for centre/hit-test callers). The
native menu computes per-button `designW` from the resolved label + `_FONT`,
renders the 3-slice stretched to that width, draws the label centred with the
real font, and hit-tests the variable rect.

## 4. Tests

* `tests/unit/menu_font_test.cpp`
  * `PropertyGetGoldenVectors` — synthetic `_FONT` blob; `MeasureWidth`/`ButtonWidth`
    match an independent Property_Get re-derivation (incl. kern + space paths).
  * `RealGfxLabelsAndWidths` — asset-guarded; over the real `gfx/gilde.gfx` `_FONT`
    + the install's textbin, the 8 labels resolve and `ButtonWidth` == the
    binary-derived widths {121,149,134,158,184,161,94,154}.
* `tests/unit/oneone_screens_wave14_test.cpp` — added
  `MenuButtonScreenRectVariableWidthPinned` (the new designW param + scaling); the
  existing 124-nominal pins keep passing (default arg).

Full suite: `ctest` 1551/1551 pass (portable build + vulkan-sdl build).

## 5. Visual verification

`GUILD_MENU_DUMP=/tmp/menu_final.bmp ./build-vk/guild_run --play \
  --game-dir europe_guild_1400_original --frames 100` -> 1024x768 dump shows the
real Russian labels in the real `_FONT` with each button sized to its text
(Авторы narrow, Настройки графики widest).
