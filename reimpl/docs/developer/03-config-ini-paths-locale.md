# 03 — Config, INI, Paths & Locale

This document describes how the original `gilde.exe` (Die Gilde / Europa 1400, 32-bit
x86, imagebase `0x400000`) reads and writes its configuration. Configuration lives in two
places: a flat `gilde.INI` file next to the executable (gameplay / audio / graphics
preferences, network, language, last-played city) and the Windows registry under
`HKEY_CURRENT_USER\Software\Ahead Entertainment\` (low-level Direct3D device selection).
At startup `VIBE_GameLogic_MainEntryAndShutdown` (`WinMain`) computes the executable's
directory, builds the absolute `gilde.INI` path, then pulls every preference key with the
kernel32 `GetPrivateProfileStringA` / `GetPrivateProfileIntA` APIs. On every shutdown path
`VIBE_Config_WriteGfxSettings @0x56af54` flushes the in-memory settings back out with
`WritePrivateProfileStringA`. Localization is driven by the `[General] Language` key, which
`VIBE_Locale_CopyLanguageString @0x5a33a0` resolves to a 0..4 language index that the text
subsystem uses to pick localized strings. Parsing is done with a handful of tiny hand-rolled
string helpers (`VIBE_Util_ParseInt`, `VIBE_Util_StrToUpper`, `VIBE_Util_StrChr`,
`VIBE_Util_StrCmp`, `VIBE_Util_StrCmpNoCase`) rather than the CRT.

> Platform boundary: the config layer is built entirely on kernel32
> `GetPrivateProfile*` / `WritePrivateProfileString` and advapi32 `Reg*`. The 1:1 reimpl
> reconstructs the INI parser faithfully and routes the actual file and registry I/O through
> the `IFileSystem` / platform shims; no behavior changes.

Cross-links: [01 — Entry point & WinMain](01-entrypoint-and-winmain.md) ·
[11 — Text rendering](11-text-rendering.md) ·
[05 — Window & platform input](05-window-platform-input.md).

---

## 1. Path computation — where `gilde.INI` lives

The very first thing `WinMain` (`VIBE_GameLogic_MainEntryAndShutdown @0x534bbc`) does is
locate itself on disk and derive the install directory and INI path. All three are stored as
process-global byte buffers.

```c
// VIBE_GameLogic_MainEntryAndShutdown @0x534bbc (excerpt @0x534bd7..0x534c45)
hInstance = hModule;
GetModuleFileNameA(hModule, byte_122F638, 0x104);   // full exe path, e.g. C:\Gilde\gilde.exe
strcpy(byte_122F73C, byte_122F638);                 // copy of full path -> install dir
*VIBE_Util_StrChr(byte_122F73C, '\\') = 0;          // truncate at LAST '\' -> install dir
                                                    //   (StrChr returns last match)
