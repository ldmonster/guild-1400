# Wave-11 hardening — GUI cluster (W11-GUI)

MCP DOWN → hardening only (no new 1:1 reconstruction). ASAN+UBSAN
(`-fsanitize=address,undefined -fno-sanitize-recover=all`) over the GUI cluster's
test targets. Added malformed/truncated/oversized-input tests and fixed every
memory-safety / UB / leak found. All fixes keep the **valid-asset path byte-identical**
(proven by the real-forms e2e golden, unchanged) — they only make malformed/degenerate
input fail safe, which is faithful (the original did not corrupt memory).

Dedicated sanitizer build dir: `build-asan-w11gui` (the shared `build-asan` is churned by
other waves). Normal `build/` kept green.

## Golden invariant held (real assets)
`real_forms_driver_e2e_test` over the shipped `Resources/forms.BIN` is **byte-identical**
before/after every fix:
```
members=416 form-members=323 parsed=323 (FRM2=321 old=2 failed=0)
windows=918 (child=592) widgets=609 [sprite=573 label=7 input=4 slider=25] objRecords=1085
richest: Menu/OPTIONS_ADVANCE_GFX.form (windows=9 widgets=10) propValidate=598 findText=7
```

## OOB writes/reads fixed

### form_parse.cpp — `Form_ParseResourceFile` (gilde.exe 0x41beb8)
The `.form` parser ingests untrusted bytes; several fields were used to index fixed-size
tables with no bound check:
- **parentIndex → `g_forms[formId].dw[parentIndex]`** — file dword, was unbounded.
  Guarded to the window-id table range `dw[1..96]`; out-of-range / negative → no child
  build (winSlot = -1). (Form record is 171 dwords; window-id table is dw[1..96].)
- **resolved parentSlot → `g_windows[parentSlot]`** — could be a stale/garbage slot id.
  Range-checked against `kMaxWindows` (96) before indexing.
- **form window-id table write `dw[1 + wi]`** — `wi` ran to `windowCount` (file dword).
  Guarded `wi < kMaxWindows` so a `windowCount > 96` can't overwrite `dw[97]` (window
  count) / run past the Form record.
