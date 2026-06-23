# Hardening sweep — render cluster 7 (sprite_scale.cpp, sun_state.cpp)

MCP-driven line-for-line diff of every `gilde.exe 0xADDR` function in:
- `src/render/sprite_scale.cpp`
- `src/render/sun_state.cpp`

Test targets (all green): `render_sprite_scale_test`, `render_billboard_project_test`,
`sun_daycycle_test`, plus `render_sprite_scale_itest`, `render_sprite_scale_e2e_test`.

---

## sun_state.cpp

| addr | function | status |
|------|----------|--------|
| 0x42DC40 | SetSunDirection | VERIFIED-1:1 |
| 0x42DC5C | EnableSun | VERIFIED-1:1 |
| 0x5C8964 | ResetGlobalState | VERIFIED-1:1 |

- SetSunDirection: `dword_62D568=-1; dword_62D56C=dword_62EB38(tick); dword_62D570=a2; return tick`. a1=ecx scratch unused. Matches.
- EnableSun: `state=1; stamp=tick; param=0; return tick`. Disasm confirms `xor ecx,ecx` -> param=0. Matches.
- ResetGlobalState: zeroes 64A05C, 64A060, 64A058, 64A064, 64A054 in exactly that order (disasm 0x5c8967..0x5c897f). Matches.

---

## sprite_scale.cpp

| addr | function | status |
|------|----------|--------|
| 0x5D72F0 | BlitScaled16 | VERIFIED-1:1 |
| 0x5D6A08 | BlitRleScaled | VERIFIED-1:1 |
| 0x5D6D74 | BlitRleLightTable | **FIXED** (X-clip) |
| 0x5D86C4 | ShowFromBankScaled | **FIXED** (byte_140694B gate) + BOUNDARY |
| 0x5D861C | ShowFromBank | **FIXED** (byte_140694B gate) |
| 0x559D60 | EncodeSpriteDrawFlags | VERIFIED-1:1 |
| 0x5AC970 | ProjectBillboardVertices (3 arms) | VERIFIED-1:1 |
| 0x5ACAB0 | BillboardEffectTint(Color/Apply) | VERIFIED-1:1 |
| 0x5ACAE0 | BillboardQuadVisibilityPass | **FIXED** (80-bit winding) |

### FIXED — 0x5D6D74 BlitRleLightTable: missing X-clip suppression
Evidence: the light-table prologue (disasm 0x5d6dc6) reads only height @+0x0A and runs
**only** the Y-clip — `(u16)height + a2 < clipY0` (return 0) and `a2 + height/scale >
clipY1`. It performs **no** width read (+0x06) and **no** X-clip. By contrast
BlitRleScaled (0x5d6a5f..0x5d6a87) reads width and applies
`a1 + width/scale > clipX1 || a1 + width < clipX0`.

The shared `RleScaledImpl` applied the X-clip to *both* paths, so the light-table form
would wrongly early-return 0 for shapes that the binary draws.

Fix: gate the width read + X-clip on `!lightTable` inside `RleScaledImpl`.

### FIXED — 0x5D86C4 / 0x5D861C: missing byte_140694B gate
Evidence: both functions wrap the stride-install + depth-dispatch block in
`if (!byte_140694B)` (disasm `cmp byte_140694B,0 / jnz` at 0x5d8704 and 0x5d864c).
byte_140694B is a real engine global (default 0; written by InitColorMasks @0x5d4b25,
FreeLightTable @0x5d4bcd, FrameData_Process @0x5d7821 — high-colour mode). When set, the
binary skips the whole block: ShowFromBank returns 1 without touching the dest stride or
blitting; ShowFromBankScaled falls through to the clip-extent recompute.

The source had no gate — it always installed the stride and dispatched, diverging in
high-colour mode. Fix: added a `highColorMode` parameter (default `false` = the global's
default, matching the codebase convention used by shape_recon_cluster's `highColorMode`
bool) and gated the block on `!highColorMode`. Default arg keeps the live HUD callers
(wire_hud_bridge.cpp) and all tests source-compatible.