// Build gilde.INI path into the exe path buffer, overwriting the file name:
strcpy(VIBE_Util_StrChr(byte_122F638, '\\') + 1, "gilde.INI");   // aGildeIni @0x623644
```

| Global | Address | Meaning |
|---|---|---|
| `hInstance` | `0x122f52c` | saved `HINSTANCE` (the `hModule` WinMain arg). |
| `byte_122F638` | `0x122F638` | Full module path from `GetModuleFileNameA`, then rewritten to the absolute **`gilde.INI`** path (`...\gilde.INI`). This buffer is the `lpFileName` passed to **every** `GetPrivateProfile*` / `WritePrivateProfileString` call. |
| `byte_122F73C` | `0x122F73C` | The **install directory** (full path with the trailing `\gilde.exe` chopped off). Used as the base for `gfx` / `game` / `movie` paths. |

`VIBE_Util_StrChr @0x5d3ef0` is a `strrchr` (returns the **last** occurrence, not the first):

```c
// VIBE_Util_StrChr @0x5d3ef0  (__usercall: a1@eax = str, a2@dl = ch)
_BYTE *v2 = 0;
do { if (a2 == *a1) v2 = a1; } while (*a1++);
return v2;                       // last match, or NULL
```

Note the well-known quirk: because `aGildeIni` is the literal `"gilde.INI"` (uppercase
extension), and the source string buffers are copied byte-by-byte, the resulting filename is
`gilde.INI` exactly.

### 1.1 Resource path roots

Three path roots are read from `[General]` and concatenated onto the install dir elsewhere in
the engine. Each is read into its own global with a default equal to the global's initial
value:

```c
GetPrivateProfileStringA("General","GfxPath",  "\\project\\gfx\\",  aProjectGfx,  0x104, byte_122F638);
GetPrivateProfileStringA("General","GamePath", "\\project\\game\\", aProjectGame, 0x104, byte_122F638);
GetPrivateProfileStringA("General","MoviePath","\\project\\movie\\",aProjectMovie,0x104, byte_122F638);
```

| Key (`[General]`) | Destination global | Address | Default |
|---|---|---|---|
| `GfxPath` | `aProjectGfx` | `0x63c908` | `\project\gfx\` |
| `GamePath` | `aProjectGame` | `0x63ca0c` | `\project\game\` |
| `MoviePath` | `aProjectMovie` | `0x63cb10` | `\project\movie\` |

The network `[Network] Server` key is also resolved into a directory-style global the same way
(it points at the server DLL):

```c
GetPrivateProfileStringA("Network","Server","Server\\Server.dll", byte_122F284, 0x104, byte_122F638);
strcpy(byte_122F388, byte_122F284);
*VIBE_Util_StrChr(byte_122F388, '\\') = 0;   // byte_122F388 = directory part only
```

---

## 2. The complete `gilde.INI` read table (startup)

Every `GetPrivateProfile*` call made by `WinMain` at startup, in source order. `Int` =
`GetPrivateProfileIntA` (returns DWORD/byte); `Str` = `GetPrivateProfileStringA` (copies into
the destination buffer; size column is the `nSize` cap). All use `byte_122F638` (the
`gilde.INI` path) as the file.

| # | Section | Key | Default | Kind / size | Destination | Addr |
|---|---|---|---|---|---|---|
| 1 | `Network` | `Server` | `Server\Server.dll` | Str 0x104 | `byte_122F284` (+ dir → `byte_122F388`) | `0x534c70` |
| 2 | `Gfx` | `TextureDivider` | `0` | Int | `dword_63C790` | `0x534cc5` |
| 3 | `Sound` | `msx` | `0` | Int | `dword_63C8F8` | `0x534cf1` |
| 4 | `Sound` | `ambient` | `0` | Int | `dword_63C8FC` | `0x534d0d` |
| 5 | `Sound` | `sfx` | `0` | Int | `dword_63C900` | `0x534d2a` |
| 6 | `Sound` | `weather` | `1` | Int | `dword_63C904` | `0x534d40` |
| 7 | `General` | `Bildmodus` | `FULLSCREEN` | Str 0x100 | `ReturnedString` (local) → `v120` mode | `0x534d59` |
| 8 | `General` | `GfxPath` | `\project\gfx\` | Str 0x104 | `aProjectGfx` `0x63c908` | `0x534de4` |
| 9 | `General` | `GamePath` | `\project\game\` | Str 0x104 | `aProjectGame` `0x63ca0c` | `0x534e09` |
| 10 | `General` | `MoviePath` | `\project\movie\` | Str 0x104 | `aProjectMovie` `0x63cb10` | `0x534e2e` |
| 11 | `General` | `Language` | `german` | Str 0x40 | `aGerman` `0x62eae4` → locale index | `0x534e5a` |
| 12 | `General` | `show_intro` | `0` | Int | `dword_63C8F0` | `0x534eab` |
| 13 | `Gfx` | `cur_res` | `0` | Int (byte) | `byte_63D724` → res tables | `0x534eb7` |
| 14 | `General` | `Stadt` | `Augsburg` | Str 0x40 | `::ReturnedString` `0x122ee50` | `0x534f14` |
| 15 | `Network` | `Host` | `128.0.0.1` | Str 0x80 | `byte_122EE90` | `0x534f39` |
| 16 | `Network` | `Port` | `7531` | Int | `dword_122F490` | `0x534f60` |

String literals & addresses: `aNetwork`=`"Network"` `0x622bf8`, `aServer`=`"Server"`
`0x623664`, `aServerServerDl`=`"Server\Server.dll"` `0x623650`, `aGfx_0`=`"Gfx"` `0x62367c`,
`aTexturedivider`=`"TextureDivider"` `0x62366c`, `aSound_0`=`"Sound"` `0x623684`,
`aMsx`=`"msx"` `0x623680`, `aAmbient`=`"ambient"` `0x62368c`, `aSfx`=`"sfx"` `0x623694`,
`aWeather`=`"weather"` `0x623698`, `aGeneral`=`"General"` `0x6236b8`,
`aBildmodus`=`"Bildmodus"` `0x6236ac`, `aFullscreen`=`"FULLSCREEN"` `0x6236a0`,
`aLanguage`=`"Language"` `0x623728`, `aGerman_0`=`"german"` `0x623720`,
`aShowIntro`=`"show_intro"` `0x62373c`, `aCurRes_0`=`"cur_res"` `0x623748`,
`aStadt_0`=`"Stadt"` `0x623768`, `aAugsburg_0`=`"Augsburg"` `0x62375c`, `aHost`=`"Host"`
`0x622bf0`, `a128001`=`"128.0.0.1"` `0x623770`, `aPort`=`"Port"` `0x62377c`.

### 2.1 `[General] Bildmodus` → window/display mode (`v120`)

The render mode integer (`v120`) is decided from the `Bildmodus` string plus two run-time
predicate calls (`loc_5CB930`, a command-line / state probe):

```c
GetPrivateProfileStringA("General","Bildmodus","FULLSCREEN", &ReturnedString, 0x100, INI);
VIBE_Util_StrToUpper(&ReturnedString);
if ( VIBE_Util_StrCmp(<lit>, &ReturnedString) )   // not the matched literal
    v120 = 3;                                      // windowed
