# Harden wave — render_07_text (text_raster.cpp, text_cp1251.cpp)

MCP: IDA Pro, module gilde.exe, imagebase 0x400000 (live).
Scope (edit-only): src/render/text_raster.cpp + .h, src/render/text_cp1251.cpp + .h,
tests/unit/text_raster_test.cpp, tests/unit/text_cp1251_test.cpp.

## Provenance inventory

| File | Function | Addr | Status |
|---|---|---|---|
| text_raster.cpp | kBuiltinFontBitmap[637] (data) | 0x62D59C | VERIFIED-1:1 |
| text_raster.cpp | DrawGlyph | 0x434D0C | FIXED (return value) |
| text_raster.cpp | DrawText | 0x434E18 | VERIFIED-1:1 / BOUNDARY |
| text_cp1251.cpp | (all) | — none — | N/A — host-authored, not a translation |

## kBuiltinFontBitmap @0x62D59C — VERIFIED-1:1
get_bytes(0x62D59C, 637) compared byte-for-byte to the 637-byte literal in
text_raster.cpp. Every byte matches (91 glyphs * 7 rows). No change.

## DrawGlyph @0x434D0C — FIXED

Control flow, clip, address math, and both raster loops verified against disasm:
- `7 * glyphMap[ch]` = `lea eax,[ecx*8]; sub eax,ecx` ✓
- right clip: `lea eax,[edx+5]; cmp eax,ecx; ja` → unsigned `(x+5) > dibStride` returns x+5 ✓
- bottom clip: `(y+7) > cy(screenHeight)` returns y+7 ✓
- addr: `dword_7626FC*y + dword_7626F4*x + dword_7626F0`
  = pitchBytes*y + pitchExtra*x + targetBase ✓
- depth gates: `cmp eax,10h; jb`(return depth) / `jbe`(16bpp) / `cmp eax,20h; jnz`(else 32bpp) ✓
- 16bpp stores `si` (color&0xFFFF) per set bit, mask 16>>col, row advance += pitchBytes ✓
- 32bpp stores `esi` (full color) per set bit, dst+=4, row advance += pitchBytes ✓

### Divergence found and fixed — RETURN VALUE (eax)
The reconstruction returned `depth` on every draw path. The disassembly proves
otherwise (the original never restores eax to depth inside the loops):

- 16bpp draw path (loc_434DC3): `xor eax,eax` per row, `inc eax` x5, `cmp eax,5`;
  eax is NOT reloaded on row exit → on loop completion **eax == 5**.
  Evidence: 0x434dcd `xor eax,eax`; 0x434de2 `inc eax`; 0x434de6 `cmp eax,5`;
  0x434df9 `jz loc_434DB9` with eax=5 → 0x434dbf `retn`.
- 32bpp draw path (loc_434D83): tail sets `mov eax,[esp+var_18]` where var_18 =
  `lea edx,[ebp+7]` = (glyph row block + 7) → on loop completion **eax == glyph+7**
  (a data-segment pointer, NOT depth).
  Evidence: 0x434d7f save of `[ebp+7]`; 0x434da8 `mov eax,[esp+var_18]`;
  0x434db7 `jnz loc_434D83`, fallthrough to retn with eax = that pointer.
- depth<16 / depth not in {16,32}: returns depth (unchanged — already correct).
- clip fail: returns x+5 / y+7 (unchanged — already correct).

Before: `return depth;` after both loops.
After:  16bpp returns `5` (column counter on exit); 32bpp returns
        `(u32)(uintptr)(glyph + 7)`; non-draw paths still return depth/clip extent.

Header doc (text_raster.h) updated to describe the exact eax contract (no longer
the inaccurate "returns depth").

This return value reaches DrawText as `LOBYTE(eax)`; it is the value DrawText
returns when the unlock tail is skipped (dword_7626F0 == 0). Faithful match now.