### BOUNDARY — 0x5D86C4 ShowFromBankScaled clip-extent recompute
The fall-through path (high-colour, or depth > 2) writes
`HIWORD(dword_64A1A2) = width/scale` (0x5d874f) and `word_64A1A6 = height/scale`
(0x5d876d). dword_64A1A2 is modeled elsewhere as a per-struct field
(`gamelogic_recon.h`/`entity_frame_update.h` `defaultBorder`), not a shared global
reachable from this signature; word_64A1A6 is not modeled at all. This cross-module
state write is documented as a BOUNDARY in the impl rather than faked. Return value (1)
is unaffected, so the dispatch behavior the live tree depends on is faithful.

### FIXED — 0x5ACAE0 quad winding test: 80-bit precision
Evidence (disasm 0x5acc92): the binary loads screenX/screenY floats, but keeps both
subtractions and both products on the x87 stack and compares them with `fcompp` — there
is **no** intermediate store to a 32-bit float. With eax=v0, edi=v1, esi=v2:
- product1 = (v0.sx - v1.sx)*(v0.sy - v2.sy)
- product2 = (v0.sx - v2.sx)*(v0.sy - v1.sy)
- `fcompp; sahf; jbe (skip)` => skip-cull when product2 <= product1; **cull when
  product2 > product1**.

Source had `float lhs/rhs` (= product2/product1), so each product was rounded to 32-bit
before the compare — a precision divergence vs the 80-bit x87 path. Per the brief
("a product kept in the x87 register stays 80-bit until the fstp; model with double"),
changed lhs/rhs to `double`. Inequality direction (`lhs > rhs` = product2 > product1)
was already correct.

### VERIFIED-1:1 notes (projection)
- BlitScaled16: 8.8 fixed-point sampling, BYTE1/SBYTE1 row idx, color-key uses HIBYTE,
  src row stride 2*(u16)width, return = last `srcStride * BYTE1(yAcc)`. All match.
- BlitRleScaled run walk: `countCur = runCur; runCur = countCur+4` per row (outer `++v23`
  after `v19=v23`); `phase = (skip >> ((phase+1)&31)) % scale` faithfully models
  `shr r/m32, cl` 5-bit mask. Both top-clipped/non-clipped branches unified via the
  per-pixel `y+i/scale >= clipY0` gate. Matches.
- Project depth-fade arm (0x5AC9A4): distSq = cy*cy+cx*cx+cz*cz; alpha clamp
  `min(t,255)` then `255 - t`; `(int)v39` stored to +0x4F under ConvertX truncate mode.
  dbl_628074 verified = 255.0 (bytes 00 00 00 00 00 E0 6F 40); float clamp 0x406FE000 =
  255.0. Matches (incl. TruncToByte = (u8)(i32)).
- No-fade arm (0x5ACB4F) and disabled arm (0x5ACC0B): match; byteOut = (movzx
  byteSrc>>2) -> sar by 2 on a zero-extended byte == logical, stored at +0x42. Matches.
- Effect-tint (0x5ACBBC): R=+0x5C, G=+0x60, B=+0x64, each ConvertX(trunc)+fistp+low byte;
  packed (R<<16)|(G<<8)|B; nodeType==8 -> 0x1F1FFF. `cmp al,5; jl` (signed) is
  behaviorally identical to the u8 `nodeType < 5` (>=128 skips in both). Broadcast loop
  is unconditional over count (`jbe` guards count==0). Matches.
- Quad pass: flags @+0x24 bit7 live, bit4 -> set bit6, flags2 @+0x26 bit2 = never-cull,
  stride 0x28. Vertex pointers are intentionally widened on the 64-bit host (documented
  in header) — functional, not byte-identical, layout.

---

## Counts
- Functions reviewed: 11 (3 sun, 8 sprite incl. 3 projection arms + tint + quad pass).
- VERIFIED-1:1 (no change): 6
- FIXED: 4 distinct fixes (light-table X-clip; byte_140694B gate x2 funcs; 80-bit winding)
- BOUNDARY: 1 (ShowFromBankScaled clip-extent recompute to cross-module globals)
- Tests: render_sprite_scale_test, render_billboard_project_test, sun_daycycle_test,
  render_sprite_scale_itest, render_sprite_scale_e2e_test — all PASS.