else
    v120 = 1;                                      // fullscreen (string == "FULLSCREEN")
if ( loc_5CB930() ) v120 = 1;                      // forced fullscreen
if ( loc_5CB930() ) v120 = 3;                      // forced windowed
```

`v120` is later handed to `VIBE_Window_CreateMainWindow(hModule, v120) @0x52895c` and
`VIBE_Render_InitDisplayAndPaths(v120, …) @0x527fa4`. See
[05 — Window & platform input](05-window-platform-input.md).

### 2.2 Command-line override of city / class / host / port

After the INI reads, four `loc_5CB930()` probes return pointers into the command line and, when
present, override the INI-loaded values by extracting the text between double-quotes following
a prefix token. The prefixes are literals `STADT="` (`0x623784`), `BERUF="` (`0x62378c`),
`IP="` (`0x623794`), `PORT="` (`0x62379c`). Extraction copies characters from just after the
`"` up to the next `"` into the destination:

- `STADT="…"` → `::ReturnedString` `0x122ee50` (overrides `[General] Stadt`).
- `BERUF="…"` → `byte_63C7DC` (character class / profession name).
- `IP="…"` → `byte_122EE90` (overrides `[Network] Host`) and sets `v16 = 1` (network-game flag).
- `PORT="…"` → temp buffer `v116`, then `dword_122F490 = VIBE_Util_ParseInt(v116)` — the only
  use of `VIBE_Util_ParseInt` in `WinMain`, parsing the command-line port (the INI `Port` came
  in already as an int via `GetPrivateProfileIntA`).

---

## 3. Resolution tables — `cur_res` → width/height

`[Gfx] cur_res` selects a row in a pair of interleaved DWORD tables. The selected width/height
land in `dword_63D728` / `dword_63D72C`:

```c
// @0x534eb7..0x534eda
byte_63D724  = GetPrivateProfileIntA("Gfx","cur_res",0, INI);       // index, stored as a byte
dword_63D728 = dword_63D70C[2 * (u8)byte_63D724];                   // width
dword_63D72C = dword_63D710[2 * (u8)byte_63D724];                   // height
```

`dword_63D710 == dword_63D70C + 4`, i.e. the two "tables" are one array of DWORDs offset by one
element, so indexing `dword_63D70C[2*i]` / `dword_63D710[2*i]` reads adjacent (width, height)
pairs out of a single flat array.

Raw bytes at `dword_63D70C @0x63D70C` (little-endian DWORDs):

```
0x63D70C: 20 03 00 00  58 02 00 00  00 04 00 00  00 03 00 00   ; 0x320 0x258 0x400 0x300
0x63D71C: 80 04 00 00  60 03 00 00  00 00 00 00  20 03 00 00   ; 0x480 0x360 0x000 0x320
0x63D72C: 58 02 00 00  ...                                     ; 0x258 (= dword_63D72C, the live H)
```

Decoded `cur_res` index → resolution:

| `cur_res` | Width (`dword_63D728`) | Height (`dword_63D72C`) |
|---|---|---|
| 0 | 800 (`0x320`) | 600 (`0x258`) |
| 1 | 1024 (`0x400`) | 768 (`0x300`) |
| 2 | 1152 (`0x480`) | 864 (`0x360`) |
| 3 | 0 | 800 |

Index 3 yields a degenerate `0 × 800` pair (the array has run out of real entries — the bytes
beyond row 2 are zero-fill / unrelated data, e.g. `0x0000`, then `0x320`/`0x258` again).
Practically only indices 0–2 are valid resolutions; the shipped game UI exposes 800×600,
1024×768 and 1152×864.

---

## 4. Localization — `[General] Language`

```c
// @0x534e5a
GetPrivateProfileStringA("General","Language","german", aGerman /*0x62eae4*/, 0x40, INI);
strcpy(byte at 0x623734 "deutsch" -> ... );    // (a `deutsch` alias copy, @0x534e62 loop)
VIBE_Locale_CopyLanguageString(aGerman, …);    // @0x534e7d
```

The default language is **`german`**; the value is copied into `aGerman` `0x62eae4`. (Doc 01
covers the surrounding flow; here is the resolver.)

### 4.1 `VIBE_Locale_CopyLanguageString @0x5a33a0`

```c
// VIBE_Locale_CopyLanguageString  (__usercall: a1@eax = lang string)
char tmp[0x40];
strcpy(tmp, a1);                 // copy caller's "german"/"english"/...
VIBE_Util_StrToUpper(tmp);       // -> "GERMAN" etc.
word_649D48 = 0xFFFF;            // 0x649d48: selected language index, default "none"
for (index = 0; index < 5; index++)
    if (!VIBE_Util_StrCmp(tmp, &LANG_TABLE[index*0x40]))   // table @0x5a3260, 0x40-byte stride
        { word_649D48 = index; return index; }