### Golden tests corrected to the binary (cite: 0x434D0C disasm)
tests/unit/text_raster_test.cpp:
- GlyphA_16bpp_GoldenGrid: `CHECK_EQ(ret, 16u)` → `CHECK_EQ(ret, 5u)`.
- GlyphA_32bpp_GoldenGrid: `CHECK_EQ(ret, 32u)` → compute `glyph+7` and check ret
  equals `(u32)(uintptr)(glyph+7)`.
- GlyphExactlyFillsFramebuffer: `CHECK_EQ(ret, 16u)` → `CHECK_EQ(ret, 5u)`.
- UnsupportedDepthNoOp (depth 24): `CHECK_EQ(ret, 24u)` UNCHANGED — correct
  (non-draw path returns depth).
- Clip tests (ret==7, ret==8): UNCHANGED — correct.

## DrawText @0x434E18 — VERIFIED-1:1 (+ BOUNDARY)

- AcquireBackBuffer gate, return 0 on failure ✓ (0x4345D4, surface_present.cpp).
- Inlined color pack `(g>>76271C<<76271E)|(r>>76271B<<76271A)|(b>>762719<<76271D)`
  is exactly PackColor(fmt,r,g,b) @0x434F30 (colorformat.cpp): gPrec/gPos/rPrec/
  rPos/bPrec/bPos map 1:1 to those six shift bytes. Reused, not redefined ✓.
- Glyph loop: check `*text` first; per char `LOBYTE=*p; if(*p!=32) LOBYTE=DrawGlyph;
  next=*++p; x+=6; while(next)` ✓.
- Unlock tail: `if(dword_7626F0) switch(byte_762721){0/2/4→F0=0; 1/3→call[vtbl+0x80],
  F0=0}` — delegated to UnlockBackBuffer @0x434680 (surface_present.cpp), guarded by
  `if(g.targetBase)`. dword_7626F0==targetBase ✓. Behavior-identical refactor.
- BOUNDARY: DDraw lock/unlock + the case-1/3 vtable call route through the present
  shim (Acquire/UnlockBackBuffer in surface_present.cpp) — only the GPU/DDraw call
  is hooked; the raster MATH and control flow are 1:1.
- Callers of 0x434E18: only VIBE_Render_DrawDebugOverlay @0x5B6128 (Latin overlay).
  DrawGlyph @0x434D0C is called only by DrawText.

## text_cp1251.cpp / .h — N/A (host-authored, no binary provenance)

This module carries NO `gilde.exe 0xADDR` provenance — confirmed by grep (zero
matches) and by its header, which states it is NOT a 1:1 reconstruction. The
reversed binary is a Latin build with **no Cyrillic codepage table** to verify:
- DrawText (0x434E18) is only called from the Latin debug overlay; there is no
  CP1251 translation table anywhere in gilde.exe.
- The kUpper[32]/kYo Cyrillic 5x7 glyph art and the small-caps 0xE0..0xFF→0xC0..
  0xDF / 0xA8,0xB8→Ё mapping are authored host content so REAL localized CP1251
  data (e.g. _STADTAUSWAHL_*_BESCHR) can render in the engine font's style.
- The only binary-derived data it touches is kBuiltinFontBitmap (ASCII fallback,
  ch<0x80), already VERIFIED byte-for-byte above.
The mission's premise ("text_cp1251 likely has a codepage translation table —
verify every byte") does not apply: there is no such table in the binary.

## Counts
- VERIFIED-1:1: 3 (kBuiltinFontBitmap, DrawText, DrawGlyph control flow/loops/clip/math)
- FIXED: 1 (DrawGlyph return value: 16bpp→5, 32bpp→glyph+7) + 3 golden CHECK_EQs
- BOUNDARY: 1 (DrawText DDraw Acquire/Unlock via present shim)
- N/A host-authored: text_cp1251.cpp (all functions; no provenance)

## Tests (green)
build targets: text_raster_test, text_cp1251_test, text_raster_itest,
text_raster_e2e_test — all built and pass.
- text_cp1251_test: Passed
- text_raster_test: Passed
- text_raster_itest: Passed
- text_raster_e2e_test: Passed
