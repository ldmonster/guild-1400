# Recon 03 — GUI / HUD / Windowing / Text-Rendering Cluster

`gilde.exe` ("Die Gilde" / "The Guild"), 32-bit x86, imagebase 0x400000.
Retained-mode software-rendered GUI. The original German source used the prefix
`d2_` / `io_` (visible in strings: `d2_SetForm()`, `io_Text()`, `d2:win_obj`,
`d2_t:w->txt`). Our recovered names use `VIBE_<Module>_<Action>`.

This cluster draws everything to an in-memory software surface; it does **not**
use GDI/USER32 for in-game widgets. user32/gdi32 are used only for the OS top-level
window, message pump, and an offscreen DIB blit (see §5).

---

## 1. Overview — the Form / Window / Object retained-mode model

There are **three** nested levels of indirection. From outermost to innermost:

### (a) Form
A *Form* is a logical screen/dialog loaded from a `.form` resource
(e.g. `misc\Messagebox`, `misc\Messagebox_BIG`). A form owns a list of **Windows**.
The "current form" is a global selector index.

- `dword_676BE4[171*formId]` = window-count for the form (first dword of a
  171-dword-stride form record).
- `dword_676A64[171*formId + winSlot]` = the **window id** stored at logical slot
  `winSlot` within the form (171-dword stride; `dword_676A60` is the same array
  base minus one dword — used as `[171*form + 1 + idx]` in GetObject* helpers,
  i.e. it indexes window-ids by *object-group* slot).
- Current-form / current-window selector globals:
  - `dword_62D258` — current form id (set by `SelectWindow` when arg!=0).
  - `dword_62D230` — current window id.
  - `dword_62D298` — cached pointer to the current Window record
    = `&dword_67EB80[238 * dword_62D230]`.

`VIBE_Form_SelectWindow(formId@eax, winSlot@edx)` @ **0x41e4cc** (923 xrefs — the
single most-called selector): validates `winSlot` against the form's window count,
resolves the window id, and caches the Window pointer. On invalid slot it logs
`"d2_SetForm(): invalid win number"`.

### (b) Window
`dword_67EB80` is the global **Window array**: stride **238 dwords = 952 bytes**,
capacity = 96 windows (`22848/238`). A window slot is "free" when dword[0] == 0
(checked in `Window_Create`). The window record holds geometry (packed 16.16 in
several dwords), surface handles, flags, the object-id list, and scroll state.

`VIBE_Window_Create(x@eax,y@dx,w@cx,h@bx,flags@stack)` @ **0x419c38** allocates a
window slot, sets bounds, optionally creates up to 3 software surfaces
(`VIBE_Surface_Create`), allocates a **0x600-byte object-id buffer**
(`d2:win_obj`, holds up to 384 object ids), optionally a **0x17D0-byte text buffer**
(`d2_t:w->txt`), creates a backing widget slot of type tag `0x40` ('@'), and pushes
the window into the global widget array.
Window flag bits seen: `0x4` (use palette `dword_62D2B8`), `0x8` (add background
object), `0x10` (allocate text buffer), `0x20` (add scrollbar buttons via
`Object_AddToWindow`), `0x200` (clip region), `0x1` (alpha/colorkey surface).

Window-record key offsets (dword index unless noted; `*((WORD*)w + N)` = word index):
- word[2..5] = x, y, w, h (16.16 stored as `>>16` to get pixels).
- `(char*)w + 26` `>> 16` = **object count** (number of objects on the window).
- `w[6]` = pointer to the **object-id array** (the 0x600 buffer). `w[6][k]` is the
  widget-array index of the k-th child object. (Note: in some helpers the byte
  view `(char*)w + 24` is the array ptr and `+26>>16` the count — the messagebox
  loop reads `*(w+24)` as the id-array base and `*(w+26)>>16` as the count.)
- `w[8],w[9],w[10]` = surface handles (main / alpha / colorkey).
- `w[11]` = text buffer (when flag 0x10).
- `w[3]` = window flags.
- `w[145],w[146],w[147]` = scroll thumb position / current / previous (used in
  `Widget_DispatchMouseClick` scrollbar drag math).
- `w[155]` = backing widget-slot index; `w[156],w[160]` = state/enabled flags.

