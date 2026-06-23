# 10 — GUI: Windows, Widgets, Forms

This is the retained-mode GUI toolkit of `gilde.exe` — internally referred to in error
strings as `d2_*` (e.g. `d2_SetForm`, `d2_DrawShape`, `d2_CreateRadioGroup`). Every menu,
HUD, dialog, scrollbar, slider, radio group, text label and clickable button in the game is
built out of three nested record types living in fixed global arrays:

* a **Form** — a numbered *page* that owns a set of windows (a screen / dialog layout),
* a **Window** (type code `64`) — a rectangular container with its own draw surface and a
  child list,
* an **Object** / **Widget** — a leaf (button, label, slider, paintbox, scene, …) attached
  to a window.

The whole toolkit is *retained-mode*: you create the tree once, then every frame the engine
re-lays-out, hit-tests against, and re-renders the same persistent records. Selection is
*ambient* — almost every operation acts on a "currently selected form / window" held in
globals rather than taking an explicit handle. That is why `VIBE_Form_SelectWindow @0x41e4cc`
is the single most-called GUI function in the binary (**923 xrefs**): nearly every other
call first selects the form+window, then operates on "the selection".

Per frame, the GUI is driven from the main loop ([14 — Per-frame loop](14-per-frame-loop.md)):
input is hit-tested (`VIBE_Gui_HitTestObject`/`HitTestWindow`), clicks dispatched
(`VIBE_Widget_DispatchMouseClick @0x421594`), then the tree is composited onto a surface and
presented. Labels resolve their strings through the text layer
([11 — Text rendering](11-text-rendering.md)); window/object backgrounds are compressed
shape blits ([23 — Render](23-render-universe-chain.md)). The original blits via DirectDraw
surfaces and reads the mouse via DirectInput; in this port the surface backend becomes Vulkan
and the input layer becomes SDL (see *Platform boundary* below).

---

## 1. The data model

### 1.1 Global arrays (the storage)

Everything is statically allocated — no per-widget heap object; records are slots in big
flat arrays indexed by integer id. The base addresses and strides recovered from the
accessors:

| Global | Addr | Role | Element stride |
|---|---|---|---|
| `dword_62D258` | `0x62D258` | **current form index** (ambient selection) | — |
| `dword_62D230` | `0x62D230` | **current window index** (ambient selection) | — |
| `dword_62D298` | `0x62D298` | cached `&window_record[current window]` | — |
| `dword_676A60` | `0x676A60` | **Form table**: per-form header + window-id list | `171` dwords = `684` bytes / form |
| `dword_676A64` | `0x676A64` | form's window-id array (`= form_base + 1`) | — |
| `dword_676BE4` | `0x676BE4` | form's **window count** (`= form_base[+0x84]`, i.e. `[171*f]`) | — |
| `dword_676BF4` | `0x676BF4` | form's **base object id** (`= form_base[+0x94]`) | — |
| `dword_67EB80` | `0x67EB80` | **Window record array** | `238` dwords = `952` bytes / window |
| `dword_67EE00` | `0x67EE00` | window "alive/in-use" flag (`= window_base[+0x80]`) | — |
| `dword_67EDEC` / `dword_67EDFC` | `0x67EDEC` | parallel window side-tables | — |
| `dword_69FFB4` | `0x69FFB4` | **Object/Widget record pool base** (a *pointer*, set at init) | `740` bytes / object |
| `unk_695080` | `0x695080` | scene-object side-records (type `65`) | `348` bytes |
| `dword_695084/8` | `0x695084` | scene-object x/y, stride `87` dwords | — |
| `dword_676584` | `0x676584` | **radio-group table** (8 groups) | `35` dwords / group |
| `dword_67658C` | `0x67658C` | radio-group member-id arrays | `140` bytes / group |
| `dword_62D26C` | `0x62D26C` | **z-order / hit-test list** (512 slots, 2044-byte span) | 4 bytes |
| `dword_62D24C` | `0x62D24C` | highest live window index (hit-test upper bound) | — |
| `word_140642x` | `0x1406420` | "type-4" shape-button side records, stride `17` | — |

The fundamental addressing idiom, seen verbatim in every accessor:

```c
// pointer to the window record for window slot 'w' of the current form:
win  = &dword_67EB80[238 * dword_676A64[171*dword_62D258 + w]];
// pointer to object/widget record for object id 'o':
obj  = dword_69FFB4 + 740 * o;
```

