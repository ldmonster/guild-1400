# Harden: sim/object_update.cpp (float/int-conversion audit)

Module: `src/sim/object_update.cpp` + `object_update.h`, tests `tests/unit/object_update_test.cpp`.
Source funcs: VIBE_Object_Update 0x40eea0, VIBE_Animation_Apply 0x415b78,
VIBE_EntityChild_Process 0x418f34, VIBE_Entity_InteractionLogic (0x41078c body).

Status: ALL provenance'd regions VERIFIED-1:1. Build green, 1/1 test passes.
No source or golden edits were required — every float->int site already matched
the binary's rounding mode.

## Float -> int sites (the critical audit)

ConvertX @0x5c6b08 = x87 RC=chop (truncate toward zero); modeled by ConvertXTrunc.

1. **FormatBuildPercent 0x40f6b0..0x40f710** (`%i%%` build percent)
   - disasm 0x40f6b7 `fld1`/`fcomp var_48`/0x40f6c3 `jb` -> clamp fill>1.0 to 1.0
     (compare done on the double-promoted float; `fill>1.0f` is equivalent).
   - 0x40f6ec `fmul dbl_610D7C` (=100.0) ; 0x40f6f2 `call ConvertX` ; 0x40f6f7 `fistp`.
   - => `(int)ConvertXTrunc(clamp*100.0)`. TRUNCATE. Source matches. VERIFIED-1:1.

2. **FormatBarPercent 0x411840..0x411878** (`%i%%` fill-bar label)
   - 0x411849 `fild`(cur) ; 0x411857 `fmul flt_610E30`(=100.0f) ;
     0x411864 `fild`(full) ; 0x41186b `fdivp` -> (cur*100)/full ;
     0x41186d `fadd dbl_610E34`(=0.5) ; 0x411873 `call ConvertX` ; 0x411878 `fistp`.
   - => round-half-up-via-truncate. The whole product/quotient stays 80-bit until
     fistp; modeled with double (`(double)cur*100.0/(double)full+0.5`). 100.0 vs
     100.0f exactly representable -> no diff. Source matches. VERIFIED-1:1.

3. **EntityChildScrollPercent 0x4190ea..0x41910c** (scroll-thumb percent)
   - 0x4190ea `fild [edx+244h]`(rangeTop) ; 0x4190f0 `fild [edx+264h]`(thumbPos) ;
     0x4190f6 `fdivp` ; 0x4190fd `fadd dbl_610F14`(=0.5) ; 0x419107 `call ConvertX` ;
     0x41910c `fistp`. => `(int)ConvertXTrunc(rangeTop/thumbPos + 0.5)`. VERIFIED-1:1.

4. **ComputeBarFraction 0x41089e..0x410920** (fill-bar lengths + <1px snap)
   - v5 = (double)maxLen/(double)(full-empty)  [fdivp @0x41089e].
   - filled = (cur-empty)*v5  : 0x4108b8 `fild`,0x4108bf `fmul st,st(1)`,
     0x4108c8 `fstp var_34` (var_34 is a FLOAT slot -> stored as float). NOT an int.
   - tgt    = (target-empty)*v5 : 0x4108cf `fild`,0x4108d6 `fmulp`,0x4108d8 `fstp var_48`(float).
   - snap test: 0x4108df `fldz`/`fcomp`/`jnb` => enter only if filled>0.0;
     0x4108ed `cmp var_34, 3F800000h`/`jge` => snap to 1.0 when bits < 0x3f800000.
     The `jge` is a SIGNED compare, but the >0.0 guard forces sign bit clear so it
     is identical to the unsigned `bits < 0x3f800000u` used in source. Same for tgt
     (0x410913/0x410920). VERIFIED-1:1.

5. **FormatAmount mass branch 0x40f079..0x40f370** (g/kg/t)
   - product computed in INTEGER: 0x40f073 `mov eax,[..]`/0x40f079 `imul eax,[..]`.
   - `>=1000` (0x40f085 `cmp eax,3E8h`/`jge`) and `>=1000000`
     (0x40f30c `cmp eax,0F4240h`/`jge`) are SIGNED. Source uses `int total` + `>=`.
   - kg: 0x40f31a `fild var_1C`(=int product) ; 0x40f321 `fmul dbl_610D74`(=0.001) ;
     fstp -> double sprintf arg "%.1f kg". t: 0x40f34b `fild` ; 0x40f352 `fmul dbl_610D6C`(~1e-6) ;
     "%.1f t". No float->int. `fild` of int is exact -> double model exact. VERIFIED-1:1.
   - g branch: "%li g" with the int product (recomputed identical value). VERIFIED-1:1.
   - zero-pad branch 0x40f375..0x40f3a4: `test dh,8`; format "%%0%ii " with
     `(unsigned __int8)v10` digits (0x40f37c `mov al,cl`), then count*unit. VERIFIED-1:1.

## Integer / control-flow sites

- **CountDigits 0x40f044**: signed `idiv`-by-10 loop, cap 10, 0 for v<=0. VERIFIED-1:1.
- **ChildPriorityClass 0x418fe4/0x41940d/0x41942b/0x419449**: signed `idiv` by
  8,6,4,2 in that order, `test edx,edx`(remainder)==0 -> store class. VERIFIED-1:1.
- **ObjectUpdateClampWidth 0x40eef4..0x40ef12**: `<48 -> 48`; cap = node[86];
  `if (cap>0 && width>=cap) width=cap`. VERIFIED-1:1.
- **EntityChildScrollArrows 0x419076 / 0x4190a9**: second comparison wins:
  0x41908b `eax=[edx+8]>>16`(pos16, SAR) + `[edx+248h]`(page) + `[edx+250h]`(step),
  0x4190a7 `cmp eax,[edx+244h]`(rangeTop), 0x4190a9 `jge` -> node235 = (sum>=rangeTop).
  node234 = (step!=0)?0:1. VERIFIED-1:1.
- **EntityChildBorderTiles 0x41924a / 0x4192c5**: horiz=([esi+6]>>16 -16) `sar 3`;
  vert=([esi+8]>>16 -16) `sar 3`. Both arithmetic (SAR) -> C `>>` on signed int.
  Negative results (<=0) gate to "no tiles" by caller. VERIFIED-1:1.

## Word-wrap (VIBE_Animation_Apply 0x415e11..0x416245)

The layout decision path (token split on space/'~', greedy pack to maxWidth,
single-word break-back to last '-'/'~') contains NO float->int conversion; the only
float math in this function is the optional texture-quad scale (0x415d72 `1.0/v60`,
0x415d98/0x415da7) which lives entirely on the renderer/hook side, not in the layout.
WrapText reconstructs the layout half; per-glyph emit (Coord_Transform advance,
Animation_Basic/Advanced/Velocity_Apply) is the renderer's hook seam (rule 8).
No float/int hazard. No change.

## Constants re-verified by get_bytes
- dbl_610D6C = 8d ed b5 a0 f7 c6 b0 3e  (~9.9999999748e-07, kWeightTon)
- dbl_610D74 = fc a9 f1 d2 4d 62 50 3f  (0.001, kWeightKg)
- dbl_610D7C = 00..00 59 40            (100.0, kPctScale100)
- dbl_610D84 = 00..00 e0 3f / dbl_610E34 = ..e0 3f / dbl_610F14 = ..e0 3f  (all 0.5)
- flt_610E30 = 00 00 c8 42             (100.0f, kBarH)

## Build / test
- `cmake --build build --target object_update_test -j` -> Built.
- `GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R '^object_update_test$'`
  -> 1/1 Passed.
