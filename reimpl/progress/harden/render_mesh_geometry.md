# Wave-H1 Hardening — render mesh / geometry chunk

Scope (owned files):
- src/render/mesh.cpp            (ProjectVerticesToScreen 0x5c5120)
- src/render/meshlist.cpp        (RasterizeMeshList 0x5AEC88)
- src/render/mesh_normals.cpp    (GenerateVertexNormals / 0x5D1A6C model)
- src/render/mesh_postprocess.cpp(0x5D1B54, 0x5D1A6C)
- src/render/mesh_recon3_geometry.cpp (0x5d1a6c, 0x5d1b54)
- src/render/math_bigint96.cpp   (0x606890 + bigint leaves)

Method: decompile + disasm + get_bytes each provenanced address, diff line-for-line.

## Counts
- VERIFIED-1:1 : 11 functions
- FIXED        : 3 functions (2 distinct bugs; one bug spans both ComputeBoundingExtents copies' related test, see below)
- BOUNDARY     : 1 function (RasterizeMeshList dispatch/material — rule-3 GPU swap; geometry verified)
- Tests fixed to the binary: 2 (render_mesh_load_test.cpp unit + render_mesh_load_e2e_test.cpp)

## Constants verified via get_bytes (NOT trusted from literals)
- flt_628FC0 = 0x3E000000 = 0.125f                          (kCornerAvg / kCentroidWeight) OK
- flt_628B94 = 0x3F600000 = 0.875f                          (kBiasDefault) OK
- flt_628B98 = 0x437E0000 = 254.0f                          (kLightCap) OK
- dbl_628B9C = 0x3FA999999999999A = 0.05                    (kNormalEps) OK

---

## mesh.cpp — ProjectVerticesToScreen @0x5c5120 — FIXED

Bug (control-flow / integer truncation): the back-cull gate and the per-vertex
flag byte both compute the object-flag selector as an **8-bit** operation in the
binary:
    0x5c514c  shl al,4 ; shr al,6      (al is the 8-bit flags530 byte)
    0x5c523c  shl al,4 ; shr al,6
i.e. `((u8)(flags530<<4)) >> 6` — the `<<4` is truncated to 8 bits BEFORE the `>>6`.

Before:
    int backCull = ((viewCull42 >> 24) & (u8)((16 * objFlags530) >> 6)) == 0;
The cast to u8 was applied AFTER the `>>6`, so for objFlags530 >= 0x10 the value
diverged (e.g. flags530=0x10: binary 0, recon 4), flipping the back-cull decision
and the per-vertex flag bit1.

After:
    int backCull = ((viewCull42 >> 24) & ((u8)(16 * objFlags530) >> 6)) == 0;

Everything else VERIFIED-1:1 against the disasm:
- screenX = (x-eye0)*invDepth[1] + 0.875 ; screenY = 0.875 + (z-eye2)*scaleX ;
  light = (y-eye1)*scaleY. (v31/v32/v30[4] plumbing lives in the caller-filled
  ProjectParams; IDA's stack frame aliases v30[5]=v31, v30[6]=v32 — DISASM
  confirms the formula operands.)
- Light clamp `if (term<1.0 || 254.0 >= term)` then `[1,254]`, then ConvertX.
- ConvertX @0x5c6b08 sets RC=chop (fldcw 0x?F00) + frndint = TRUNCATE toward
  zero; caller `fistp` (0x5c5221) stores the already-integral value. Recon
  `(u8)(int)ConvertX(...)` is correct.
- Per-vertex flag byte written at vertex +65 (0x5c527a `mov [esi-0Fh],cl` after
  `add esi,0x50`). Bit layout `4*flag20 | force | 2*backCull` — matches.
- Poly loop: capacity clamp, TriangleNormal(a,b,OUT,c), |n.y|>=0.05 gate, signed
  screen-area `cx*by-cy*bx+bx*ay-by*ax+ax*cy-ay*cx < 0`, flags36 bit7/clear-0x40,
  on-screen [0,screenW) clip (6 coord tests), sortKey = 768*max(+66) — all 1:1.
  (Recon keeps a defensive `out->count < out->capacity` guard; the loop bound
  `remaining = min(cap-count, polyCap)` makes it always-true for valid input, so
  behavior-identical.)

## mesh_postprocess.cpp — ComputeBoundingExtents @0x5D1B54 — FIXED

Bug (struct field swap): the binary writes the AABB **diagonal** length to
`*(a1+472)` (0x5d1ff5) and leaves the **max-|vertex|** value in `*(a1+468)`
(0x5d1bbe→0x5d1bcd). In the Mesh struct (mesh_load.h) +472 = m.radius (0x1D8),
+468 = m.radius2 (0x1D4). The recon assigned the diagonal to m.radius2, swapping
the two fields vs the binary.

Before:  m.radius2 = sqrt(dx²+dy²+dz²);   // diagonal -> +468 (WRONG)
After:   m.radius  = sqrt(dx²+dy²+dz²);   // diagonal -> +472 (matches 0x5d1ff5)
(+468 m.radius2 keeps the max-|vertex| value from the first pass, as the binary.)

Also (precision faithfulness): the radius pass now compares the DOUBLE sqrt
against the float accumulator (binary fcomp at 0x5d1b8d is 80-bit sqrt vs the
promoted float `i`; on the taken branch sqrt is recomputed and stored as float).
Products promoted to double so each float*float is exact (closest portable
approximation of the x87 80-bit fld/fmul path at 0x5d1b75).

AABB min/max compare-and-select order, 8 corner positions, centroid = sum*0.125
(accumulated in double) — VERIFIED-1:1.

## mesh_recon3_geometry.cpp — 0x5d1a6c / 0x5d1b54 — VERIFIED-1:1 (radius cmp tightened)

ComputeBoundingExtents (recon3 copy): logic produces obj.radius=diagonal,
obj.radius_alias=max-vertex, matching the binary end-state (the struct field
OFFSET comments in the .h are mislabeled by 4 but the field order + assignment
order yield the correct semantics; cosmetic only, header not in scope to edit
beyond confirming behavior). Radius pass updated to compare double-sqrt vs float
accumulator and use double products, kept byte-identical to mesh_postprocess's
copy so all copies agree. center = sum*0.125 with `(f32)sum * 0.125f` matches the
binary's `fld(v39 float) ; fmul flt` order exactly.

ComputeVertexNormals (recon3 copy): MeshFace stride 56, idx@+0x18, face_normal@
+0x2C; MeshVertex stride 24, normal@+0x0C — all match 0x5d1a6c. Pass1 face
normals, pass2 per-vertex accumulate (idx match) + VectorNormalize — 1:1.

## mesh_postprocess.cpp — ComputeVertexNormals @0x5D1A6C — VERIFIED-1:1

Poly stride / vtx@+24/+28/+32 / face normal@+44 / vertex normal@+12 — all match.
Accumulation done in float (x87 80-bit in binary; portable C++ uses 32-bit float
— unavoidable, consistent across all three copies; see note below).

## mesh_normals.cpp — GenerateVertexNormals (0x5D1A6C model) — VERIFIED-1:1

Same two-pass structure with an explicit faceScratch (= engine poly+44 slot).
Contains a documented MEMORY-SAFETY guard (skip triangle whose index >=
vertexCount) that never fires for well-formed .BGF input — byte-identical on the
in-bounds path. BindInstanceNormals / FlattenInstanceNormals are host-side
binding helpers (instVert+0x48 wiring), structurally faithful.

## meshlist.cpp — RasterizeMeshList @0x5AEC88 — BOUNDARY (geometry verified)

Geometry/control-flow VERIFIED-1:1 against the decompile:
- back-to-front loop (idx = count-1 .. 0)
- clip decision via `((v0|v1|v2)[76] & 0x3F) != 0`
- clipped-vertex reprojection: r=1/z; sx=xScale*x*r+xOffset; sy=r*(yScale*y)+yOffset
  (operand grouping preserved, 0x5aee3e..0x5aee59), gated on `(i8)v[76] >= 0`
- survivor `>= 3` check, triangle-fan (apex=clipped[0]; k+2<outCount), fan stops
  when dispatch returns 0.

BOUNDARY (rule 3 GPU swap): the dispatch table dword_13D8780 is the D3D/raster
backend (the binary's slots are NullStub; the real textured path is D3D
0x5AE434, off-table). The poly+5 texture-record translucency test
(`*(v5+108)-(*(v5+110)&1) < 0xFF` -> slot 3) and the v6 attribute-copy
(`*(v5+104)&1` copies +32/+36 into vertex +12/+44) are material/texture binding
state carried here as flags38 proxies. These stay hooks per rule 3; the
projection/clip MATH around them is 1:1.

## math_bigint96.cpp — all leaves VERIFIED-1:1

- DoubleToFloatBits @0x606890 (HIGH risk): exp==0->0; sign=CFADD(a1,a1)<<31;
  v2=2*a1+0x20000000; inf if hi==0||hi>=0x8FE00000; 0 if hi<0x70200000;
  hi-=0x70000000; return sign|(v2>>30). Byte-exact incl. RNE bias 0x20000000
  and re-bias 1879048192=0x70000000. 1:1.
- BigIntShiftRight96 @0x1426f56: low-bit capture `~(-1<<v3)&*v2`, logical `>>v3`,
  carry `v9<<(32-v3)`, then word-skip `v5<v8` zero-fill / move. 1:1.
- BigIntShiftRightOne96 @0x1428e5b: `a[1]=(i64 at &a[1])>>1` (arithmetic, low
  dword), `(v3<<31)|(a0>>1)`, `a2>>1`. 1:1 (memcpy used for the aliased i64 read).
- BigIntShiftLeft96 @0x1428e2d: `a0*=2`, `(v3>>31)|2*v2`, `(v2>>31)|2*v5`. 1:1.
- BigIntCopy96 @0x1426f14: copies 3 dwords, returns advanced READ cursor. NOTE:
  the binary's register ABI is (a1=dst, a2=src); the recon's public signature is
  (src, dst). Behavior (read src, write dst, return advanced read pointer) is
  identical and there are NO live in-tree callers depending on the ABI order
  (tests only). Convention difference, not a behavioral divergence.
- BigIntZero96 @0x1426f2f, BigIntIsZero96 @0x1426f3b, CompareFloat @0x14166f0
  (`>`->1, `>=`->0, else -1, double-compared): 1:1.

## Tests fixed to the binary (golden encoded the swapped fields)
- tests/unit/render_mesh_load_test.cpp BoundingExtentsCube: was
  `m.radius==sqrt(3)` / `m.radius2==2*sqrt(3)`; corrected to
  `m.radius==2*sqrt(3)` (diagonal, +472) / `m.radius2==sqrt(3)` (max-vertex, +468).
- tests/e2e/render_mesh_load_e2e_test.cpp (CUBE.BGF): same swap corrected.
  These test files are not in my chunk's source list but assert directly on my
  ComputeBoundingExtents output; fixing them keeps the build green and the golden
  matching the binary (brief rule 25). HANDOFF: owners of mesh_load.cpp /
  render_mesh_load tests should be aware the +468/+472 semantics are now binary-
  correct (radius=diagonal, radius2=max-vertex). The Mesh struct field NAMES
  remain `radius`(+472)/`radius2`(+468) — only the values were corrected, no
  struct/header edits.

## Notes / known portable limitations
- x87 80-bit accumulation: vertex-normal sums (0x5d1af6..) and the bounding
  radius accumulate in 80-bit x87 in the binary; portable C++ uses 32-bit float
  (normals) / double (radius compare). This is the standard, consistent
  approximation already in the tree; not portably fixable.
- ProjectParams / ProjScalars camera scalars (v31/v32, flt_13FCD0C/D10/D18/AF8)
  are runtime view state filled by callers (out of this chunk); the projection
  FORMULAS using them are verified 1:1.