### (c) Object / Widget
`dword_69FFB4` is the global **Object/Widget array**: stride **740 bytes**,
capacity **511** (`378140/740`). A widget slot is free when `dword[slot+1]` (offset
+4) == 0; `VIBE_Widget_AllocSlot` @ 0x412dac linearly scans for a free slot, marks
`+4 = 1`, and records the pointer in a 512-entry pointer cache `dword_62D26C`
(2048 bytes = 512 ptrs), tracking the high-water index in `dword_62D24C`.

Widget record (740 bytes) key offsets (byte offsets):
- `+24` (BYTE) = **type tag**: `0x40`='@' window-backing, `0x41`='A' animated/3D
  object (`VIBE_GameLogic_Objects`), `0x45`='E' edit/text field. Confirmed in
  `Object_GetDataPtr`, `Object_SetValueOrText`, `Form_GetObjectAnimPtr`.
- `+4`  = in-use flag.
- `+8`  = id (window backing = winSlot+1024).
- `+12` = pointer to type-specific data record (for 'A' objects, the 3D-object
  struct; for the window-backing widget, the Window record).
- `+16..+22` (WORD) = x,y,w,h copied from window.
- `+26` (WORD) = object subtype/order (=2 default).
- `+28..+34` (WORD) = clip/screen bounds; `+28` incremented = child count.
- `+36` = value (also mirrored at `+40`).
- `+44` = group/parent link.
- `+52,+60` = inherited render/clip pointers from the owning window backing widget.
- `+68,+72` = button/clickable flags (nonzero ⇒ togglable button).
- `+116` = owning window slot.
- `+120,+124,+128,+140` = edit-field ('E') text ptr / value / range / step.
- `+620` = back-pointer used to fetch parent's `+52/+60`.

### Object accessor helpers (what they return)
- `VIBE_Form_GetObjectPtr(localId@eax, group@edx)` @ **0x41db20** →
  `dword_69FFB4 + 740 * windowObjIdArray[localId]` — pointer to the **widget
  record**. Resolves window via `dword_676A60[171*form + 1 + group]`, bounds-checks
  `localId <= objectCount`.
- `VIBE_Form_GetObjectDataPtr(localId,group)` @ **0x41dc10** → calls
  `VIBE_Object_GetDataPtr(widgetIdx)` @ **0x41db9c**, which for a type-'A' object
  dereferences `widget+12` → 3D-object record and, depending on its flag byte at
  `+38` (`0x10` ⇒ return `[+328]`, `0x01` ⇒ return record+40 [inline value],
  `0x02` ⇒ return `[+296]`), returns the **data payload**; otherwise returns
  `widget+36` (the value), or for 'E' edit fields `widget+120`, else -1.
- `VIBE_Form_GetObjectAnimPtr(localId,group)` @ **0x41dd14** → for type-'A' objects
  returns `widget+12` (the **3D-object / animation record**), else 0.

`VIBE_Object_AddToWindow(winSlot@ecx, y@dx, x@ax, gfxId@ebx)` @ **0x41ae10** is the
core "create a child widget" routine: errors `"Too many objects on window!"` at
≥384 objects; calls `VIBE_GameLogic_Objects` to spawn the underlying object, stores
its index in `w[6][count]`, links Z-order (`VIBE_ZOrder_InsertObject`), copies
parent clip/render pointers, bumps count, and grows the window content-height
(`*(w+580)`).

### The 740 / 536 / 134 strides in the main loop
- `dword_69FFB4 + 740*idx` — the widget/object array above (confirmed everywhere).
- `byte_12CE912[536*idx]` and `dword_12CEB18[134*idx]` (134 dwords = 536 bytes) are
  **two views of the same 536-byte-stride table** — the per-frame **rendered-object
  / scene-instance table** the main loop walks to draw the 3D objects that widgets
  reference (parallel to the 740-byte logical-widget table). `dword_12CEB18` is the
  dword view, `byte_12CE912` a byte field 0x202 (514) into the same record. This
  table is owned by the renderer/scene cluster, not the GUI cluster, but GUI
  type-'A' widgets index into it via `widget+12`.

---

## 2. Text / font rendering engine (`VIBE_Text_*`)

Two near-identical 7–9 KB dispatchers form the heart of text output; a third
(`RenderCreditsBlock`) is a scroll-clone:

- `VIBE_Text_RenderRichString(fmt, ...)` @ **0x59d6e8** (8882 B, 1172/295 xrefs) —
  draws a formatted/marked-up string into the current window's text buffer
  (`dword_62D298` window, text buffer at offset 0x17d0/6096, secondary 0x17c0/6080;
  per-call working buffer 0x3218 bytes). Emits each glyph/icon; calls
  `VIBE_Text_GetCurrentIconWord` and errors
  `"io_Text(): too many icons before redraw!"`.
- `VIBE_Text_RenderFormattedMessage(buf, msgId, ...)` @ **0x59f99c** (7296 B,
  422 xrefs) — resolves a localized text id, substitutes variadic args, and writes
  the result; errors `"io_Text(): Missing Random-Text (e.g. {r1})..."`.
- `VIBE_Text_RenderCreditsBlock` @ 0x5a161c — scrolling-credits variant.

### Inline format / markup codes (recovered from constants + format strings)
Escape character is `%` (0x25). A leading `$` (0x24) introduces substitution
tokens; `{` (0x7b) introduces random-text tokens like `{r1}`.

`%`-codes (built from sprintf fragments at 0x627e08..0x627ee4):
- `%s`, `%i`, `%c`, `%0%ii` (zero-padded int), `%.f` / `%0.Nf` (fixed point via
  `VIBE_Text_FormatFixedPoint` / `FormatExponent` / `FormatDigitPair*`), `%i%c`,
  `%s %s`, `%s %s %s`, `%s %i`, `%s >%s<`.
- Single-letter sub-specifiers consumed inline: `i`, `b`, `U`, `E`, `B`, `T`, `m`,
  `e`, `a` (constants 0x69 'i', 0x62 'b', 0x55 'U', 0x45 'E', 0x42 'B', 0x54 'T',
  0x6d 'm', 0x65 'e', 0x61 'a'). These select number/money/date/time/title
  formatting (`VIBE_Money_FormatWithSeparators`, `VIBE_GameTime_PackToRecord`,
  `VIBE_GameTime_GetSeasonFromYear`).
- Unknown `%` ⇒ error `"Unknown textparameter: %%%c"` (in markup builder).

Markup tokens parsed by `VIBE_Window_ParseMarkupAndBuild` @ **0x416720** (8118 B):
- `$[` ... `$]` — bracketed region (`Missing ']'` error). 
- `$M` — message/embed marker (uses literal `"M"`).
- `$T` — table/tab marker.
- `_FONT` — switch font.
- `_BUTTON_RED` — inline red button widget.
This builder converts a marked-up string into actual child Objects on a window
(it calls `Object_AddButtonLabel`, `Object_AddTextLabel`, `Object_SetEditText`,
`Widget_CreateSprite`, `Window_LayoutScrollContent`, `Window_Resize`).

### Item / label formatters
- `VIBE_Text_FormatItemLabel` @ 0x59c2e4 (2544 B) and
  `VIBE_Text_FormatItemLabelWithIcon` @ 0x59ccf4 — build localized item/quantity
  labels with optional inline icon words.
- `VIBE_Text_GetCurrentIconWord` @ 0x5a3418 — returns the pending icon code.

### Text/label database
- `VIBE_Text_LoadTextFile` 0x44dba0 / `ReloadTextFile` 0x44d970 /
  `SaveTextFile` 0x44de8c / `FreeTextFile` 0x44d940 — load/save the localized
  `.dat`/text resources into slots (`FindTextFileSlot`, `FindTextArrayIndex`).
- `VIBE_Text_BuildTextArray` @ 0x44bb5c (7496 B) — parse a loaded text file into
  the indexed string array.
- `VIBE_Text_ParseLabelDefinition` 0x44b2e0 / `GenerateLabelDefines` 0x44ae24 /
  `LookupLabelEntry` 0x44add4 — the `#define`-style label-id system.
- `VIBE_Text_ParseRandomTextToken` 0x44b8a0 — resolves `{rN}` random variants.

Font tables: `dword_8C36B0` and `dword_B537B4` are referenced from
`RenderRichString` as the active font / glyph-metric tables (font cluster owns the
actual glyph bitmaps; GUI reads metrics from these). Font is selected via the
`_FONT` markup token and window default at `dword_62D2B0`.

---

## 3. Key functions (address / prototype / purpose)