return 0xFFFF;                   // unrecognized
```

The comparison table is five 64-byte uppercase ASCII slots starting at `aGerman_1 @0x5a3260`.
Raw bytes confirm the entries and order:

| Index | String (`@0x5a3260 + index*0x40`) |
|---|---|
| 0 | `GERMAN` |
| 1 | `ENGLISH` |
| 2 | `FRENCH` |
| 3 | `ITALIAN` |
| 4 | `SPANISH` |

The resolved index is stored in `word_649D48 @0x649d48`. `VIBE_Text_GetCurrentIconWord @0x5a3418`
reads it back, and the text subsystem keys off it to choose localized strings / asset variants —
so `Language=german` (index 0) drives all localized text. See
[11 — Text rendering](11-text-rendering.md). An unrecognized language leaves `word_649D48 =
0xFFFF`.

---

## 5. Persisting settings on shutdown — `VIBE_Config_WriteGfxSettings @0x56af54`

This function is invoked on **every** shutdown path in `WinMain` (success and all error exits).
It serializes the in-memory settings block (a struct at `0x1233510`, plus `byte_63D724` and the
float color-correction values) back into `gilde.INI` via `WritePrivateProfileStringA`. Each
numeric value is first formatted into the local `String[32]` buffer by
`VIBE_AnimationState_Update(value, String, 10) @0x5d92ec` (an `itoa`-base-10 helper despite the
name); color values are scaled by `flt_6251F0 @0x6251f0` before formatting.

```c
// pattern, repeated per key:
VIBE_AnimationState_Update((u8)byte_1233515, String, 10);
WritePrivateProfileStringA("Gfx","texture_scale", String, byte_122F638);
```

Complete write table (section, key, source global, value addr):

### `[Gfx]`
| Key | Source | Addr |
|---|---|---|
| `texture_scale` | `byte_1233515` | `0x1233515` |
| `details` | `byte_1233514` | `0x1233514` |
| `lod_handling` | `byte_1233516` | `0x1233516` |
| `shadow_detail` | `byte_1233517` | `0x1233517` |
| `floor_mipmapping` | `byte_1233518` | `0x1233518` |
| `gfx_set` | `byte_1233510` | `0x1233510` |
| `camera_limits` | `byte_1233519` | `0x1233519` |
| `floor_lod` | `byte_123351A` | `0x123351A` |
| `character_detail` | `byte_123351B` | `0x123351B` |
| `fog_plane` | `byte_123351C` | `0x123351C` |
| `cur_res` | `byte_63D724` | `0x63D724` |
| `brightness_r` | `flt_1233520 * flt_6251F0` | `0x1233520` |
| `brightness_g` | `flt_1233524 * flt_6251F0` | `0x1233524` |
| `brightness_b` | `flt_1233528 * flt_6251F0` | `0x1233528` |
| `brightness_a` | `flt_123352C * flt_6251F0` | `0x123352C` |
| `contrast_r` | `flt_1233530 * flt_6251F0` | `0x1233530` |
| `contrast_g` | `flt_1233534 * flt_6251F0` | `0x1233534` |
| `contrast_b` | `flt_1233538 * flt_6251F0` | `0x1233538` |
| `contrast_a` | `flt_123353C * flt_6251F0` | `0x123353C` |
| `gamma_r` | `flt_1233540 * flt_6251F0` | `0x1233540` |
| `gamma_g` | `flt_1233544 * flt_6251F0` | `0x1233544` |
| `gamma_b` | `flt_1233548 * flt_6251F0` | `0x1233548` |
| `gamma_a` | `flt_123354C * flt_6251F0` | `0x123354C` |

### `[Sound]`
| Key | Source | Addr |
|---|---|---|
| `master_vol` | `byte_1233550` | `0x1233550` |
| `sfx_vol` | `byte_1233551` | `0x1233551` |
| `msx_vol` | `byte_1233552` | `0x1233552` |
| `speech_vol` | `byte_1233553` | `0x1233553` |
| `msx_freq` | `byte_1233554` | `0x1233554` |

### `[Game]`
| Key | Source | Addr |
|---|---|---|
| `speed` | `dword_1233558` | `0x1233558` |
| `mouse_speed` | `dword_1233560` | `0x1233560` |
| `scroll_speed` | `dword_1233564` | `0x1233564` |
| `camera_speed` | `byte_123355C` | `0x123355C` |
| `invert_mouse` | `byte_1233568` | `0x1233568` |
| `nachtwaechter` | `byte_1233569` | `0x1233569` |
| `stadt` | `byte_123356C` (written **as a string**, not formatted) | `0x123356C` |
| `historie` | `dword_12335AC` | `0x12335AC` |
| `mission` | `dword_12335B0` | `0x12335B0` |
| `net_mission` | `dword_12335B4` | `0x12335B4` |
| `show_cursor_txt` | `byte_123356A` | `0x123356A` |
| `show_geb_info` | `byte_123356B` | `0x123356B` |
| `panel_mode` | `byte_12335B8` | `0x12335B8` |
| `help_events` | `byte_12335B9` | `0x12335B9` |
| `difficulty` | `byte_12335BA` | `0x12335BA` |
| `hints` | `byte_12335BB` | `0x12335BB` |
| `panel_help` | `byte_12335BC` (the function's `return` value) | `0x12335BC` |

Notes:
- `[Game] stadt` is written directly as the string at `byte_123356C` (the active city name), not
  via the int formatter.
- Section/key string literals (selection): `aGfx`=`"Gfx"` `0x624fbc`, `aSound`=`"Sound"`
  `0x6250e4`, `aGame`=`"Game"` `0x62511c`. The save table uses a separate `Gfx` literal
  (`0x624fbc`) from the read table's (`0x62367c`); both are the string `"Gfx"`.
- The write table is **not** symmetric with the read table: `WinMain` reads `TextureDivider`,
  `msx`/`ambient`/`sfx`/`weather`, `Bildmodus`, the `*Path` keys, `Language`, `show_intro`,
  `cur_res`, `Stadt`, `Host`, `Port`; `WriteGfxSettings` writes the richer in-game settings
  panel block above. Keys read at startup but never written here (e.g. `Bildmodus`, `Language`,
  `GfxPath`, `Host`, `Port`, sound enable flags) are persisted by other UI paths or left as
  authored.

### 5.1 `VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc`

Applies the loaded `[Game]` speed/scroll values to the live input/camera state (no I/O):

```c
// VIBE_Config_ApplyCameraAndScrollSettings @0x56c0cc
dword_62D0E4 = 0;
if (word_63C740 & 4)   // game-mode flag set
    *(float*)&dword_62D07C = ((double)dword_631284 * dbl_625214 + dbl_62521C) * dbl_625224;
