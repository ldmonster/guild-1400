# GUI chunk 04 — tooltip dispatch & builder cores (1:1 hardening)

Files: `src/gui/tooltip.cpp`, `src/gui/tooltip_build.cpp` (+ headers, `tests/unit/gui_tooltip_dispatch_test.cpp`).
MCP: gilde.exe, imagebase 0x400000. DISASM is the reference of record where Hex-Rays collapses __usercall args.

## tooltip.cpp

### Tooltip_ClassifySubject — gilde.exe 0x4f7424 (classify core) — VERIFIED-1:1
Diffed against the classification region of `VIBE_Tooltip_DispatchByType` (0x4f74f6..0x4f781c).
- Person-record region test `v5 >= word_12CE910 && v5 < byte_1333110`, then `*v5 == -1` clears the ptr — matches source case (a). (Source adds a `personRecLo != 0` null-guard; the binary's bounds are fixed data addresses `word_12CE910`/`byte_1333110`, so the guard is a modeling no-op for the live data. Noted, not behavioral.)
- Table order after the person region: building (`dword_13CE298`, span 43264, byte code capped `>=72->0`) → upgrade (`dword_13CE294`, span 42408, `/589`) → person-code (`dword_13CE290`, span 548864, i16 capped `>=731->0`) → object (`dword_13CE27C`, span 47515, `/65`). Order, bases, spans, strides, caps all match the source constants (kBuildingSpan/kUpgradeSpan/kPersonCodeSpan/kObjectSpan/strides/72/731).
- Divisions taken as `(int)(uptr - base)/stride` (signed) — matches.
- sceneRef==0 fallback by `dword_75BF3C` (tooltipId): `[206,1010) -> obj = id-206`; `[1010,1082) -> bld = id+14`. Constants 206/1010/1082/+14 confirmed (0x4f77e4..0x4f781c).

### Tooltip_SelectBuilder — gilde.exe 0x4f7424 (final dispatch) — VERIFIED-1:1
Diffed against 0x4f7836..0x4f788a.
- Decision priority objectCode → buildingCode → contactPtr → personPtr → none matches binary `(_WORD)v0 || v1 || v3 || v2`.
- Object class byte at `*(_BYTE*)(dword_13CE27C + 65*(__int16)v0)`; {32,23,37} -> kObject else kUpgrade. Source uses `kObjectStride * static_cast<i16>(...)` — matches the `(__int16)v0` index width. (`(_WORD)v0` vs full-int test is harmless: object code domain ≤ 731.)
- contactPtr (v3) is sourced from `dword_631724` at the top of the real dispatch, outside the classify core; left as a default-0 input here (modeling boundary), and the contact branch is preserved for callers that set it.

## tooltip_build.cpp

### kBuildingColors[7] — gilde.exe 0x4f73f0 — VERIFIED-1:1
`get_bytes(0x4F73F0,28)` = `00 00 00 00 | 49 00 00 00 | 49 49 00 00 | 49 49 49 00 | 49 56 00 00 | 56 00 00 00 | 56 49 00 00`.
Little-endian dwords = {0x00000000, 0x00000049, 0x00004949, 0x00494949, 0x00005649, 0x00000056, 0x00004956}.
All 7 match the table byte-for-byte. The brief's FourCC/char constants are these palette bytes: 0x49='I', 0x56='V' (e.g. bytes `49 56` -> 0x00005649 at index 4; `56 49` -> 0x00004956 at index 6). Confirmed exact.

### StrToUpperLocal — VIBE_Util_StrToUpper @0x5e9f50 — VERIFIED-1:1
Binary: `v3 = *i - 97; if (v3 <= 0x19u) *i = v3 + 65;` (unsigned `(c-'a') <= 25`). Source `c>='a' && c<='z' -> c-32` is bit-identical (`(c-97)+65 == c-32`).

### Tooltip_BuildContactKey / Tooltip_ResolveContact — VIBE_Tooltip_BuildContact @0x4f83e8 — VERIFIED-1:1 (finder = BOUNDARY)
Disasm 0x4f83f3..0x4f843c:
- Copy loop is a NUL-terminated copy into a 140-byte stack buffer (`var_8C` at frame +0x100, return addr at +0x18c -> 140 bytes capacity). Source bounds the copy at `sizeof(upper)==140`. Matches.
- `VIBE_Util_StrToUpper(name)` then `VIBE_Crt_Sprintf_0(key, "_HILFE_%s+0", name)`. Upper-then-format order matches.
- `4f843a mov eax,esp; call VIBE_Text_FindTextArrayIndex` — finder receives the **formatted key buffer** (`var_18C`), not the raw name (Hex-Rays mis-shows v13). `!= -1` => hasText/textIndex. Source `Tooltip_ResolveContact` passes the built key to `findIndex`; the real `VIBE_Text_FindTextArrayIndex @0x44e0d8` is a mockable BOUNDARY (text DB, out of this chunk).

### Tooltip_BuildingLayout — VIBE_Tooltip_BuildBuilding @0x4f78e4 — FIXED
Disasm-verified (DISASM > Hex-Rays). Fixes:
1. **iconObjectId = code + 1010 was FABRICATED.** No `+1010` exists in the function. Icon is `4f7986 VIBE_Object_AddToWindow(dword_62D230, 0)` — a global handle. Removed the field (and its test assertion). Evidence: full disasm 0x4f78e4..0x4f7a0e shows no `+1010`/`+0x3F2`-as-id term feeding an object id; `dword_62D230` is the only icon source.
2. **code is signed 8-bit.** `4f78fb movsx esi, al`. nameTextId/descTextId now use `14*(signed char)code`. Added regression test (code=200 -> (i8)=-56). Before: `14*int(code)`.
3. **Added descTextId = 14*(i8)code + 1079.** `4f798b mov eax,[var_1C](=14*code); 4f798f add eax,437h` -> a second RichString arg the struct previously dropped.
4. Colour selector `*(u8*)(record+583)` (`4f793e mov al,[ebp+247h]`), indexed UNCONDITIONALLY (`4f7944 add eax,esp`); the original over-reads adjacent stack for sel>=7. Kept the `sel < 7` guard (clamps the original's C++-UB OOB case to 0; identical for every valid record). nameTextId base 0x436=1078, descTextId base 0x437=1079, extraField `*(record+579)` = `4f79f2 mov edx,[ebp+243h]` (unaligned, via memcpy) — all confirmed. salePrice = `VIBE_Building_ComputeSalePrice` (id 0x28) deferred BOUNDARY (caller-supplied).

### Tooltip_UpgradeApplies — VIBE_Tooltip_BuildUpgrade @0x4f8154 — FIXED
Disasm 0x4f8154..0x4f816f: `4f815c movsx edx, ax` then `shl eax,6; add edx,eax` -> index = `65*(i16)code`; `cmp byte ptr [edx+eax], 1Dh` (0x1D=29) returns -1 (skip) when equal. The arg arrives in `ax` (16-bit, sign-extended), not 32-bit. Fixed source index `kObjectStride * objectCode` -> `kObjectStride * (i16)objectCode`. Identical for the live code domain (1..731); now faithful to the `movsx ax` width.

## Counts
- VERIFIED-1:1: 5 (Tooltip_ClassifySubject, Tooltip_SelectBuilder, kBuildingColors, StrToUpperLocal, Tooltip_BuildContactKey/Tooltip_ResolveContact)
- FIXED: 2 (Tooltip_BuildingLayout, Tooltip_UpgradeApplies)
- BOUNDARY (deferred, out-of-tree data/runtime): VIBE_Text_FindTextArrayIndex (text DB), VIBE_Building_ComputeSalePrice (economy), the contact-ptr acquisition + form/render runtime in the full dispatcher.

## Tests
`gui_tooltip_dispatch_test` (added BuildingLayoutSignedCharCode, descTextId check; removed iconObjectId assertion) and `gui_tooltip_dispatch_e2e_test`: both PASS.