| Addr | Name | Prototype (recovered) | Purpose |
|------|------|-----------------------|---------|
| 0x41e4cc | VIBE_Form_SelectWindow | `int(formId@eax,winSlot@edx)` | Select current form/window; cache ptr. **Anchor, 923 xrefs.** |
| 0x41db20 | VIBE_Form_GetObjectPtr | `int(localId@eax,group@edx)` | → widget record ptr. |
| 0x41dc10 | VIBE_Form_GetObjectDataPtr | `int(localId,group)` | → widget data payload. |
| 0x41dd14 | VIBE_Form_GetObjectAnimPtr | `int(localId,group)` | → 3D/anim record (type 'A'). |
| 0x41db9c | VIBE_Object_GetDataPtr | `int(widgetIdx@eax)` | Resolve value/data/edit-text per type+flag. |
| 0x41dea8 | VIBE_Form_GetChildObjectId | `int(a,b,c)` | Map child slot → object id (125 xrefs). |
| 0x41d6ac | VIBE_Form_CenterChildWindows | `char(form@eax)` | Center a form's windows (214 xrefs). |
| 0x41da04 | VIBE_Form_Destroy | `int(form@eax)` | Destroy form + windows (246 xrefs). |
| 0x41d634 | VIBE_Form_SetObjectsVisible | `int(form,vis)` | Show/hide all objects (59 xrefs). |
| 0x419c38 | VIBE_Window_Create | `int(x@ax,y@dx,w@cx,h@bx,flags)` | Allocate + init window (surfaces, buffers). |
| 0x41a598 | VIBE_Window_AddChildWindow | `int(x,y,w,h,?,?)` | Nested window. |
| 0x41a0f8 | VIBE_Window_Resize | `int(w@ax,h@dx,...)` | Resize + reflow. |
| 0x41a90c | VIBE_Window_Destroy | `int(win@eax)` | Free window surfaces + objects. |
| 0x416720 | VIBE_Window_ParseMarkupAndBuild | `int(win@eax,str@edx)` | Parse `$[ ]`,`$M`,`$T`,`_FONT`,`_BUTTON_RED` → child objects. **8 KB.** |
| 0x4186d8 | VIBE_Window_RenderContent | `int(win@eax,?)` | Blit window + children to surface. |
| 0x41536c | VIBE_Window_LayoutScrollContent | `int(win,..)` | Lay out scrollable content. |
| 0x4163bc | VIBE_Window_ApplyScrollOffset | `int(win@eax)` | Apply scroll offset to children. |
| 0x4146ac | VIBE_Window_RenderUpdates | `int()` | Flush dirty windows to screen. |
| 0x41ae10 | VIBE_Object_AddToWindow | `int(win@ecx,y@dx,x@ax,gfx@ebx)` | Create child object (94 xrefs). |
| 0x41af64 | VIBE_Object_AddAnimatedToWindow | `int(win,?,?)` | Add type-'A' 3D object. |
| 0x41b288 | VIBE_Object_AddTextLabel | `int(x@ax,y@dx,?,str@ebx)` | Static text (47 xrefs). |
| 0x41b598 | VIBE_Object_AddButtonLabel | `int(x,y,?,str)` | Text button. |
| 0x41b75c | VIBE_Object_SetEditText | `char(obj@eax,str@edx)` | Edit-field text. |
| 0x41dfec | VIBE_Object_SetValueOrText | `char(idx,str,a3,a4,a5)` | Set value/string/slider/edit (77 xrefs). |
| 0x41e318 | VIBE_Object_SetEnabled | `int(obj@eax,en@edx)` | Enable/disable (51 xrefs). |
| 0x41e614 | VIBE_Object_SetColor | `char(obj@eax,col@edx)` | Set color (43 xrefs). |
| 0x41dd98 | VIBE_Object_SetVisibleRecursive | `int(obj,vis)` | Show/hide subtree (21 xrefs). |
| 0x412dac | VIBE_Widget_AllocSlot | `int()` | Allocate free 740-byte widget slot. |
| 0x413220 | VIBE_Widget_LayoutBounds | `char(x,y,obj)` | Compute clip bounds (27 xrefs). |
| 0x421594 | VIBE_Widget_DispatchMouseClick | `void()` | **Master per-frame widget hit-test + click dispatch.** |
| 0x420db4 | VIBE_Widget_ProcessMouseDrag | `int()` | Drag handling. |
| 0x41fd48 | VIBE_Widget_HoverUpdate | `int*()` | Hover state. |
| 0x420360 | VIBE_Widget_HandleKeyInput | `char()` | Keyboard input to focused widget. |
| 0x421370 | VIBE_Widget_SetFocus | `char(obj@eax)` | Focus management. |
| 0x4121c4 | VIBE_Widget_DrawScrollBar | `int(a,b)` | Render scrollbar. |
| 0x410180 | VIBE_Widget_CreateSlider | `int(x,y,..)` | Slider widget. |
| 0x4137bc | VIBE_Widget_DrawCheckbox | `int(obj@eax)` | Checkbox render. |
| 0x412728 | VIBE_RadioGroup_Create | `int(?,?)` | Radio/button group for dialogs. |
| 0x412800 | VIBE_RadioGroup_AddButton | `int(grp,obj)` | Add button to group. |
| 0x41acac | VIBE_ZOrder_InsertObject | `void(obj,win)` | Insert into Z-order. |
| 0x41aae8 | VIBE_ZOrder_RaiseWindow | — | Raise window to front. |
| 0x4bc280 | VIBE_Hud_HandleMouseClick | `char(ax,ebx,edi)` | **In-world HUD click dispatch (selection, room change).** |
| 0x59542c | VIBE_Hud_ProcessDragDropClick | — | HUD drag/drop. |
| 0x4bae88 | VIBE_Hud_DrawSelectedUnitInfo | — | Selected-unit info panel (2328 B). |
| 0x4ad6f0 | VIBE_Dialog_ShowMessageBox | `int(msgId@dx,kind@sil)` | Modal messagebox (94 xrefs). |
| 0x4acbd0 | VIBE_Dialog_ShowMessageBoxBig | — | Big messagebox. |
| 0x569a30 | VIBE_Dialog_RunMessageBox | `int(a,b,c)` | Generic modal runner. |
| 0x59d6e8 | VIBE_Text_RenderRichString | `int(fmt, ...)` | **Rich-string renderer. 8.8 KB, 1172 xrefs.** |
| 0x59f99c | VIBE_Text_RenderFormattedMessage | `void(buf,msgId,...)` | **Localized formatted message. 422 xrefs.** |

