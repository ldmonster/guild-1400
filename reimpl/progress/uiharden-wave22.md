# Wave-22 — UI hardening to VERIFIED 1:1 (mature GUI modules)

Owner: W22-UIHARDEN. Scope (owned files only): `src/gui/{object,window,form_loader,
form_parse,widget_create,menu_frame_leaves,tooltip_build,gui_dialogs4,gui_dialogs5,
gui_dialogs6,statpanel,statchart}.{cpp,h}` + their tests + this doc. Did NOT touch the
files owned by sibling wave-22 agents (chatconsole, trade_panel_windows, mapview,
privilege_panels_b, widget_layout, gui_object_state).

Two axes: (1) float->int 1:1 fidelity (the ConvertX-TRUNCATE-vs-fistp-round bug class
from gap-statchart-wave17); (2) memory safety under ASAN+UBSAN.

`VIBE_Coord_ConvertX @0x5c6b08` is TRUNCATE toward zero (verified wave-15/17; re-confirmed
this wave). The canonical sibling `guild::util::ConvertX == std::trunc` is reused.

## Axis 1 — float->int fidelity divergences FIXED

### D1. gui_dialogs5.cpp `Panel_ChooseProfession` @0x566b9c — wealth scale dropped
Binary (decompile + disasm 0x566b…):
```
v4  = (double)wealth * flt_624DF8;   // flt_624DF8 = 0.015f  (get_bytes 0x624DF8,4 = 8F C2 75 3C)
VIBE_Coord_ConvertX();               // TRUNCATE st(0)=v4
v37 = (int)v4;
```
The reconstruction had `int v37 = wealth;` with a comment claiming "inert coord keeps it
integral". WRONG — the `*0.015f` scale is load-bearing. Fixed to
`v37 = (int)util::ConvertX((double)wealth * (double)0.015f)`.
- Precision note: `flt_624DF8` is a FLOAT32 whose exact value is 0.0149999996…, so e.g.
  `1000 * flt = 14.99999966…` → trunc = **14** (not 15). `(double)0.015f` reproduces the
  exact bits the x87 `fld dword` loads, so the reconstruction matches the binary's 14.
- New constant `kWealthDisplayScale = 0.015f`.

### D2. gui_dialogs6.cpp `Panel_RunPlantBar` @0x54dfdc — commit price: missing halve + 2nd trunc
Binary:
```
v24 = VIBE_Building_ComputeMarketPrice(...)
VIBE_Coord_ConvertX();  p1 = (int)v24            // TRUNCATE
v25 = (double)p1 * dbl_624490;  // dbl_624490 = 0.5  (get_bytes 0x624490,8 = ..E0 3F)
VIBE_Coord_ConvertX();  p2 = (int)v25            // TRUNCATE again
v26 = max(p2, 1024)
```
The reconstruction did only `int p=(int)price; v26=max(p,1024)` — it OMITTED the `*0.5`
scale and the second truncation. Fixed to the two-stage truncate/halve/truncate/floor.
- New constant `kPlantPriceHalf = 0.5` (dbl_624490).

## Axis 1 — sites audited and VERIFIED already-correct

- **gui_dialogs4.cpp `Widget_AddPersonRow` @0x518efc** (price label): binary does
  `ConvertX()` then `v12=(int)v7`. Reconstruction calls `coordConvertX(price)` (=trunc
  default) and `int amount=(int)price`. Since the C `(int)` cast truncates and ConvertX
  truncates, `(int)price == (int)trunc(price)`. VERIFIED equivalent.
- **menu_frame_leaves.cpp `InitStateReader` @0x412970** (cursor warp to button centre):
  binary builds `X=trunc(x + dbl_610E84*w)`, `Y=trunc(y + dbl_610E84*h)` on x87
  (dbl_610E84 = 0.5, get_bytes 0x610E84,8), each via ConvertX, then ConvertY(X,Y). The
  reconstruction warps to `(x + w/2, y + h/2)`. For integer x/y and non-negative w/h,
  `trunc(x + 0.5*w) == x + w/2` (integer div toward zero) exactly. VERIFIED equivalent;
  comment expanded with the addr + idiom.
- **statpanel.cpp / statchart.cpp**: all float->int sites already ConvertX-TRUNCATE with
  the x87-extended (double-widened) products, locked in wave-17. Re-swept: still 1:1.
  (Tests live in gui_panels_test/e2e — 526/56 checks, untouched, still green.)
- **object.cpp, window.cpp, tooltip_build.cpp, form_loader.cpp, form_parse.cpp,
  widget_create.cpp**: no float->int sites — integer geometry/clip arithmetic only
  (`x16` truncation/HIWORD shifts, `+96`, `>>16`, etc., byte-faithful). VERIFIED clean.

## Axis 2 — Memory safety (ASAN + UBSAN)

Built `build-asan` with `-DGUILD_BACKEND=OFF -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"` and ran
all 19 gui unit+e2e test binaries covering the owned modules:

| binary | checks | sanitizer |
|--------|--------|-----------|
| gui_core_test / _e2e | 1576 / 47 | clean |
| gui_panels_test / _e2e | 526 / 56 | clean |
| gui_dialogs5_test / _e2e | 47 / 7 | clean |
| gui_dialogs6_test / _e2e | 77 / 13 | clean |
| gui_form_loader_test / _e2e | 53 / 26 | clean |
| gui_form_parse_test | 82 | clean |
| gui_form_asset_test / _e2e | 38 / 23 | clean |
| gui_widget_create_test / _e2e | 79 / 43 | clean |
| gui_window_mgmt_test / _e2e | 67 / 28 | clean |
| menu_frame_leaves_test / _e2e | 59 / 24 | clean |

ALL CLEAN: 0 ASAN (OOB/leak), 0 UBSAN (misaligned/overflow/UB), 0 check failures.
No memory-safety fix was required this wave — the earlier packed-record/unaligned reads
in these modules already go through `std::memcpy` (e.g. tooltip_build `record+579`
unaligned dword; gui_dialogs5 `LdI32/LdU32`), which ASAN/UBSAN confirm are sound.

## Tests added / corrected (binary-pinned)
- `GuiDialogs5.ChooseProfessionLaysOutColumns`: new assertion
  `profWealthArg == 14` (= `trunc(1000 * 0.015f)`), capturing the float32 precision +
  TRUNCATE. Captures the `RichStr(0x16A7,…,v37)` arg via the recording hook.
- `GuiDialogs6.PlantBarPriceTruncateHalveTruncateFloor`: new golden pinning the
  truncate→×0.5→truncate→floor(1024) idiom (the inert test path can't reach the live
  code, so the math is pinned standalone via `util::ConvertX`). 5 checks.

## Build status
- `guild` library: green (normal `build/` and `build-asan/`).
- Changed tests green in both presets (gui_dialogs5_test 47/0, gui_dialogs6_test 77/0).

## Remaining boundary
None for these modules. Every float->int site in the mature GUI cluster is now a
fully-reconstructed ConvertX truncation (no fake analogues, no deferred boundary).