else
    *(float*)&dword_62D07C = (double)dword_1233558 * flt_62520C + dbl_625214;   // from [Game] speed
VIBE_Input_SetWheelBase(dword_1233560 - 64);   // [Game] mouse_speed, biased by 64
result = *(u8*)(v1 + 4);
dword_6316C8 = 100 - result;                   // scroll edge margin
return result;
```

`dword_1233558` is the `[Game] speed` value and `dword_1233560` is `[Game] mouse_speed`
(`SetWheelBase` is given `mouse_speed - 64`, so 64 is the neutral midpoint). Constants
`dbl_625214/62521C/625224/flt_62520C` are the scale/bias factors.

---

## 6. Config string helpers

These are the hand-rolled primitives the config layer uses instead of the CRT. All are
`__usercall` with register arguments (noted), which the reimpl preserves.

### `VIBE_Util_ParseInt @0x5dc070` (`__usercall`, `a1@eax`)

A signed-decimal `atoi`. Uses the character-class table `byte_64A208 @0x64a208` indexed by
`(char+1)`: bit `0x2` = whitespace, bit `0x20` = decimal digit. Skips leading whitespace, takes
an optional `+`/`-`, then accumulates `value = value*10 + (digit-'0')` while the digit bit is set.
Negates if the sign was `-`. No overflow guard (wraps like 32-bit int).

```c
while (byte_64A208[(u8)(*a1 + 1)] & 2) ++a1;          // skip whitespace
char sign = *a1;
if (*a1 == '+' || sign == '-') ++a1;
int v = 0;
while (byte_64A208[(u8)(*a1 + 1)] & 0x20)             // while digit
    v = (u8)*a1++ + 10*v - 48;