### 1.2 The Form record (684 bytes, `dword_676A60[171*f ...]`)

A form is a thin owner. From `VIBE_Form_SelectWindow`, `VIBE_Form_Destroy @0x41da04` and the
accessors:

| Offset (dword) | Meaning |
|---|---|
| `+0` (`[171*f]`, byte `0x84` into header… see note) | first window-id slot of the list |
| window list | array of window indices (`dword_676A64`) |
| `[+0x64]` (`v3[100]`) | "form active" flag — `Form_Destroy` no-ops if zero |
| `[+0x69]` (`v3[105]`) / `[+0x6A]` (`v3[106]`) | owned offscreen surfaces (freed on destroy) |
| count | number of windows (`dword_676BE4`) |
| base obj | base object id of the form (`dword_676BF4`) |

`VIBE_Form_SelectWindow(form@eax, win@edx)` is the gatekeeper: if `form != 0` it sets the
ambient `dword_62D258`; it bounds-checks `win` against the form's window count (`< 0` or
`> count` ⇒ logs `"d2_SetForm(): invalid win number"` and returns 0); on success it resolves
`dword_62D230` (current window index) and caches `dword_62D298`, returning 1.

### 1.3 The Window record (952 bytes, type code `64`)

Built by `VIBE_Window_Create @0x419c38`. Fields are a mix of `int16` (16.16 fixed-point
hiword used as the integer coordinate — note the pervasive `>> 16`) and `int`/pointer dwords.
Offsets recovered from `Window_Create`, `Gui_HitTestWindow`, `Window_PositionCentered` and
`Paintbox_DrawShape`:

| Offset | Type | Meaning |
|---|---|---|
| `+0x02` (word 2) | i16 | **x** (origin) |
| `+0x04` (word 3) | i16 | **width** (`PositionCentered` reads `[+0x06]>>16` too) |
| `+0x06` (word) | i16 | **height** (`*(v2+6)>>16`) |
| `+0x08` (word 4) | i16 | **y** |
| `+0x0A` (word 5) | i16 | secondary height/extent |
| `[+3]` (`+0x0C`) | i32 | **style flags** (see below) |
| `[+8]`/`[+9]`/`[+10]` (`+0x20…`) | ptr | up to 3 **draw surfaces** (front / `style&1` back / `style&0x10..` extra) |
| `[+6]` (`+0x18`) | ptr | **child object-id array** (`0x600`-byte `d2:win_obj` alloc) |
| `+0x1A` (word 13) | i16 | **child count** (read everywhere as `*(win+26)>>16`) |
| `[+10]` (`+0x28`) | ptr | paintbox-blit shape state (used by `DrawShape`) |
| `[+11]` (`+0x2C`) | ptr | optional `0x17D0`-byte text buffer (`style&0x10`) |
| `+0x1C/0x1E/0x20/0x22` | i16 | layout clip rect (top/left/bottom/right) |
| `[+145]` (`+0x244`) | i32 | scroll **content height**; `[+146]/[+147]` scroll thumb pos / prev |
| `[+155]` (`+0x26C`) | i32 | **anchor object id** (the type-`64` widget mirroring this window) |
| `[+156]/[+157]/[+158]/[+160]` | i32 | active/raise/palette/"valid" flags (`[+160]` is the *paintbox valid* flag checked by `DrawShape`) |

**Window style flag bits** (the `a5`/`[+3]` mask in `Window_Create`):

| Bit | Effect |
|---|---|
| `0x001` | allocate a second (back) surface; sets `[+229]` blend constant |
| `0x004` | inherit palette `dword_62D2B8` into `[+158]` |
| `0x008` | auto-add a close/title object (`VIBE_Object_AddToWindow`, glyph `dword_62D2A8`) |
| `0x010` | allocate the per-window text buffer `[+11]` |
| `0x020` | scrollable: auto-add the two scroll-arrow objects (`+1`,`+2` glyphs); enables thumb-drag in dispatch |
| `0x200` | symmetric scroll-extent init (`[+145..147]`) |

