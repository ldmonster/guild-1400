# HARDEN render_08 — src/render/tile_lighting.{cpp,h}

1:1 line-for-line diff of every provenanced function against the gilde.exe disasm
(DISASM > Hex-Rays). MCP module `gilde.exe`, imagebase 0x400000.

## Counts
- VERIFIED-1:1: 6
- FIXED: 3
- BOUNDARY (rule-8 / API-fold notes): 2

## Constants / table — VERIFIED-1:1
`get_bytes` confirmed byte-for-byte:
- `kTerrainTypePatterns` @0x5c4690 (9-byte stride x15): `_ill*`,`_unk*`,`SAND`,`ERDE`,
  `WIESE`,`MOOR`,`PFLASTER`,`KIESEL`,`FELS`,`EIS`,`WASSER`,`WEG`,`WEG`,`_ill*`,`` —
  135 bytes match source exactly.
- flt_628878 = -4.0 (`00 00 80 C0`), flt_628874 = 2.0 (`00 00 00 40`),
  dbl_62887C = -1023.0 (`00 00 00 00 00 F8 8F C0`), flt_62886C = 0.5 (`00 00 00 3F`),
  flt_628868 = 0.5 (`00 00 00 3F`). All confirmed.
- ConvertX @0x5c6b08: sets x87 CW HIBYTE=0x1F (RC=11 truncate-toward-zero), frndint,
  restores CW. VERIFIED truncate-toward-zero.

## BuildTileIlluminationTable (build half of 0x5c4718) — VERIFIED-1:1
Disasm @0x5c475c..0x5c479e: `byte_1405100[i]=1`; `if (name[0])` loop k in [0,15)
`StrToUpper(name)` then `loc_5CB930` strstr, first match -> `byte_1405100[i]=k; break`.
Empty pattern (idx 14) always matches. Matches source.

## ComputeTileIllumination (lookup half of 0x5c4718) — FIXED (comment) + VERIFIED
cell = (mask&x) + size*(mask&y); high-bit gate `test byte ptr [eax],80h; jnz -> 0`
@0x5c4739. VERIFIED.
- FIX (comment, addr 0x5c47b7..0x5c47be): the lookup is `mov al,[eax]; and eax,0FFh;
  mov al,byte_1405100[eax]` — the FULL byte t indexes the table, **no `& 7` mask**.
  The 0x80 gate guarantees t in [0,127]; the engine's type grid only stores slot
  indices 0..7, so on the reachable domain `t == t&7` (behavior-identical). Source
  keeps `t & 7u` as a behavior-identical memory-safety pin for the 8-entry struct;
  comment corrected to state the binary uses the raw byte (was: "type byte (0..7)").

## StampLightCircle (0x5bc294) — VERIFIED-1:1 + BOUNDARY
Disasm-verified: row clamp [0,size-1] @0x5bc3b8/0x5bc3da, col clamp @0x5bc3e5,
distance `(row-cy)^2+(col-cx)^2 < r2cap` @0x5bc419, single-cell `types[size*cy+cx]`
@0x5bc44b. Integer stamp body byte-for-byte.
- BOUNDARY: binary branches single-cell vs circle on the PRE-truncation FLOAT radius
  (`v15 > 1.0` @0x5bc371); the integer API folds this to `r <= 1`, agreeing except
  for float radius in (1.0,2.0). The float radius derivation (bone-chain transform +
  pick) is the deferred outer walk — not reconstructible from integer inputs. Documented.

## BuildTileElevationByte (part of 0x5c47dc) — VERIFIED-1:1
`((double)(i16)h * scaleH + originY - tileMinY) * invScaleY`, then ConvertX truncate
to byte. Matches disasm @0x5c4c39..0x5c4c64 / @0x5c49b2..0x5c49cf.

## MipDownsample helper — VERIFIED-1:1
`(a+b+c+d) >> 2` unsigned. (Standalone reduce helper; the engine's in-place pyramid
is in BuildLitTileGeometry below.)

