# Menu Sub-Screens — 1:1 Ground Truth (forms + labels + gfx)

Recovered via IDA MCP + the real `Resources/forms.BIN` (FRM2 parser) + `gfx/gilde.gfx`
+ `Resources/textbin_*.BIN`. This is the DATA every sub-screen reconstruction must
match. The proven main-menu model applies: NATIVE 800×600 pixel size, CENTER-TRANSLATED
by `((fbW-800)/2,(fbH-600)/2)` (PositionAtCoord mode-2/3 @0x41d7e0), NEVER scaled; real
`_FONT` glyphs; real localized labels from the textbin; real gfx sprites.

## Background (KEY FIX)
The options/menu sub-screens use **`_OPTIONEN_PIC`** (gfx #1770, **800×600**) as the full
background — NOT `_MENUE_BACKGROUND`. Resolution variants: `_OPTIONEN_PIC_1024` (#1771),
`_OPTIONEN_PIC_1152` (#1772). The main menu keeps `_MENUE_BACKGROUND`.

## Frame model (Window_Create @0x419c38)
Flags decoded: `0x1` = own opaque backing surface (panel); `0x10` = text buffer;
`0x4` = window color; `0x200` = scroll; `0x8` = adds 1 frame-corner obj (dword_62D2A8);
`0x20` = adds 2 more frame-corner objs (dword_62D2A8+1,+2). The options outer windows use
flags `0x11`/`0x111` → **no frame-corner gfx** (neither 0x8 nor 0x20). The visible panels
are the form's own type-5 sprite objects: **`_TOOL_TIP_BIG`** (gfx #1784, shape0 475×301
parchment; +1/+2 = 384×7 / 384×17 trim) placed inside the body window.

## Gfx assets (all decode OK from gilde.gfx)
- `_OPTIONEN_PIC` #1770 800×600 (bg); `_OPTIONEN_PIC_1024` #1771; `_OPTIONEN_PIC_1152` #1772
- `_TOOL_TIP_BIG` #1784 shapes: [0]475×301 [1]384×7 [2]384×17  (parchment panel)
- `_SLIDER_GOLD_WAAGERECHT` #106 8 shapes: [0]68×18 [1]100×6 [2]100×6 [3]66×19 [4]100×6
  [5]67×18 [6]35×18 [7]34×18  (horizontal gold slider; aux=2 in forms = horizontal)
- `_BUTTON_RED` #174 6 shapes: [0]12×33 [1]12×33 [2]100×33 [3]12×33 [4]12×33 [5]100×33
  (3-slice: normal {0=L,2=C,1=R}, hover {3=L,5=C,4=R})
- `_HEADLINE_GOLD_BIG` #72 2 shapes 48×18 (title headline art)
- `_OPTIONS_RAHMEN` #1778 1 shape 470×403; `_FORM_RAHMEN` #1633 9-slice (10 shapes)

## Forms (Resources/forms.BIN, FRM2; native window x/y/w/h)
- `Menu/MAIN_MENU.form`   WIN0 (232,160,407,352) flags0x10 — button hub (0 objs) [DONE]
- `Menu/OPTIONS.form`     WIN0 (168,128,440,478) flags0x10 — button hub (0 objs) [Menu_RunOptionsMain @0x56dccc]
- `Menu/CHOOSENETWORK.form` WIN0 (144,160,297,400) flags0x110 — button hub (0 objs) [Menu_ChooseNetworkMode @0x529a64]
- `Menu/OPTIONS_GFX.form`  WIN0 (112,120,452,574) flags0x11; WIN1 (146,171,328,509) parent1 = 9 sliders
  `_SLIDER_GOLD_WAAGERECHT` at x=208 y={16,48,96,160,128,192,240,272,304} + 2× `_TOOL_TIP_BIG+1` sprites (64,80)/(64,224)
- `Menu/OPTIONS_SFX.form`  WIN0 (112,120,449,575) flags0x11; WIN1 (144,168,340,514) parent1 = 5 sliders
  x=208 y={32,120,160,200,280} + 2× `_TOOL_TIP_BIG+1` (64,80)/(64,248)
- `Menu/OPTIONS_GAME.form` WIN0 (104,120,449,575) flags0x11; WIN1 (136,168,344,508) parent1 = 11 sliders
  x=208 y={8,72,96,120,144,168,208,232,256,280,304} + 2× `_TOOL_TIP_BIG+1` (72,56)/(72,192)
- `Menu/LOADGAME_NEW.form` WIN0 (128,136,449,575) flags0x11; WIN1 (168,184,390,493) parent1 = slot list (0 form objs, runtime rows) [Menu_RunLoadGame @0x56a270]
- `Menu/SAVEGAME.form`     WIN0 (168,136,353,345) flags0x10 + input field (type65) @(176,280)
- `Menu/CHOOSENETWORK_IP.form` WIN0 (152,192,224,361) flags0x110 font'_FONT+2': input(16,88), label `_OPTIONEN_NETZWERK_JOINEN+1`(16,56)
- `Menu/SEARCH_NETWORK.form`   WIN0 (120,120,451,575) flags0x111 [Menu_SearchNetwork]

## Label key mapping (textbin `_OPTIONEN_*`; resolve BY NAME via TextDb.FindIndex)
Array base: global `dword_8C9854` = textdb idx 11020 (`_OPTIONEN_MENUE_LOAD`); stride 4 bytes
→ idx(addr) = 11020 + (addr-0x8C9854)/4. Verified.

OPTIONS_GFX @0x56c21c (title rich-string id 0x1867; 9 rows; child build order):
| child | label key        | value list key (AppendWideLines) | binding |
|-------|------------------|----------------------------------|---------|
| 0 res | `_OPTIONEN_GFX+0` | `_OPTIONEN_STUFEN_RES` (3) | byte_63D724, hidden in-game (byte_63CC40) |
| 1 details | `_OPTIONEN_GFX+1` | `_OPTIONEN_STUFEN` (3) | byte_1233514 |
| 2 texture | `_OPTIONEN_GFX+2` | `_OPTIONEN_STUFEN` (3) | byte_1233515 |
| 3 floor_lod | `_OPTIONEN_GFX+3` | `_OPTIONEN_STUFEN` (3) | byte_123351A |
| 4 floor_mip | `_OPTIONEN_GFX+4` | `_OPTIONEN_STUFEN_AN_AUS` (2) | byte_1233518 |
| 5 lod_handling | `_OPTIONEN_GFX+5` | `_OPTIONEN_STUFEN` (2) | byte_1233516 |
| 6 shadow | `_OPTIONEN_GFX+6` | `_OPTIONEN_STUFEN` (3) | byte_1233517 |
| 7 gamma | `_OPTIONEN_GFX+7` | (slider 50..100, seed 100-fog) | byte_123351C |
| 8 cam_limits | `_OPTIONEN_GFX+8` | `_OPTIONEN_STUFEN` (3) | byte_1233519 |

OPTIONS_SFX @0x56c808 (rows `_OPTIONEN_SFX+0..4`; sliders 0..127 step16; +4 = `_OPTIONEN_STUFEN_FREQ` cycle 0..4).
OPTIONS_GAME @0x56cc44 (rows `_OPTIONEN_GAME+0..11`; child4 invert_mouse hidden; +7 panel_mode = `_OPTIONEN_STUFEN_PANEL` 0..4).
OK/Cancel button row: `_OPTIONEN_BUTTONS+0` ("Принять"/Okay) / `_OPTIONEN_BUTTONS+1` ("Отмена"/Cancel),
built by Hud_BuildButtonRow @0x4bcdfc into form window 3 (dword_8C996C).

NETWORK hub `_OPTIONEN_NETZWERK_MENUE+1..5` (Host/Join/Search/host-without-load/...).
LOAD slot text `_OPTIONEN_LOAD_GAME_SLOT_INFO` / `..._LEER` (empty slot).

## Methodology for each screen (mirror native_main_menu.cpp)
1. Background = `_OPTIONEN_PIC` (res variant by fbW), full-screen.
2. Form geometry from forms.BIN (native window x/y/w/h), center-translate only.
3. Body parchment = `_TOOL_TIP_BIG` sprite(s) at the form's type-5 object positions.
4. Rows: real label (left, real `_FONT`, CP1251 from textbin) + `_SLIDER_GOLD_WAAGERECHT`
   3-slice track with thumb at value/range + cycle value text (right).
5. Title: rich-string text centered in title window, real `_FONT`.
6. OK/Cancel: `_BUTTON_RED` 3-slice buttons, labels `_OPTIONEN_BUTTONS+0/+1`.
7. Value/range/step bindings already disasm-pinned in `OptionsRowsFor` (sdl_options_screen.cpp).