`Window_Create` also spawns the window's own *anchor widget* (type `64`, slot from
`VIBE_Widget_AllocSlot`) so the window participates in the same object pool / z-order as its
children, links `obj[+0x74]=window_index+1024`, `obj[+0x08]`/`obj[+0x0C]`=back-pointers, and
finally sets the ambient selection (`dword_62D230`/`dword_62D298`) to the new window. The
global form-cycle counter `dword_62D2F0` wraps at 96 (max 96 concurrent windows).

### 1.4 The Object / Widget record (740 bytes)

`obj = dword_69FFB4 + 740*id`. The byte at **`+0x18` (offset 24) is the widget type code** —
the discriminator switched on in every destroy/layout/draw routine.

| Offset | Type | Meaning |
|---|---|---|
| `+0x08` (`[2]`) | i32 | id / window back-link (`window_index+1024` for type 64) |
| `+0x0C` (`[3]`) | ptr | **payload** — text record (type 65), label string ptr, window ptr (type 64) |
| `+0x0E` (word 7) | i16 | screen x (`>>16`) |
| `+0x10` (word 8) | i16 | layout x (set by `LayoutBounds`) |
| `+0x12` (word 9) | i16 | layout y |
| `+0x14` (word 10) | i16 | **text color** (`Widget_SetTextColor`) |
| `+0x16` (word 11) | i16 | width |
| `+0x18` (**byte 24**) | u8 | **TYPE CODE** (table below) |
| `+0x1A` (word 13) | i16 | **child count** (type-64 only) |
| `+0x1C/0x1E/0x20/0x22` | i16 | bound rect: top, left/bottom (`+30`), bottom/right (`+34`) — used by hit-test |
| `+0x24` (`[9]`) | i32 | **value / pressed state** (radio & toggle: `[9]==[10]`) |
| `+0x24` (`[36/40]` for type 69) | — | (overlap: `SetUserData` writes user data at `+0x24`=`[9]`) |
| `+0x24` (offset 36) | i32 | **user data** (`VIBE_Object_SetUserData`) / toggle current |
| `+0x28` (offset 40) | i32 | toggle previous |
| `+0x2C` (`[11]`) | ptr | parent window record |
| `+0x2C` (offset 44, `[11]`) | ptr | parent window (layout reads `[44]` clip) |
| `+0x34` (offset 52, `[13]`) | i32 | **hidden flag** (`SetVisibleRecursive`: `1`=hidden) |
| `+0x3C` (offset 60, `[15]`) | ptr | scene/atlas record (`[15][408]` = "is 3D scene") |
| `+0x44/0x48` (offset 68/72) | i32 | **clickable / toggle** flags (dispatch checks these) |
| `+0x70` (offset 112) | i16 | **fill color** (`Object_SetColor`, `CreateTextLabel`) |
| `+0x74` (offset 116) | ptr/i32 | **type-specific data**: label string buf (67), scene index (65), child array (64), shape id (71), radio slot |
| `+0x78` (offset 120) | i32 | slider min / type-69 shape ptr / type-71 alloc |
| `+0x7C/0x80` (124/128) | i32 | slider value / max |
| `+0x88` (offset 136) | i32 | slider current step |

**Widget type codes** (byte `+0x18`; the switch in `VIBE_Widget_DestroyByType @0x414f98` and
`LayoutBounds @0x413220` enumerate them):

| Code | Hex | Widget | Notes |
|---|---|---|---|
| 4 | `0x04` | **shape button** | side record `word_1406420[17*idx]`; press state via `SetButtonState`; sprite from shape atlas |
| 9 | `0x09` | sprite/child-list node | `[+0x2C]` group ptr |
| 64 | `0x40` | **Window anchor** | has child array `[+0x74]`, recursive layout & visibility |
| 65 | `0x41` | **3D scene / animated object** | `unk_695080[348*idx]`; `GetObjectAnimPtr` requires this code; color → `dword_6951B4` |
| 66 | `0x42` | scroll-arrow object | spawned by `GameObject_SpawnWindowObject @0x40e1c8` |
| 67 | `0x43` | **text label** | `CreateTextLabel @0x41b494`, `[+0x74]` = `0x101`-byte string |
| 69 | `0x45` | **edit / value field** | text at `[+124]`, value at `[+120]`, flags `[+132]` (`0x10` ⇒ extra arg) |
| 71 | `0x47` | owned buffer widget | `[+0x78]` heap buffer, freed on destroy |

