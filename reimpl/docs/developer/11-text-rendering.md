# 11 — Text & rich-string rendering

How `gilde.exe` turns *markup-annotated, localized* strings into pixels. The engine has
its own miniature **rich-text markup language** (`$`/`%`/`{` escape codes), a binary
**localized string database** (`textbin_german\*.res`), a hand-rolled **5×7 bitmap
font**, and a separate GDI path for OS-rendered editbox text. Everything below is read
directly from the IDA decompilation — the binary is the source of record.

> Platform boundary: the glyph rasteriser blits monochrome bitmaps straight into a
> locked DirectDraw back-buffer (`VIBE_Render_DrawGlyph @0x434d0c`). In the
> reimplementation this is the **DirectDraw → Vulkan** swap point — the *markup parser,
> the .def/.res loaders, the font tables, and the layout math stay 1:1*; only the final
> pixel store changes. The editbox path (`VIBE_Surface_DrawText`) uses Win32 GDI
> `TextOutA` and is the **Win32 → SDL/native** swap point.

Cross-links: [03 — Config & locale](03-config-ini-paths-locale.md) (the `german`
locale folder and VFS roots), [10 — GUI widget/form](10-gui-widget-form.md) (widgets
feed `$`-markup strings into the layout engine), [23 — Render universe
chain](23-render-universe-chain.md) (back-buffer acquire / present around glyph blits).

---

## 1. The pieces

| Component | Address | Role |
|---|---|---|
| `VIBE_Text_LoadDefinitionFile` | `0x44b8f4` | Opens the master `.def` text-engine definition file; enumerates referenced text files. |
| `VIBE_Text_LoadTextFile` | `0x44dba0` | Loads one `textbin_german\<name>.res` binary string blob into the global string table. |
| `VIBE_Text_BuildTextArray` | `0x44bb5c` | Builds the runtime label/index arrays from loaded text. |
| `VIBE_Text_RenderRichString` | `0x59d6e8` | **Rich-string preprocessor** — expands `$`/`%`/`{` markup against the string DB (1172 xrefs). |
| `VIBE_Text_RenderFormattedMessage` | `0x59f99c` | **printf-style** variant — same markup engine plus C-style format specifiers over `va_list` (918 xrefs). |
| `VIBE_Window_ParseMarkupAndBuild` | `0x416720` | GUI text-flow layout engine: parses widget body markup, wraps lines, places sprites/buttons/icons, selects the active `_FONT`. |
| `VIBE_Font_InitGlyphTable` | `0x42e350` | Builds the ASCII→glyph-index map (`byte_75FB50`). |
| `VIBE_Render_DrawText` | `0x434e18` | Renders a whole ASCII string: per-char advance + colour pack. |
| `VIBE_Render_DrawGlyph` | `0x434d0c` | Blits one 5×7 monochrome glyph into the back-buffer. |
| `VIBE_Surface_DrawText` | `0x4243d8` | GDI `TextOutA` path for editbox/system-font text. |
| `VIBE_String_GetDelimitedField` | `0x44ad48` | Splits a packed multi-field DB record into its grammatical sub-strings (noun cases). |

Global state that ties them together:

| Global | Address | Meaning |
|---|---|---|
| `dword_8C36B0[]` | `0x8c36b0` | Master pointer table: string-id → C-string. Indices `< 0x4000` are DB ids; `≥ 0x4000` are treated as raw pointers. |
| `dword_8C36AC[]` | `0x8c36ac` | Per-id string pointers as relocated from the `.res` blob. |
| `byte_767EB0[]` | `0x767eb0` | Per-id **grammatical-gender / category byte** (`0xFF` = plain string, else a noun class). |
| `byte_8D36B0[]` | `0x8d36b0` | Per-id 80-byte metadata record (declension table indices). |
| `dword_62EB24` | `0x62eb24` | Count of loaded string ids. |
| `dword_B537B4` | `0xb537b4` | "text engine ready" flag (set by `LoadDefinitionFile`). The renderers no-op if 0. |
| `dword_8C379C / 8C3790 / 8C37A8` | — | Declension/article tables for the three grammatical forms used by `$s`/`$G`. |
| `unk_62D59C` | `0x62d59c` | 7-bytes-per-glyph **bitmap font** (5×7). |
| `byte_75FB50` | `0x75fb50` | ASCII → glyph-index map (built by `VIBE_Font_InitGlyphTable`). |