---

## 4. Data structures (proposed C++ layout)

```cpp
// dword_676BE4 / dword_676A64 / dword_676A60 : Form table, 171-dword stride.
struct Form {              // 171 dwords = 684 bytes
    int32_t windowCount;   // [0]  -> dword_676BE4[171*id]
    int32_t windowId[170]; // [1..]-> dword_676A64[171*id + slot]
};
extern Form g_forms[/*N*/];          // base 0x676A60/0x676A64/0x676BE4

// dword_67EB80 : Window array, 238-dword (952-byte) stride, capacity 96.
struct Window {                       // 952 bytes
    int32_t  inUse;        // +0   (0 = free slot)
    /* +4  */ ...
    int16_t  pad8[4];      // +16  margins (=4,4,4,4)
    int16_t  x,y,w,h;      // word[2..5] = +4,+6,+8,+10 (16.16 elsewhere)
    int32_t  flags;        // w[3] = +12
    int32_t  objListPtr;   // w[6] = +24  -> int32 objectId[<=384]  (0x600 buf)
    int32_t  objCount16_16;// +26 (>>16 = count)
    int32_t  surfaceMain;  // w[8]  = +32
    int32_t  surfaceAlpha; // w[9]  = +36
    int32_t  surfaceKey;   // w[10] = +40
    int32_t  textBuffer;   // w[11] = +44  (0x17D0 buf when flag 0x10)
    // ... scroll: w[145]=thumbPos, w[146]=cur, w[147]=prev (+580..+588)
    int32_t  contentHeight;// +580 (*(w+580))
    int32_t  backWidget;   // w[155] = +620 backing widget slot
};
extern Window g_windows[96];          // base 0x67EB80; free-flag array 0x67EE00

// dword_69FFB4 : Object/Widget array, 740-byte stride, capacity 511.
struct Widget {                       // 740 bytes
    int32_t  _0;
    int32_t  inUse;        // +4
    int32_t  _8;           // +8  id (window backing = winSlot+1024)
    int32_t  dataPtr;      // +12 -> Window* (type @) or Object3D* (type A)
    int16_t  x,y,w,h;      // +16,+18,+20,+22
    uint8_t  type;         // +24 : 0x40 '@' window, 0x41 'A' 3d obj, 0x45 'E' edit
    int16_t  order;        // +26 (=2)
    int16_t  cx,cy,cw,ch;  // +28..+34 clip/screen bounds (+28 also child count)
    int32_t  value;        // +36 (mirrored +40)
    int32_t  groupLink;    // +44
    int32_t  renderPtr;    // +52  (inherited from parent backing)
    int32_t  parentClip;   // +60
    int32_t  btnFlagA;     // +68 (nonzero => togglable button)
    int32_t  btnFlagB;     // +72
    int32_t  ownerWindow;  // +116
    int32_t  editText;     // +120 (type 'E')
    int32_t  editValue;    // +124
    int32_t  editRange;    // +128
    int16_t  editFlags;    // +132
    int32_t  editStep;     // +140
    int32_t  backWindow;   // +620
};
extern Widget g_widgets[511];         // base 0x69FFB4
extern Widget* g_widgetCache[512];    // base 0x62D26C; high-water 0x62D24C

// Object3D flag byte at obj+38: 0x10 => data at obj[+328]; 0x01 => inline obj+40;
//                               0x02 => data at obj[+296]; 0x40 => slider clamp.

// Per-frame rendered-object/scene table (renderer cluster, GUI indexes via +12):
//   dword_12CEB18[134*idx]  (134 dwords = 536-byte stride)
//   byte_12CE912[536*idx]   (byte field +0x202 of same record)
```