The text payload sub-record (type 65/69, at `obj[+0x0C]`) carries flags at `+38`
(`&1`=copy-string, `&2`=value-record, `&0x40`/`&0x80`=slider/stepper) and an inline string
buffer at `+40`. Note the engine copies strings **two bytes at a time** (a wide/interleaved
copy idiom) in `CreateTextLabel`, `SetObjectValueOrText` and `InsertNamedNode` — preserve it
exactly; it stops on the first NUL byte and caps at `0xFE`.

---

## 2. Core operations

### 2.1 Selection & lookup

* **`VIBE_Form_SelectWindow @0x41e4cc`** — set ambient form+window (§1.2). Returns 0 + logs on
  bad window number. Called before virtually every other GUI op.
* **`VIBE_Form_GetObjectPtr`** — `obj_record_ptr` for child `a1` of window `a2` of the current
  form. Validates `a1 ≤ window.child_count`, indexes the window's child array
  (`win[6][a1]`), returns `dword_69FFB4 + 740*childId`.
* **`VIBE_Form_GetObjectDataPtr`** — same, but returns the object's *data* via
  `VIBE_Object_GetDataPtr` (heap payload, for editable fields).
* **`VIBE_Form_GetObjectAnimPtr`** — same, but only for type-`65` objects (asserts byte
  `+0x18==65`); returns the animation record at `obj[+0x0C]`.
* **`VIBE_Object_SetUserData @0x41db…`** — stores `a2` at `obj[+0x24]` (offset 36) of object
  `base_obj_id + a1` of the current form (uses the `dword_676BF4` base — note this addresses
  by *global object id*, not per-window child index).

### 2.2 Mutators

* **`VIBE_Object_SetColor @0x41e614`** — writes fill color to `obj[+0x70]`; if the object is a
  scene record (type 65) also propagates into the scene color table `dword_6951B4`, if type 64
  into `word_67EDFC`.
* **`VIBE_Widget_SetTextColor @0x412530`** — sets `obj[+0x14]` then calls
  `VIBE_Widget_RefreshText` to re-rasterize.
* **`VIBE_Object_SetVisibleRecursive @0x41dd98`** — sets `obj[+0x34]` = `(a2==0)` (hidden when
  arg is 0); for type-`64` windows it recurses over the child array, so hiding a window hides
  its whole subtree.
* **`VIBE_Object_SetValueOrText @0x41dfec`** / **`VIBE_Form_SetObjectValueOrText`** — the
  universal "set the contents of a widget" entry. Behavior keys off the type code:
  * type **65** with payload flag `&1`: copy `a2` as a string into the inline buffer.
  * type 65 with flag `&2`: store value/extra into the payload (`+24/+28/+296`) and re-run the
    **slider quantization** (`VIBE_Slider_ComputeStep`, clamps to ≤25 steps).
  * type **69**: store text/value/extra at `+124/+128/+120` (and `+140` if flag `&0x10`).
  * otherwise (clickable/toggle, `[68]||[72]`): set value at `+36`/`+40`.
* **`VIBE_Widget_SetButtonState`** — for type-`4` shape buttons, toggles the pressed/visible
  bytes in the `byte_1406424[17*idx]` side record.

### 2.3 Construction

* **`VIBE_Window_Create @0x419c38`** — finds a free window slot, fills geometry + style,
  allocates 1–3 draw surfaces (`VIBE_Surface_Create`) and the child-id array, spawns the
  anchor widget (type 64), optionally adds close/scroll objects, sets ambient selection.
* **`VIBE_Object_CreateTextLabel @0x41b494`** (`d2:ShowTxt`) — allocates a slot
  (`Widget_AllocSlot`), a `0x101`-byte string buffer at `+0x74`, copies the label text,
  stamps type `67`, default color `dword_62D2B0`, and sizes the bound rect via
  `VIBE_Property_Get`.
* **`VIBE_Widget_AddSpriteToWindow @0x41217c`** — creates a sprite widget at window-relative
  `(x,y)` (adds the window origin from the window record) via `VIBE_Widget_CreateSprite`, then
  `VIBE_GameObject_AttachToWindow` links it into the window's child array + z-order.
* **`VIBE_GameObject_SpawnWindowObject @0x40e1c8`** — builds a type-`66` object (the scroll
  arrows / glyph objects) with two `GameLogic_Objects` glyph children.