## BuildLitTileGeometry (0x5c47dc) — FIXED (branch 2) + VERIFIED (branch 1)
### Branch 1 (r != 0, direct fill + in-place midpoint pyramid) — VERIFIED-1:1
- Direct fill: heights index reconstructed = cell = (ty*r)*S + tx*r (disasm
  @0x5c4c28: `imul edx, r; add edx, S*r*ty` — confirmed, Hex-Rays had lost the reg);
  entries store `24*cell` @0x5c4c94. Inner loop runs tx in [0,N) (`cmp ecx,N; jb`
  @0x5c4caf — the Hex-Rays `v32+1 < v31` was a decompiler post-increment artifact).
- Midpoint pyramid @0x5c4cfb..0x5c5078: avg4 = (A+B+C+D)>>2 (sum of 4 unsigned bytes
  is always >= 0, so the compiler's trunc-toward-zero /4 == >>2); top-edge
  `(avg4 + 2*(C+A))/5u` @0x5c4f11, left-edge `(avg4 + 2*(B+A))/5u` @0x5c4f6f, centre
  `avg4` @0x5c4f86; entries copy entries[A] to all three midpoints. Strides 24-byte,
  class byte +0. All operand indices (A,B,C,D, rowA/rowB/rowH, xr) verified.
### Branch 2 (r == 0, ratio^2 box downsample) — FIXED
- FIX (addr 0x5c49cf..0x5c49d8 + 0x5c49fb):
  - BEFORE: `i64 sum = 0; ... sum += (i64)ConvertX(e); ... hh[..] = sum/(i64)ratio2;`
  - AFTER:  `u32 sum = 0; ... sum += (u32)(i32)ConvertX(e); ... hh[..] = sum/ratio2;`
  - EVIDENCE: accumulator is the 32-bit reg `edi`; each sample is `fistp [qword];
    mov eax, dword ptr[low32]; add edi, eax` — only the LOW 32 bits feed a 32-bit
    UNSIGNED accumulator. Final divide `mov eax,edi; xor edx,edx; div ebx` @0x5c49fb
    is a 32-bit UNSIGNED divide. The prior i64 signed form diverged for negative
    elevations / 32-bit wraparound.
- Illumination histogram (15 bins) + first-max vote (`v22=-1`, `if (v25 < bins[k])`)
  @0x5c4a90..0x5c4aad, store `(best==-1)?0:best` at `24*(cy*S+cx)`: VERIFIED.

## StampSlopeLight (slope half of 0x5bc45c) — VERIFIED-1:1
Disasm @0x5bc6f0..0x5bc7d8: normalize (1/sqrt via fdivrp), dot with light dir,
`if (d <= 0 -> jbe skip)`; `idx = (int)ConvertX(d * dbl_62887C[-1023.0])` @0x5bc78b;
`lit = falloffScale * flt_1405110[idx]` then **BARE `fistp dword ptr [eax]`** @0x5bc7b3
(NO ConvertX before it -> round-to-nearest-even; std::lrint matches) — the existing
comment's disasm claim is VERIFIED. Clamp `cmp ebx,7Fh; jbe` @0x5bc7bb then `or [eax]`
@0x5bc7d6. The idx clamp in source is a documented behavior-identical safety pin (no
clamp in binary; valid unit light dir gives idx in [0,1023]).

## QuadPolyVisible (visibility half of 0x5bc45c) — FIXED
- FIX (addr 0x5bc982..0x5bcb30; register map bl=h0, dl(==cl @0x5bc97c)=h1, dh=h2,
  bh=h3; loc_5BC9A2 or 0x80 = Hidden, loc_5BCA8D/AB6/B07 and 0x7F = Visible,
  loc_5BC9A6 fall-through = Unchanged):
  - BEFORE truth table (collapsed-guard structured form): `[0,2,1,0,1,0,2,2,2,1,0,1,0,1,2,0]`
  - AFTER  truth table (disasm goto-cascade simulated instruction-by-instruction):
    `[0,2,2,1,1,0,0,1,1,0,0,1,2,2,2,0]`
  - EVIDENCE: stepwise simulation of every `test/jz/jnz` across all 16 inputs; the
    flat re-encoded form reproduces the disasm table exactly (cross-checked in python).
    The prior nested form mistranslated the G1/A69/inner-guard cascade.
  - Source rewritten as the faithful flat cascade; test golden updated to match.

## Test status
`render_tile_lighting_test`: PASS. `render_tile_lighting_e2e_test`: PASS.
`ctest -R tile_lighting`: 2/2, 100%.