---

## 2. The localized string database (`.def` → `.res`)

### 2.1 The `.def` master file — `VIBE_Text_LoadDefinitionFile @0x44b8f4`

Called with a path to the text-engine **definition file**. Behaviour:

1. Derives the base directory by finding the last `\` in the path (`VIBE_Util_StrChr(a1, '\\')`)
   and copies it to `unk_767CB0`; if there's no backslash it falls back to the baked-in
   default directory at `unk_6189D4`.
2. Opens the file in text mode (`VIBE_File_OpenStream(path, "rt")`). On failure it pops a
   Win32 `MessageBoxA(0, "Could not open Textfiledefinition!", "Error", MB_ICONHAND)` and
   returns 0. *(MessageBoxA → SDL message box under Rule 4.)*
3. Reads the file **line by line** via `VIBE_Text_ReadLine @0x5e9e30`. Each non-empty line
   is expected to contain a **double-quoted token**: the loop scans for the first `"`,
   then the closing `"`, NUL-terminates it, and chops at the first `.` (`VIBE_Util_StrChr(v17, '.')`)
   so `"name.ext"` yields the bare base name `name`. Each name is stored into a 512-byte
   slot of a 64 KB on-stack table (`v26[65536]`, stride `0x200`), up to `0x10000` bytes.
4. After the file ends (stream EOF bit `*(stream+12) & 0x10`), it closes the stream and
   calls `VIBE_Text_LoadTextFile` once **per collected name**. When all succeed it sets
   `dword_B537B4 = 1` (text engine ready) and returns 1.

So the `.def` is a plain-text index: one quoted `"<basename>.<something>"` per line,
naming the `.res` blobs to pull in.

### 2.2 The `.res` binary blob — `VIBE_Text_LoadTextFile @0x44dba0`

For each base name it opens, via the **VFS** (`VIBE_Vfs_OpenFile @0x450bc8`), the file:

```
textbin_%s\%s.res     ; fmt aTextbinSSRes @0x618d74, locale = "german" @0x62eae4
```

The locale folder is hard-wired to `german` (the only shipped locale; see
[03 — Config & locale](03-config-ini-paths-locale.md)). The slot bookkeeping lives in
`byte_77BEB0` (112-byte records, max 128 loaded files — returns 0 past that).

The `.res` layout, read sequentially with `VIBE_Vfs_ReadStream`:

| Order | Bytes | Field |
|---|---|---|
| 1 | 4 | `count` (`v28`) — number of strings in this file |
| 2 | 4 | base offset A (stored at record `+96`) |
| 3 | 4 | base offset B (stored at record `+100`) |
| 4 | `4*count` | **offset table** (`v31`) — per-string offsets into the text pool |
| 5 | `80*count` | per-string 80-byte metadata records → `byte_8D36B0[]` (declension data) |
| 6 | `1*count` | per-string **gender/category bytes** → `byte_767EB0[]` |
| 7 | 4 | `poolSize` (`v29`) |
| 8 | `poolSize` | the **text pool** (all NUL-separated strings) → newly `Alloc`'d buffer |

The per-string offsets are then **relocated** into `dword_8C36AC[]` by adding the text-pool
base, so `dword_8C36B0[id]` resolves directly to a `char*`. `dword_62EB24` is bumped to the
new highest id. Failure to open logs `"Could not open textfile:%s"` via
`VIBE_ErrorLog_ReportMessage`.