* **`VIBE_GameObject_AttachToWindow @0x40e57c`** — the child-attach primitive: bounds-check
  (≤384 objects/window else logs `"Too many objects on window!"`), `VIBE_ZOrder_InsertObject`,
  copy bounds, append id to `win[+0x60]`/`win[+0x18]` array, bump child count `win[+0x1C]`.
* **`VIBE_RadioGroup_Create @0x412728`** (`d2_CreateRadioGroup`) — allocates one of **8** group
  slots (`dword_676584`, stride 35); stores up to **32** member object ids; marks each member
  as toggle-clickable (`obj[+0x44]=1`, clears bit `obj[+444]&~2`). Returns group index or −1.
* **`VIBE_RadioGroup_SetEnabled`** — writes the disabled flag `obj[+0x34]` (offset 56) to every
  member of group `a1`.
* **`VIBE_Object_InsertNamedNode @0x42e…`** — loads a binary animation
  (`VIBE_ModelIo_LoadBinaryAnimation`) and links it into the scene-node linked list
  (`dword_13FC8E4`); used for 3D-scene widgets, not flat UI.

### 2.4 Layout & positioning

* **`VIBE_Widget_LayoutBounds @0x413220`** — recomputes a widget's clip/bound rect from its
  `(x,y,w,h)` and parent, clamping to screen extents `dword_69FFB8/69FFBC`. For type-`64`
  windows it **recurses** to relayout all children relative to the new window origin (the core
  of "move a window ⇒ move its contents"). For type-65/4 it mirrors coords into the scene/shape
  side tables.
* **`VIBE_Window_PositionCentered @0x41d764`** — computes a centered origin (`a2` bit `1`=center
  X about `dword_69FFA4`, bit `2`=center Y about `dword_69FFA0`) and calls `LayoutBounds` on the
  window's anchor object `win[155]`.
* **`VIBE_Window_AutoFitHeight`** — grows the window height to enclose the lowest child
  (`child.y + child.h`) + 8 px padding, then `VIBE_Window_Resize`.

### 2.5 Per-frame click dispatch & hit-testing

This is the GUI's input half of the per-frame loop ([14](14-per-frame-loop.md)):

* **`VIBE_Gui_HitTestWindow`** — walks live windows top-down (from `dword_62D24C`), returns the
  window whose rect (and title-bar band `+30..+32`) contains the mouse and that is visible
  (`[+56]==0`). Sets `dword_62D22C` = hit window.
* **`VIBE_Gui_HitTestObject`** — walks the **z-order list** `dword_62D26C` from the top
  (offset 2044 downward), returns the topmost object whose bound rect contains the cursor and
  that is not hidden/occluded (`[13]`,`[14]`==0, scene-not-busy `[15][408]==0`). Sets
  `dword_62D22C` (hit object) and `dword_62D290` (its parent window).
* **`VIBE_Widget_DispatchMouseClick @0x421594`** — the click pump, run once per frame:
  1. ticks the GUI clock (`VIBE_GameTick_MainLoop`) and processes any active drag
     (`VIBE_Widget_ProcessMouseDrag`);
  2. resolves the hit object's index within its window (`dword_75BF10`);
  3. `VIBE_Widget_HoverUpdate` for hover state;
  4. if the mouse button is down (`dword_672228`) and there's a hit (`dword_75BF40 != -1`):
     fires the widget — **toggles** (`obj[+36] ^= 1`) if a toggle, plays the click sample
     (`VIBE_Audio_StartVoiceSample`), and if the hit belongs to a **radio group** clears the
     other members and sets this one (the mutually-exclusive selection logic, scanning all 8
     groups);
  5. scans the 32-slot drag-handle table `unk_75BA38` for a grabbed window edge;
  6. if a scrollable window (`style&0x20`) is hit on its scrollbar track, computes the new
     scroll value/thumb (`win[146]`, `win[142]`, `win[13]`) and flags a redraw.

  The mouse position itself (`dword_75BF46`, `word_75BF4A`) is supplied by the input layer
  (DirectInput in the original → **SDL** in this port).

### 2.6 Rendering