return (sign == '-') ? -v : v;
```

### `VIBE_Util_StrToUpper @0x5e9f50` (`__usercall`, `a1@eax`)

In-place ASCII upper-case (`a..z` → `A..Z`); returns the same pointer. Implemented as
`if ((u8)(c-'a') <= 25) c = (c-'a')+'A';`. Used to canonicalize `Bildmodus` and `Language`
before comparison.

### `VIBE_Util_StrChr @0x5d3ef0` (`__usercall`, `a1@eax`, `a2@dl`)

`strrchr` — returns the **last** occurrence of the character (or NULL). Used to split the exe
path on `\`.

### `VIBE_Util_StrCmp @0x5d3f10` (`__usercall`, `a1@edx`, `a2@eax`)

A word-at-a-time `strcmp` with the classic `(~v & (v - 0x01010101) & 0x80808080)` zero-byte
test; returns 0 on equality, ±1 otherwise. Used for the `Bildmodus`/`Language` exact matches.

### `VIBE_Util_StrCmpNoCase @0x5cb8f0` (`__usercall`, `a1@eax`, `a2@edx`)

Case-insensitive byte compare (lower-cases `A..Z` on both sides before comparing); returns the
signed byte difference at the first mismatch / terminator. In `WinMain` it matches the
command-line `BERUF=` profession name against the class-name table `dword_8C3B48` (76 entries,
4-byte stride, loop bound `304`) to derive the starting character class index `v121`.

---

## 7. Registry usage — Direct3D device configuration

Separate from the INI, the renderer persists its **device/driver** selection in the Windows
registry under `HKEY_CURRENT_USER\Software\Ahead Entertainment\<subkey>`. The base path prefix
is the literal `aSoftwareAheadE = "Software\Ahead Entertainment\"` (`0x610a14`); the caller
supplies the subkey, which is appended via `sprintf("%s%s", prefix, subkey)`.

### 7.1 Registry helpers (advapi32 wrappers)

| Function | Addr | Win32 import | Notes |
|---|---|---|---|
| `VIBE_Registry_OpenKey` | `0x40c420` | `RegOpenKeyExA` / `RegCreateKeyExA` | mode 1 = open read (`KEY_READ` `0x20019`); mode 2 = create r/w (`0x20006`); returns `HKEY` or `(HKEY)-1`. |
| `VIBE_Registry_CloseKey` | `0x40c4c8` | `RegCloseKey` | — |
| `VIBE_Registry_QueryDwordValue` | `0x40c4d8` | `RegQueryValueExA` | read 4-byte DWORD. |
| `VIBE_Registry_QueryDwordOut` | `0x40c510` | `RegQueryValueExA` | read DWORD into `*out`, returns 1 on success. |
| `VIBE_Registry_QueryStringValue` | `0x40c550` | `RegQueryValueExA` | read up to 128-byte string. |
| `VIBE_Registry_QueryFloatValue` | `0x40c5c0` | (via string query) | string → float. |
| `VIBE_Registry_QueryFloatOut` | `0x40c608` | (via string query) | float into `*out`. |
| `VIBE_Registry_SetDwordValue` | `0x40c670` | `RegSetValueExA` | `REG_DWORD`. |
| `VIBE_Registry_SetStringValue` | `0x40c690` | `RegSetValueExA` | `REG_SZ`. |
| `VIBE_Registry_SetFloatValue` | `0x40c6b8` | `RegSetValueExA` | formats float with `"%f"`, writes `REG_SZ`. |

