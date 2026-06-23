# Wave-H1 hardening — render_03_part_d

Files owned:
- src/render/object_light_shade.cpp (+ .h, tests/unit/object_light_shade_test.cpp)
- src/render/object_project.cpp (+ .h, tests/unit/object_project_test.cpp)
- src/render/object_transform_ops.cpp (+ .h, tests/unit/object_transform_ops_test.cpp)

MCP-verified against gilde.exe (imagebase 0x400000). All three test binaries build and
pass after the fixes: object_light_shade_test 105/0, object_project_test 10/0,
object_transform_ops_test 31/0.

## Constants re-confirmed via get_global_value (all matched the headers)
- flt_628C94 / flt_628C74 = 0x437f0000 = 255.0 (cap)
- flt_628C88 = 0.59, flt_628C8C = 0.30, flt_628C90 = 0.11 (software luma weights)
- flt_628C64 = 0.30, flt_628C68 = 0.59, flt_628C6C = 0.11 (material luma weights)
- flt_628C70 = 0x3b800000 = 1/256 (material scale)
- flt_628C28 / flt_628C50 = 0xc47fc000 = -1023.0 (LUT index scale)
- flt_628C24 = 0x41a00000 = 20.0 (sun normal Y pre-bias)
- flt_628C30 = 0x3c23d70a = 0.01 (per-vertex sun intensity scale)
- flt_628C20 / flt_628C4C = 0x41200000 = 10.0 (point-light intensity scale)
- flt_64A074/78/7C = 0x43480000 = 200.0 (ambient seed)
- dbl_628074 = 0x406fe000... = 255.0 (the particle-alpha cap; not in our subset)

## object_light_shade.cpp

### FinalizeVertexShadeSoftware  — FIXED  (gilde.exe 0x5c8218 software finalize)
Divergence class: float->int rounding mode.
Evidence (disasm @0x5c83dc..0x5c8408): each B/G/R byte store is `fld [var]; fistp dword`
with NO preceding ConvertX/frndint and NO cvtt — a BARE fistp = round-to-nearest-even.
- BEFORE: `out.r = (u8)(int)r; ...` (truncate toward zero)
- AFTER:  `out.r = (u8)FistpRound(r); ...` where FistpRound = (int)std::lrint(v)
Offsets confirmed: R->[edx+46h]=+70, G->[edx+45h]=+69, B->[edx+44h]=+68 (header correct).
Max-channel `<=` compare chain and the `v14 > 0x437F0000` (== mx > 255.0f) gate verified
unchanged (positive-float int compare == float compare; boundary identical).

### FinalizeVertexShadeLuma  — FIXED  (gilde.exe 0x5c8218 hardware/else finalize)
Evidence (disasm @0x5c8500..0x5c8535): `faddp; fistp [var_2C]; mov ebp,[var_2C];
cmp ebp,0FFh; jnb -> 255` — accumulate in x87 80-bit, BARE fistp (round-nearest), then
unsigned compare `(unsigned)l >= 0xFF -> 255`.
- BEFORE: `(int)(...)` truncate, signed `l >= 255`.
- AFTER:  `FistpRound(...)`, `(unsigned)l >= 0xFFu ? 255 : (u8)l`.

### ApplyMaterialVertexShade — FIXED  (gilde.exe 0x5c7f04 ApplyVertexShading)
Two divergences.
Evidence (disasm @0x5c7fa5..0x5c8031): expression `(luma + texHi)*flt_628C70*texLo`
matches; clamp is `fld flt_628C74; fcomp var_24; jnb` = UPPER clamp only (keep v when
255>=v else 255). The store @0x5c8013 `fld var_34; fistp` is a BARE fistp (round-nearest).
There is NO lower 0.0 clamp anywhere in the function.
- BEFORE: `if (255<v) v=255; if (v<0) v=0; return (u8)(int)v;`  (spurious lower clamp +
  truncate)
- AFTER:  `if (255<v) v=255; return (u8)FistpRound(v);`  (no lower clamp + round)