Each logical string is actually a **packed multi-field record**: a noun and its
grammatical variants (nominative/genitive/etc., singular/plural, with article) stored as
consecutive NUL-terminated fields. `VIBE_String_GetDelimitedField @0x44ad48` walks `N`
NUL-separated fields and returns the requested one; mode `4` returns the whole record if a
field is empty, mode `8` advances 4 extra fields (selecting the plural/declined block).
This is what makes the markup able to inflect nouns correctly in German.

---

## 3. The rich-string markup language

Both `VIBE_Text_RenderRichString @0x59d6e8` and `VIBE_Text_RenderFormattedMessage @0x59f99c`
are **string preprocessors**: they copy the source (either a literal pointer, when the id
arg ≥ `0x4000`, or `dword_8C36B0[id]` for a DB id) into a 6096-byte working buffer
(`0x17D0`/`0x17C0` size limits enforced) and then scan it for escape characters,
**rewriting the buffer in place** — every expansion does a `VIBE_Util_MemMove` to open/close
a gap and a `qmemcpy` of the replacement. The final expanded plain text is copied to the
caller's output buffer. Overflow past `0x17CB` aborts the whole render (`return`).

### 3.1 The scanner

The main loop reads one char at a time and only reacts to three **escape introducers**:

```
'%' (0x25)   '$' (0x24)   '{' (0x7B)
```

Anything else is literal text and is skipped. After an introducer an optional **single
decimal digit** argument is parsed (`c-'0'`, `0..9`, else "no arg" = `-1`). The next char
is the **token letter**, dispatched through a big nested binary search on its ASCII code.
The grammatical-state variable `v183` (0..6) is set by case modifiers (`$t/$b/$e/$u/$E`)
and consumed by the next noun-emitting token (`$s`/`$G`) to pick the right declension and
optionally upper-case the first letter.

### 3.2 Markup reference

Letters are case-sensitive. "arg" = the optional preceding digit.

| Token | Hex | Effect (from decompile) |
|---|---|---|
| `%%` | `25 25` | Literal `%`. (In `RenderFormattedMessage` the first `%` is turned into byte `0x16` as a guard, then collapsed.) |
| `%i` | `25 69` | printf integer from `va_list`; arg sets minimum width (`"%0<arg>i"` via `a0Ii_1 @0x627e88`). Sets the "multi/zero" flag (`v182`) when value ≠ 1. |
| `%I` | `25 49` | DB-indexed integer **with thousands separators** (`.` every 3 digits, built into `v161`); negative prefixed `-`. Used for money-ish counts. |
| `%a` | `25 61` | Emits the integer **plus a literal `0x14` icon-marker byte** (`"%i%c"`, char 20) — an *inline icon placeholder* consumed later by the redraw/icon pass. Bare `%a` emits just `char 20` (`"%c"`). |
| `%f` | `25 66` | printf float/double from `va_list`; arg = precision (builds `"%.<p>f"` from fragments `%.` `f` `.`). |
| `%t` | `25 54` | **Money / amount with separators** — `VIBE_Money_FormatWithSeparators(*va, byte_6477A1, dst)` (only when preceded by `%`). |
| `$s` | `24 73` | **Insert DB string by id** (`*va` = id). Looks up `dword_8C36B0[id]`, splits the correct grammatical field via `GetDelimitedField`, applies the pending case-state `v183` (concatenating article/adjective from the declension tables `8C379C/8C3790/8C37A8` with the trimmed preceding word). |
| `$G` | `24 47` | Like `$s` but for the **"named object/title"** family: indexes into the metadata at `id*14+1078`, optionally appends `" >name<"` (`aSS_9 @0x627ee4`) when an override name is present. |
| `$w` | `24 77` | Insert the **workshop/profession noun** for an actor: `dword_8C584C[2*(... >>16)]` field, via the actor record stride `189`. |
| `$v` | `24 76` | Consume one `va_list` arg silently (a guard/skip for a value that was already inlined). |
| `$i` | `24 69` | Inline integer (DB or va), arg = width; the sub-letters `i/c/b/a/n` are treated as *already-handled icon/format markers* and skipped (`v2 += 2`). |
| `$D` | `24 44` | **Date**: packs `*va` via `VIBE_GameTime_PackToRecord`, looks up the season name `dword_8C37E0[season]` and year, emits `"<season> <year>"` (`aSI_0 @0x627edc`). |
| `$C` | `24 43` | **Clear** — zero-fills a 128-byte scratch region (`VIBE_Light_SetGrayColorThunk(0,128,a1)`); a reset/erase control. |
| `$t` | `24 74` | Set case-state **6** (sentence-with-trailing-word, capitalised) for next noun. |
| `$b` | `24 62` | Set case-state **3**. |
| `$e` | `24 65` | Set case-state **5**. |
| `$u` | `24 75` | Set case-state **1**. |
| `$E` | `24 45` | Set case-state **6** (the upper-case-first variant; `v161[0] = toupper(...)`). |
| `{r<n>}` | `7B 72 ..` | **Random text**: pick one of `n` consecutive variants of a tagged string group. Reads the digit after `{`, scans for the closing token, parses the count with `VIBE_Util_ParseInt`, searches `byte_767EB0[]` for the matching group tag, then `dword_8C36B0[k + RandomModulo(count)]`. Missing group logs `"io_Text(): Missing Random-Text (e.g. {r1})..." @0x627eac`. |