Imports: `RegOpenKeyExA @0x60e530`, `RegQueryValueExA @0x60e534`, `RegSetValueExA @0x60e538`,
`RegCreateKeyExA @0x60e52c`, `RegCloseKey @0x60e528`.

```c
// VIBE_Registry_OpenKey @0x40c420  (__usercall: a1@eax = subkey, a2/v2 = mode)
sprintf(SubKey, "%s%s", "Software\\Ahead Entertainment\\", a1);
if (mode == 1) { RegOpenKeyExA(HKEY_CURRENT_USER, SubKey, 0, KEY_READ /*0x20019*/, &h); return h; }
if (mode == 2) { RegCreateKeyExA(HKEY_CURRENT_USER, SubKey, 0,0,0, 0x20006, 0, &h, &disp); return h; }
return (HKEY)-1;
```

### 7.2 What is stored — the D3D config block

`VIBE_Render_SaveD3dRegistryConfig @0x5d3938` (and the parallel
`VIBE_Render_SaveD3DSettingsToRegistry @0x5e0890`) write the renderer's chosen mode and limits;
`VIBE_Render_LoadD3dRegistryConfig @0x5d3be8` / `VIBE_Render_LoadD3DSettingsFromRegistry
@0x5e0abc` read them back. The value names (all under the `Ahead Entertainment` key):

| Value name | Addr | Type |
|---|---|---|
| `d3_screen_res_x` | `0x629178` | DWORD |
| `d3_screen_res_y` | `0x629188` | DWORD |
| `d3_screen_res_depth` | `0x629198` | DWORD |
| `d3_use_hardware` | `0x6291ac` | DWORD |
| `d3d_use_triple` | `0x6291bc` | DWORD |
| `d3_max_polys` | `0x6291cc` | DWORD |
| `d3_max_textures` | `0x6291dc` | DWORD |
| `d3_max_mipmaps` | `0x6291ec` | DWORD |
| `d3s_max_palettes` | `0x6291fc` | DWORD |
| `d3d_floortex_divider` | `0x629210` | DWORD |
| `d3d_z_buffer_depth` | `0x629228` | DWORD |
| `d3d_static_texture_fourcc` | `0x62923c` | DWORD |
| `d3_mode` | `0x6292a4` | string (`DRAWDIB` / `DIRECTWINDOW` / `DIRECTWINDOWSOFT` / `FULLSCREEN` / `FULLSCREENSOFT`) |
| `d3io_LODMode` | `0x6292dc` | string (`d3io_LOD_LOW` / `d3io_LOD_HIGH` / `d3io_LOD_SWITCH`) |
| (7 float values) | — | `REG_SZ` via `"%f"` (color correction / brightness/contrast/gamma) |
| `d3_registry_version` | `0x629374` | DWORD, written as `3` |

`d3_mode` string is selected by a switch on the chosen device mode (`0=DRAWDIB`,
`1=DIRECTWINDOW`, `2=DIRECTWINDOWSOFT`, `3=FULLSCREEN`, `4=FULLSCREENSOFT`); `d3io_LODMode` from
`byte_64A098 @0x64a098` (`1=LOD_SWITCH`, `2=LOD_LOW`, `4=LOD_HIGH`). Callers of the registry
layer also include the DDraw device-selection dialog `DialogFunc @0x432b28` and
`VIBE_Render_SelectDDrawDevice @0x43343c` / `VIBE_Render_EnumDevices @0x5dd1c4`.

> Reimpl note: these `Reg*` calls are the renderer's persisted GPU/device choice. Under the
> Vulkan + SDL backend (rules 3–4) the display mode is owned by SDL/Vulkan rather than DirectDraw
> device enumeration, so the registry block is routed through the platform shim and the
> DirectDraw-specific value semantics are reinterpreted by the new device layer. The INI side
> (sections 1–6) is reconstructed 1:1.