* **`VIBE_Window_RenderEntityList @0x4134f0`** — composites the GUI for the frame: draws all
  entities into the back buffer `dword_62D210`, copies it to the front buffer `dword_62D218`
  (`VIBE_Result_Handler_Interaction` is the surface blit), runs up to 32 fade overlays
  (`dword_672280[]`), and presents (`VIBE_Render_PresentFrame`).
* **`VIBE_Window_RenderEntityScene @0x41523c`** — the 3D-scene variant: color-fills the scene
  buffer, runs fades, presents.
* **`VIBE_Paintbox_DrawShape / DrawShapeDirect / DrawShapeClipped`** — blit a registered shape
  into the current window's paintbox surface (`win[10]`); errors
  (`"d2_DrawShape:Invalidate window!"` / `"…has no paintbox!"`) if the window has no valid
  paintbox. Shapes come from `VIBE_Shape_LoadAndRegister`.
* **`VIBE_Widget_DrawBackgroundSprite`** — blits the cached window-background sprite
  (`VIBE_State_Update` → `VIBE_Animation_Basic`) onto buffer `dword_62D21C`.
* **`VIBE_DragCursor_SetMode`** — sets the global drag-cursor mode `word_62D310` (e.g. while a
  window is being dragged).

These all bottom out in the surface/blit primitives that the original implemented on
DirectDraw and this port reimplements on **Vulkan** ([23 — Render](23-render-universe-chain.md)).

### 2.7 Teardown

* **`VIBE_Widget_DestroyByType @0x414f98`** — frees one widget according to its type code: scene
  records (65) → free `unk_695080` slot; labels (67)/buffers (71) → `VIBE_Memory_FreeDebug` the
  payload; edit fields (69) → destroy surface; windows (64) → `VIBE_Window_Destroy`;
  type-4 → release shape-anim slot. Then unlinks the widget from its parent's child array
  (memmove-compaction), from the z-order list, decrements `dword_62D24C`, and zeroes the slot.
* **`VIBE_Form_Destroy @0x41da04`** — destroys an entire form: iterates the form's window list
  in reverse calling `Widget_DestroyByType` on each live window, frees the form's two owned
  surfaces (`[+0x69]/[+0x6A]`), then zeroes the 684-byte form record.

---

## 3. Lifecycle summary

```
Build  : Window_Create (alloc slot, surfaces, anchor widget, child array, style objects)
          └─ Object_CreateTextLabel / Widget_AddSpriteToWindow / GameObject_SpawnWindowObject
               └─ GameObject_AttachToWindow (z-order + child array)
          └─ RadioGroup_Create (group toggles)
          └─ Window_PositionCentered / AutoFitHeight  → LayoutBounds (recursive)

Per frame (from the main loop, doc 14):
  Select : Form_SelectWindow (set ambient form+window)
  Input  : Gui_HitTestWindow / HitTestObject  → Widget_DispatchMouseClick
  Mutate : Object_SetValueOrText / SetColor / SetVisibleRecursive (game logic reacts)
  Draw   : Paintbox_DrawShape / DrawBackgroundSprite → Window_RenderEntityList → PresentFrame

Teardown: Form_Destroy → Widget_DestroyByType (per window) → free surfaces, zero records
```

---

## Platform boundary

* **Drawing** — every blit/fill (`VIBE_Surface_Create/Destroy/ColorFill`,
  `VIBE_Result_Handler_Interaction`, `VIBE_Animation_Basic`, `VIBE_Render_PresentFrame`) was a
  **DirectDraw** surface operation in the original; this port routes them through the
  `IGraphicsDevice` shim onto **Vulkan**. The GUI code above is unchanged — it manipulates
  surface *handles* and integer rects, not GPU state.
* **Input** — `Widget_DispatchMouseClick` and the hit-testers read the mouse cursor from
  globals (`dword_75BF46`, `word_75BF4A`, button flag `dword_672228`) populated by the input
  layer, which was **DirectInput** and is **SDL** in this port.
* **Audio** — the click feedback (`VIBE_Audio_StartVoiceSample`) was Miles Sound System;
  it is **SDL** audio here.

---

## See also

* [11 — Text rendering](11-text-rendering.md) — how labels/edit fields resolve and rasterize strings.
* [14 — Per-frame loop](14-per-frame-loop.md) — where dispatch + render are invoked each frame.
* [23 — Render](23-render-universe-chain.md) — the shape/sprite blit and surface-present chain.