Notes:
* The `0x14` (`char 20`) byte emitted by `%a` is an **inline-icon sentinel**; the rich
  renderer counts these and bails with `"io_Text(): too many icons before redraw!"
  @0x627e50` if too many accumulate before the surrounding window redraws.
  `VIBE_Text_GetCurrentIconWord @0x5a3418` exposes the current icon word (`word_649D48`).
* `VIBE_Text_FormatItemLabel @0x59c2e4` / `…WithIcon @0x59ccf4` are helper formatters the
  `%n` path calls to build item-name labels (with the `n*Ii` width form).
* `VIBE_Text_TrimTrailingSpace @0x59c270` removes the trailing word so the declension
  logic can re-attach the correctly-cased article/adjective.

### 3.3 GUI-layout markup (`VIBE_Window_ParseMarkupAndBuild @0x416720`)

The widget/form layout engine consumes the **same `$`/`%` introducers** but adds
*formatting/flow* tokens (it both expands text and positions child sprites). These appear
in window body strings produced by the GUI builders in
[10 — GUI widget/form](10-gui-widget-form.md):

| Token | Effect |
|---|---|
| `$M` / `%M` | New paragraph — vertical break of `3/2 × line-height` (or the saved sprite height). Cursor returns to left margin. |
| `$A<n>` | Line break of `n × line-height` (advance n lines). |
| `$Y` | Variant break (`0x59`) — line break preceded by `$`. |
| `$<` / `$>` | Set the **left / right margin** (indent) to `2·n·M`-units, where `M` is the font's space width. |
| `$X<n>` | Set a **tab column** at `3·n·M` units; pads the cursor out to it. |
| `$L` | Reset margins/justification (left). |
| `$R` | Right/region marker — `v204 = 64` flow flag. |
| `$F<n>` | **Set text colour** from the palette table `dword_40DDB0[]` (3 bytes RGB per index); `$F9` (`v203==9`) clears the explicit colour (`byte_62D300 &= ~0x40`). |
| `$[ ... $]` | **Embedded label / editable field block** — the text between is captured as an input field's caption (`asc_610EE8 "$["`, `asc_610EEC "$]"`). Used to lay out edit boxes and buttons inline. |
| `$<digit><c\|i\|b\|t\|a\|n>` | **Inline sprite/button/icon**: places a child object (`VIBE_Object_AddToWindow`/`AddButtonLabel`/`Widget_CreateSprite`) at the cursor; sub-letter selects centred/icon/button/text-field/active styling; `[label]` after it supplies the caption. |
| `%c` | Centre justification for the following run. |
| `%C` | Clear the buffer and restart layout from the top. |
| unknown | Logs `"Unknown textparameter: %%%c" @0x610ef0`. |