### Key global selectors / state
| Global | Meaning |
|--------|---------|
| 0x62D258 | current form id |
| 0x62D230 | current window id |
| 0x62D298 | current Window* (= &g_windows[curWin]) |
| 0x62D2F0 | rotating window-create counter (wraps at 96) |
| 0x62D26C / 0x62D24C | widget pointer cache (512) / high-water index |
| 0x62D2B0 | default font id for new windows |
| 0x62D2B8 | default palette |
| 0x75BF38 | id of last-clicked widget (1210=OK, 1155=Cancel/right) |
| 0x75BF08 / 0x75BF10 / 0x75BF00 | last-clicked window / object slot |
| 0x75BEF0 | last-clicked widget record ptr |
| 0x62D22C / 0x62D290 | currently hovered object / window |
| 0x672220 / 0x672228 / 0x672230 | mouse button / click / right-click state |
| 0x676584/0x676588/0x67658C | radio-group tables (35-dword stride, 8 groups) |
| 0x631DA8 | "messagebox active" re-entrancy guard |
| 0x8C36B0 / 0xB537B4 | active font / glyph-metric tables |
| 0x67EE00 | window-slot free flags (parallel to g_windows) |

---

## 5. External dependencies

In-game GUI is **fully software-rendered to an offscreen surface**; no per-widget
GDI. OS-level usage only:
- **user32**: `RegisterClassA/CreateWindowExA/DefWindowProcA` (top-level window in
  `VIBE_Window_CreateMainWindow` 0x52895c, `VIBE_Window_MainWndProc` 0x5279dc),
  message pump (`PeekMessageA/GetMessageA/TranslateMessage/DispatchMessageA` via
  `VIBE_Window_PumpMessages` 0x4bea64), `GetCursorPos/SetCursorPos/ShowCursor`,
  `MessageBoxA` (fatal errors only), `wsprintfA/CharUpperBuffA`.
- **gdi32**: `CreateDIBSection/CreateCompatibleDC/SelectObject/BitBlt` — present the
  software surface; `TextOutA/SetTextColor/SetBkMode` — used only for OS-side debug
  text, **not** for game UI (game text goes through `VIBE_Text_RenderRichString`
  into the software surface using the bitmap-font tables).
- **ddraw**: `DirectDrawCreate` / `DirectDrawEnumerateExA` — primary display path.

Reimplementation can target any blit-capable backend (SDL/DX). The GUI cluster
itself only needs: a 8/16-bit indexed software surface, blit/colorfill/clip,
mouse+keyboard state, and the bitmap-font glyph tables.

---

## 6. Effort & risks

**Overall effort: L.** Three large dispatchers dominate.