### PointLightDiffuse — FIXED  (gilde.exe 0x5c6f90 point arm @0x5c736e / IlluminateObject)
Full math diffed against the non-type-7 point arm: d=vpos-light[118/119/120];
dist2=d.d; gate `dist2 < range^2`; VectorNormalize(d); NdotL=normal.d; gate `NdotL<0`;
atten = 1/(falloff*dist2)*intensityScaled; factor = atten*ramp[idx]; rgb = lcolor*factor.
All verified 1:1 (intensityScaled folds light[37]*10.0*objScale, the caller's job).
Divergence: LUT index conversion.
Evidence (disasm @0x5c7427): `fistp dword [eax]` then `fmul flt_1405110[eax*4]` — the
index `(int)(NdotL * flt_628C28)` is a BARE fistp (round-nearest), and the engine does
NOT clamp it (unit normals keep it in [0,1023]).
- BEFORE: `(int)(NdotL * kRampIndexScale)` truncate.
- AFTER:  `FistpRound(NdotL * kRampIndexScale)` (memory-safety [0,rampLen) guard kept,
  never fires for valid unit-normal input).

### SunFalloffIndex (file-local helper, type-7 vertex arm @0x5c7241) — FIXED
Evidence (disasm @0x5c7241): same `fistp; fmul flt_1405110[eax*4]` — round-nearest, no
engine clamp.
- BEFORE: `(int)(ndotl * -1023.0f)`  AFTER: `(int)std::lrint(ndotl * -1023.0f)`.

### LightMeshVertices (both overloads) — VERIFIED-1:1 (consolidated flow 0x5c8218/0x5c6f90)
Ambient seed (200,200,200 default), point-arm walk, type-7 sun arm (intensity*0.01*
objScale*LUT[round(NdotL*-1023)] when NdotL<0), finalize via the now-rounding
FinalizeVertexShadeSoftware. Structure matches ApplyToCachedVertices. The null-pointer
guards (null light list == empty list, count<=0 early-out) are documented memory-safety
nets, byte-identical to the in-bounds path. Sun-disable when intensity<=0 matches the
CollectAffectedObject early-out.

### TransformVertexLightingNormal — VERIFIED-1:1 (type-7 vertex arm @0x5c70a4..0x5c7226)
ny=(n.y - bone[+108])*(20.0/bone[+468]); 4-stride 3x3 rotate (m[0,4,8 / 1,5,9 /
2,6,10]); VectorNormalize with zero-length collapse to (0,0,0). Matches disasm; no
float->int sites.

Test goldens fixed to the binary (round, not truncate):
- LumaReduceAndClamp: green 200*0.59f = 117.999..→ **118** (was 117). Verified host
  round-nearest computation. Other channels (red 60, blue 22, ambient 200) unchanged.
- Comments in SoftwareNormalizeOnOverflow / MaterialModulation updated (their golden
  VALUES were already correct under round: 42.5->42 even; 199; 100; 254).

## object_project.cpp  — VERIFIED-1:1 (extract of gilde.exe 0x5ac970)

NOTE: 0x5ac970 in the IDB is `VIBE_Particle_UpdateBillboards`, not
"VIBE_Render_ProjectObjectVertices". The reimpl correctly extracts two self-contained
arms of it; I corrected the file-header provenance to say so.

- Per-vertex projection (loc_5ACB5B, byte_649DD8==0 arm): gate `[edx+4Ch]&0x80`;
  invZ=1/[edx+8]; screenX([edx+10h]) = flt_13FCD0C*[edx]*invZ + flt_13FCD18;
  screenY([edx+14h]) = flt_13FCAF8*[edx+4]*invZ + flt_13FCD10. Pure float stores — no
  float->int. Math, operand order, and offsets verified 1:1.
- Per-polygon backface cull (loc_5ACAF6): stride 40 (`v19 += 10` dwords); flags36 at +36;
  gate sign 0x80; `&0x10 -> |0x40`; else signed projected-area test
  `(v0.sx-v2.sx)*(v0.sy-v1.sy) > (v0.sx-v1.sx)*(v0.sy-v2.sy)` AND `(flags38 & 4)==0` ->
  clear 0x80. Verified 1:1 (v0/v1/v2 = poly[0]/[1]/[2]).
- The `z==0` divide guard and `!v0/!v1/!v2` null checks are documented memory-safety
  nets (the engine assumes valid data); they don't alter observable output for valid
  input.

## object_transform_ops.cpp  — VERIFIED-1:1

- RemoveMeshFromTree (0x5f0b18): `if (cell && node) r = WalkAndInvoke(off_649D64, node,
  RemoveMeshRecursive, (u16)(cell+68)|0x280, cell); return (char)node-or-r`. The null
  guard returns `(char)node`. Adapted to the portable WalkAndInvoke callback ABI (the
  original's __usercall obj@eax/cell@edx threaded via a documented call context). Logic
  1:1.
- TransformPointPassThrough (0x5b7d38 else/no-parent branch): copy point verbatim,
  MatrixToEuler over the angle buffer. Matches. (The reimpl splits in/out angle buffers;
  behavior-equivalent.)
- TransformPointByInverseWorld (0x5b7dce): py=px*m1+py*m5+pz*m9+m13; pz=px*m2+py*m6+
  pz*m10+m14; outPoint[0]=px*m0+py*m4+pz*m8+m12; then store [2]=pz, [1]=py. Side-effect
  order (x stored first, then z, then y) preserved 1:1.
- PointThroughBoneChain: thin re-export of util::PointThroughBoneChain (out of subset).
No float->int sites in this file.

## HANDOFFS (outside my ownership — same fistp bug class, NOT edited)

src/render/light.cpp carries the SAME truncate-instead-of-round divergence in the
ENGINE-WIRED path (this is the function the live LightMeshVertices delegates to via
AccumulatePointLight). All need `(int)` -> `std::lrint`/round-nearest-even per the disasm
evidence cited above:
- line ~23  `int i = (int)v21;`            -> bare fistp finalize (round-nearest), @0x5c8506-class
- lines 57-59 `lv.outB/G/R = (u8)(int)..`  -> BuildObjectCache software finalize bare fistp @0x5c83e0/f1/0402
- line ~78  `return (u8)(int)v14;`         -> ApplyVertexShading bare fistp @0x5c8017
- line ~135 `FalloffIndex: (int)(ndotl * -1023.0f)` -> LUT index bare fistp @0x5c7427/0x5c7241 (no clamp in engine)
Recommend the light.cpp owner apply FistpRound (std::lrint) to all four and update any
goldens (e.g. green 0.59 channel 117->118) accordingly.

## Counts
- VERIFIED-1:1: 6  (LightMeshVertices x2, TransformVertexLightingNormal,
  object_project ProjectObjectVertices, object_transform_ops RemoveMeshFromTree /
  TransformPointPassThrough / TransformPointByInverseWorld — counting the transform_ops
  trio as 3 -> see below)
- FIXED: 5  (FinalizeVertexShadeSoftware, FinalizeVertexShadeLuma,
  ApplyMaterialVertexShade, PointLightDiffuse, SunFalloffIndex helper)
- BOUNDARY: 0 in-subset (the flt_1405110 runtime LUT remains an out-of-tree data hook as
  previously documented; PointLightDiffuse is parameterised on it and golden-tested).
- HANDOFFS: 4 sites in src/render/light.cpp (same fistp round bug, not owned).