The active **font** for a widget is chosen up front:
```c
v2 = window.fontOverride ? window[+634]>>16 : VIBE_Property_Validate("_FONT");  // aFont @0x610dc0
VIBE_State_Finalize(v2);   // 0x41e57c : selects font, sets line-height dword_69FFB0 = font[+46]
```
`_FONT @0x610dc0` (10 xrefs) is the **named GUI property** that resolves to a font sprite
resource; `VIBE_State_Finalize @0x41e57c` reads its line height (`*(u16*)(font+46)`) and
sets glyph spacing constants `dword_62D270` (4 or 8) used during wrapping. Word-wrap
measures each word with `VIBE_Property_Get(word, font)` and breaks at the right margin,
hyphenating on `~`/`-`.

---

## 4. The font system and glyph rasteriser

### 4.1 Glyph index map — `VIBE_Font_InitGlyphTable @0x42e350`

Zeroes a 256-byte table (`byte_75FB50`) then fills the ASCII→glyph map: space → 1,
`A`..`Z` → 2..27, `a`..`d` → 28..31, then a packed block
`qmemcpy(t+101," !\"#$%&'()*+,-./012345",22)` for the high punctuation and
`qmemcpy(t+33,"EHIJKB8>?L<:;96PQRSTUVWXY=GCMDFN",32)` for the digit/punctuation reorder,
plus `\` → 55, `[` → 64, `]` → 65, `_` → 79, `~` → 90. Unmapped chars stay 0 (blank).

### 4.2 Glyph bitmap format — `unk_62D59C @0x62d59c`

Each glyph is **7 consecutive bytes** (`7 * byte_75FB50[ch]` stride). Each byte is one
**5-pixel scan-row**, MSB-aligned at mask `0x10` (bit 4 = leftmost pixel). So a glyph is a
**5-wide × 7-tall** monochrome bitmap. Example, the first non-blank glyph (index 1, space’s
neighbour) `15 0A 15 0A 15 0A 15` is a checkerboard test pattern; `0x1F` rows are solid.

### 4.3 String rasteriser — `VIBE_Render_DrawText @0x434e18`

```c
ok = VIBE_Render_AcquireBackBuffer();          // lock DD surface (→ Vulkan)
color = packRGB(r,g,b);                         // via per-channel shift tables byte_762719..E
for (each char c in string) {
    if (c != ' ') VIBE_Render_DrawGlyph(c, x, color, y);
    x += 6;                                      // fixed 6-px advance (5 glyph + 1 gap)
}
// flush pending icon op (byte_762721 dispatch), reset dword_7626F0
```
Colour is packed from the back-buffer's pixel format using the shift/loss tables
(`byte_762719`=r-shift … `byte_76271E`=g-shift), so it works in both 16-bit and 32-bit
modes.

### 4.4 Glyph blit — `VIBE_Render_DrawGlyph @0x434d0c`

```c
bits = &unk_62D59C[7 * byte_75FB50[ch]];        // 7 rows
if (x+5 > screenW || y+7 > screenH) return;     // clip
base = pitchX*x + pitchY*y + surfBase;
for (row = 0; row < 7; ++row) {
    mask = 0x10;                                 // leftmost of 5 pixels
    for (col = 0; col < 5; ++col) {
        if (bits[row] & mask) put_pixel(base + col, color);
        mask >>= 1;
    }
    base += pitchY;
}
```
Two code paths: `dword_762714 == 16` writes `_WORD` pixels, `== 32` writes `_DWORD`
pixels. **This is the only function that touches surface memory** for in-engine text, and
is the precise DirectDraw→Vulkan substitution point (Rule 3): replace `put_pixel` with a
write into the Vulkan staging image / glyph atlas; the 5×7 source data and advance stay
identical.

### 4.5 The GDI editbox path — `VIBE_Surface_DrawText @0x4243d8`

Distinct from the bitmap font. Used where the engine wants the **OS system font** (text
input fields). It `GetDC`s the DirectDraw surface, then:
```c
SetBkMode(hdc, TRANSPARENT);
SetTextColor(hdc, (g<<8) | b | (r<<16));
TextOutA(hdc, x, y, lpString, strlen(lpString));
ReleaseDC(...);
```
This is pure Win32 GDI and is the **Win32 → SDL** swap (Rule 4): render the string with an
SDL/native font into the target surface. Note it can temporarily un-/re-compress the
target surface around the draw (`VIBE_Decompression_Finalize` / `VIBE_DecompressState_Blob`).

---

## 5. How a string gets from a widget to the screen — end-to-end

```
GUI builder (doc 10)                                   doc 03: locale = "german"
   └─ builds a window body string with $/%-markup
        and/or a DB id (e.g. id < 0x4000)
   │
   ▼