- **objectCount → per-object array reads** — `objCount` is `HIWORD(dword@+202)` (up to
  65535) and drives reads at `+208/+3472/+10/+106 (+ 8/2*o)` and the 64-byte name slot
  `+400+64*o`. A malformed/oversized count read past the record into the next record (or
  off the buffer end for the last record). Clamp `objCount` to the min over all arrays
  that keeps every per-object read within `stride` (byte-identical for valid forms — the
  arrays physically can't hold more). Conditional input/slider byte reads
  `rec[+3472+8*o]` / `rec[+3868+8*o]` additionally guarded at their use sites against
  `stride` (also covers the old-layout 3796-byte record which cannot reach +3868).
- **`BuildChildWindow` child-list write `plist[parent.objCount()]`** — guarded
  `objCount >= kMaxChildren` (384), matching `Object_AddToWindow`'s own "Too many objects"
  cap, so a full parent can't write `plist[384..]`.

### window.cpp
- **`Window_Create`** — `Widget_AllocSlot()` returns -1 when the widget pool is full;
  the code then did `g_widgets[wIdx]` → **`g_widgets[-1]` OOB write**. Now checks the
  -1 return, frees the half-claimed slot, and fails. (A malformed/oversized form can fill
  the pool.)
- **`Window_Destroy`** — bound was `slot > kMaxWindows`, an **off-by-one**: `slot == 96`
  passed and indexed `g_windows[96]` OOB; negatives were unchecked. Now
  `slot < 0 || slot >= kMaxWindows`.

## UB fixed — misaligned dword reads (`int&` bound to unaligned address)
The 1:1 design models records as packed byte blobs; several leaves read a dword at an
unaligned offset and `>> 16/24` (the original does an unaligned x86 `mov`). Binding a
misaligned `int&` is C++ UB and faults on strict-alignment hosts. Replaced with
byte-identical memcpy by-value loads:
- `types.h` `Widget`, `form_loader.h` `GfxObject` — added `ld<T>(off)` (unaligned load).
  `GfxObject::width()/height()` (+78/+82) now read by value via `ld`.
- `widget_create.cpp`, `menu_frame_leaves.cpp`, `gui_dialogs4.cpp` — `w.at<i32>(110)`,
  `w.at<i32>(14)`, `w.at<i32>(18)` → `w.ld<i32>(...)`.
- `gui_dialogs4.cpp` — added by-value `MemR<T>`; converted misaligned `Mem<i32>(p, {2,6,
  14,26}) >> 16` reads (all read-only; lvalue uses keep `Mem<T>`).
- `gui_dialogs5.cpp` — added `LdI32/LdU32`; fixed unaligned reads at law-row +14 and +13.
- `gui_dialogs6.cpp` — added `LdI32`; fixed unaligned `weights[v7+1]/+2` reads.
- `tooltip_build.cpp` — `Tooltip_BuildingLayout` extra field `*(record+579)` (offset %4==3)
  → memcpy load.
- Test fix: `gui_tooltip_dispatch_test.cpp` wrote `*(int32_t*)(rec+579)` (misaligned) →
  memcpy store.

## OOB read fixed — undersized scratch buffer
- `gui_dialogs5.cpp` `Panel_BuildLawSeals` — `char v5[12]` Gesetz scratch, but the code
  reads a dword at `v5+12` (count) → **read 4 bytes at +12..+15, past the array**.
  Enlarged to `char v5[32]` (the original record is larger; covers the +0x18 layout the
  comment notes). Reads via memcpy.

## Leak fixed
- `Object_CreateTextLabel` (`menu_frame_leaves.cpp`) calloc'd a 257-byte text buffer and
  stashed it in `g_widgetData` via `SetWidgetData`, but neither `Widget_FreeSlot` nor
  `ResetWidgets` freed it → leak. The data table mixes **owned heap** buffers and
  **borrowed** pointers (Window*/field-records/scene-states), so a blanket free was unsafe.
  Added ownership tracking in `object.cpp/object.h`:
  - `g_widgetDataOwned[]` flag + `SetWidgetDataOwned(idx, heapBlock)` (frees any prior
    owned block on overwrite).
  - `Widget_FreeSlot` / `ResetWidgets` now `std::free` only owned blocks (matches the
    original's destroy-time `VIBE_Widget_DestroyByType` release; borrowed pointers
    untouched).
  - `SetWidgetData` now bounds-checks idx and marks the slot borrowed.
  - `Object_CreateTextLabel` switched to `SetWidgetDataOwned`.

## Tests added (all pass clean under ASAN+UBSAN; goldens unchanged)
- `gui_form_parse_test.cpp` — `GuiFormParseHarden.*`: empty/1-byte, truncated mid-record,
  objectCount-too-large (clamped, incl. last-record-no-overrun), out-of-range / negative /
  stale-slot parentIndex, NUL-less caption (64-byte cap), windowCount > 96, slider high
  object-index byte-read guard.
- `gui_form_loader_test.cpp` — `GuiFormLoaderHarden.*`: 0/1-byte, header-only (declared
  records but none present), count cap boundary.
- `gui_core_test.cpp` — `GuiCoreHarden.*`: `Window_Destroy` out-of-range (96 / -1 / huge),
  `Window_Create` & `Object_AddToWindow` under widget-pool exhaustion (the `g_widgets[-1]`
  fix).
- `gui_main_menu_test.cpp` — `GuiMainMenuHarden.OutOfRangeButtonIndexIsSafe` (the
  "bad button count" class: negative / == count / huge hovered index).
- `gui_infopanel_build_test.cpp` — `GuiInfoPanelBuildHarden.*`: host returning
  out-of-range widget ids (negative / past `kMaxWidgets`) — `SetWidgetFlag`/`Ref736`
  bounds guards; `InfoPanel_ResetDetailSlots` count clamp (>4 / negative).

## Verification
- All 63 GUI **unit** + all GUI **integration** tests + `real_forms_driver_e2e/itest`
  pass clean under ASAN+UBSAN (`build-asan-w11gui`), no `runtime error` / leak.
- Normal `build/`: 256 GUI-related tests pass; full suite 1472/1472 pass.
- Real-forms golden byte-identical (above).

## Notes / not-changed (1:1 envelope — needs MCP)
- `Object_GetDataPtr` (object.cpp) dereferences `g_widgetData[idx]` as `Object3D*` for
  type 'A'; on a degenerate/null payload this faults the same way the original does (an
  internal, not asset-driven, index) — left as the engine's own envelope.
- Old-layout (`.form` non-FRM2) slider/input use the FRM2 trailer byte offsets (+3472/
  +3868) which a 3796-byte old record can't reach; the shipped old-layout members
  (help.form, "ToolTip Geldsack.form") contain no sliders/inputs so this never fires.
  The reads are now stride-guarded (default 0); the **correct old-layout offsets** are a
  BEHAVIORAL question — needs MCP at 0x41beb8 to recover the old trailer layout.
- `gui_dialogs3.cpp` `Widget_CreateRawImage` stores a `g_hooks->AllocDebug` block in the
  data table (hook-managed ownership, not `calloc`); its free path belongs to the hook
  provider, not this fix.