| Item | Effort | Risk |
|------|--------|------|
| Form/Window/Object model + accessors | M | Low — small, well-understood functions; struct layout recovered. |
| Widget creation/layout/Z-order | M | Medium — many `__usercall` reg-arg conventions to transcribe carefully. |
| `Widget_DispatchMouseClick` / `ProcessMouseDrag` / `HoverUpdate` | M | Medium — global state machine (0x75BFxx, 0x6722xx), order-sensitive. |
| **`Text_RenderRichString` (8.8 KB)** | **L** | **High** — giant goto-laden format dispatcher; icon/font state, redraw batching, 451 constants. Needs byte-accurate transcription. |
| **`Text_RenderFormattedMessage` (7.3 KB)** | **L** | **High** — near-duplicate; share helpers but verify divergences. |
| `Window_ParseMarkupAndBuild` (8 KB) | L | High — markup grammar (`$[ ]`,`$M`,`$T`,`_FONT`,`_BUTTON_RED`,`%`-codes). |
| Dialog/Hud/Menu/Panel builders (~150 fns) | L (bulk) | Low each — mechanical, but high count; each loads a `.form` and wires widgets. |
| Number/date/money formatters | S | Low — small, testable in isolation. |

Primary risks: (1) the three KB-scale dispatchers — budget for golden-output diff
testing against the original; (2) `__usercall` register-argument prototypes must be
transcribed exactly; (3) the 16.16 fixed-point packing of geometry; (4) shared
mutable global state across click/drag/hover must preserve evaluation order.

---

## 7. Proposed C++ file / namespace layout & implementation order

Namespace `gui::`. Suggested files:

```
gui/Form.{h,cpp}            // Form table, SelectWindow, GetObject* accessors
gui/Window.{h,cpp}          // Window struct, Create/Destroy/Resize/AddChild, surfaces
gui/Object.{h,cpp}          // Widget struct, AddToWindow, SetValueOrText, GetDataPtr
gui/Widget.{h,cpp}          // AllocSlot, LayoutBounds, slider/scrollbar/checkbox/sprite
gui/Input.{h,cpp}           // DispatchMouseClick, ProcessMouseDrag, HoverUpdate,
                            //   HandleKeyInput, SetFocus, Hotspot, Selection
gui/ZOrder.{h,cpp}          // Z-order insert/remove/raise
gui/RadioGroup.{h,cpp}      // dialog button groups
gui/text/TextDb.{h,cpp}     // LoadTextFile/BuildTextArray/labels/random tokens
gui/text/Format.{h,cpp}     // number/money/date/fixed-point/exponent helpers
gui/text/RichText.{h,cpp}   // RenderRichString, RenderFormattedMessage, icon/font
gui/text/Markup.{h,cpp}     // Window_ParseMarkupAndBuild ($[..]/$M/$T/_FONT/%-codes)
gui/Dialog.{h,cpp}          // ShowMessageBox* family + RunMessageBox
gui/Hud.{h,cpp}             // in-world HUD: click, labels, panels, edge-scroll
gui/Menu.{h,cpp}            // main menu / options / save-load / character select
gui/Panel.{h,cpp}           // info/stat/trade/training panels, tooltips
gui/widgets/...             // Book, MapView/CityMap, ChatConsole, Scroll, EventPanel
platform/Surface.{h,cpp}    // software surface + blit (shared w/ renderer cluster)
platform/Win32App.{h,cpp}   // CreateMainWindow, WndProc, PumpMessages, DIB present
```

**Implementation order** (each step independently testable):
1. `platform/Surface` + minimal blit/colorfill/clip (dependency for everything).
2. `gui/Form` + `gui/Window` + `gui/Object` + `gui/Widget` + `gui/ZOrder` — the
   retained-mode core; unit-test object creation, accessor returns, geometry.
3. `gui/text/Format` (small leaf helpers) — golden-test number/date/money output.
4. `gui/text/TextDb` — load real `.dat` text resources, verify label resolution.
5. `gui/text/RichText` + `gui/text/Markup` — **the hard part**; diff rendered
   surfaces against the original binary on canned inputs.
6. `gui/Input` + `gui/RadioGroup` — wire mouse/keyboard/focus; verify click ids
   (1210/1155) and slider math.
7. `gui/Dialog` (messagebox family) — first end-to-end interactive screen.
8. `gui/Hud`, then `gui/Menu` / `gui/Panel` / `gui/widgets/*` — bulk builders,
   mechanical port using the now-stable core API.
9. `platform/Win32App` — real OS window + present (or substitute SDL backend).