VIBE_Window_ParseMarkupAndBuild @0x416720
   ├─ selects _FONT (VIBE_Property_Validate / VIBE_State_Finalize) → line height
   ├─ expands inline $s/%i/… (shares the markup vocabulary)
   ├─ word-wraps + places child sprites/buttons/fields ($[ $] $< $> $X $F …)
   └─ stores per-line text + width into the window's line array
   │
   ▼  (per modal message / formatted text)
VIBE_Text_RenderRichString @0x59d6e8  /  VIBE_Text_RenderFormattedMessage @0x59f99c
   ├─ src = (id>=0x4000) ? (char*)id : dword_8C36B0[id]          ← string DB
   ├─ scan for % $ {  → expand tokens in place (memmove + qmemcpy)
   │     └─ $s/$G → VIBE_String_GetDelimitedField (German declension)
   │     └─ {r<n>} → VIBE_Math_RandomModulo over a tagged group
   └─ copy expanded plain text to caller buffer
   │
   ▼  (rasterise)
VIBE_Render_DrawText @0x434e18
   ├─ AcquireBackBuffer (render chain, doc 23)
   ├─ pack colour for surface pixel format
   └─ per char → VIBE_Render_DrawGlyph @0x434d0c  → 5×7 blit  → DirectDraw/Vulkan surface
```

The **string database** is populated once at startup: `VIBE_Text_LoadDefinitionFile`
(`.def`) → many `VIBE_Text_LoadTextFile` (`textbin_german\*.res`) →
`dword_8C36B0[]` / `byte_767EB0[]`. From then on every UI string is just an **id** that the
renderers resolve and the markup engine inflects and lays out.

---

## 6. Reimplementation checklist

* **Keep 1:1:** the markup token table (§3.2/§3.3), the `.res` binary layout (§2.2), the
  declension field rules in `GetDelimitedField`, the 5×7 glyph data and the ASCII→index map
  (§4.1/§4.2), the 6-px advance, and the `0x17CB`/6096 buffer limits and overflow aborts.
* **Swap (Rule 3):** `VIBE_Render_DrawGlyph`'s `put_pixel` → Vulkan image/glyph-atlas write;
  `AcquireBackBuffer`/present hooks come from [23 — Render](23-render-universe-chain.md).
* **Swap (Rule 4):** `VIBE_Surface_DrawText`'s GDI `GetDC/SetTextColor/TextOutA` →
  SDL/native font; `MessageBoxA` in the `.def` loader → SDL message box.
* **VFS, not raw I/O:** `.def` via `VIBE_File_OpenStream`, `.res` via `VIBE_Vfs_OpenFile`
  — keep the `textbin_german\%s.res` path format and the `german` locale (doc 03).
* **Do not approximate** the German grammatical-case machinery (Rule 8): it is real
  observable behaviour, driven by the `.res` metadata and the `$t/$b/$e/$u/$E` + `$s/$G`
  state interaction.
