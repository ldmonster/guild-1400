# Harden wave — render 05: octree build + scene lights + transform

Scope: `src/render/{scene_recon_octree,scene_recon5_rebuild,scene_transform,scene_lights}.cpp`
Method: decompile + disasm each provenance'd function via IDA MCP (module gilde.exe,
imagebase 0x400000), diff line-for-line, confirm every float bit-pattern with get_int.

## Constants confirmed (get_int)
- `flt_62C178` @0x62C178 = 1056964608 = **0x3F000000 = 0.5f** (octree axis split). ✓
- Min seed = **0x501502F9 (+1e10)**, Max seed = **0xD01502F9 (-1e10)** — confirmed in
  the ComputeMeshNodeBounds disasm (`mov dword ptr [esi], 501502F9h` / `[ecx], 0D01502F9h`). ✓
- `flt_5CA2B0` @0x5CA2B0 = 0 (sun reference plane; NOT part of the affect test). ✓

## scene_recon_octree.cpp

- **octreePushMesh / AddMeshToCell 0x5efea0** — VERIFIED-1:1. Gate `drawCell &&
  drawCell->meshPresent(+260) && (obj+531 & 4)`; `++cell+48`; alloc 8B from dword_64A7D0
  pool; `[0]=obj`; append head/tail (+60/+64). Matches.
- **octreeFreeRecursive / FreeNodeRecursive 0x5efe50** — VERIFIED-1:1. Drain list (cache
  next before free), recurse octant[0..3] (`v4..v4+16` = 4 dwords), free cell block from
  dword_64A7CC. Matches.
- **ComputeMeshNodeBounds 0x5effa8** — VERIFIED-1:1 (logic) / BOUNDARY (asset reduce).
  `test [eax+0x212],0x80; jnz skip` ⇒ transform iff bit7 clear = `(flagNoPivot & 0x80)==0`. ✓
  Register convention confirmed via disasm: eax=node, edx=min[], ebx=max[] (two separate
  ptrs). Seeds min=+1e10/max=-1e10, reduces 8 corners, caches to drawData +144/+148/+152
  (min) and +160/+164/+168 (max). BOUNDARY: the 8-corner reduce +
  TransformBoundingVolume(0x5ad438) + ComputeBoundingBox(0x5c9e64) are mesh-asset coupled;
  surfaced via `computeNodeBounds`/`nodeMeshAABB` hooks. Seeding + write-back are 1:1.
- **CollectMeshesInBounds 0x5eff10** — VERIFIED-1:1. Args `(a1@edx = lo, a2@ebx = hi)`.
  Overlap test (2-D x/z slab on the node's cached drawData AABB):
  `min.x(+144) <= hi.x && max.x(+160) >= lo.x && min.z(+152) <= hi.z && max.z(+168) >= lo.z`.
  The source stores temp lo/hi as `{x, z, _}` and reads index 1 as z internally — a
  self-consistent re-labeling of the original `{x, _, z}` layout; identical results.
  Empty cell ⇒ free + return null. Matches.
- **SubdivideOctree 0x5f0160** — VERIFIED-1:1. Phase-1 AABB reduce: depth>1 reads cached
  drawData AABB; depth<=1 calls ComputeMeshNodeBounds. min uses `>=`→take-node, max uses
  `<=`→take-node (the original's mixed strict/non-strict min.x branch is logically the
  same function; equal-value picks node either way). halfX=(maxX-minX)*0.5,
  halfZ=(maxZ-minZ)*0.5. Quadrant geometry confirmed against the v45/v50 stack writes,
  INCLUDING the **Q3 lo.z = minZ + halfX quirk** (original `a1[6] + v55`, v55 = halfX, not
  halfZ) — faithfully preserved. Gate: `depth<maxDepth && minDepth<meshCount`. Collapse:
  marker=-1, seed from first non-null child's meshCount(+48), mismatch ⇒ keep split; all
  equal ⇒ free children + null them; else recurse depth+1. `result` tracking matches. ✓
- **BuildOctreeForRegion 0x5f05b0** — VERIFIED-1:1. alloc 72B; word[34]=regionMask;
  dword[13]=minDepth(a4); WalkAndInvoke(off_649D64, root, AddMeshToCell, regionMask, cell);
  SubdivideOctree(cell, minDepth, maxDepth, 1). On meshCount!=0: stamp each node's
  drawData+176 = -1 then return cell; else free + return residual (0). ✓ (Confirmed arg
  order: SubdivideOctree(cell, a4=minDepth, a3=maxDepth, 1).)

## scene_recon5_rebuild.cpp

- **RebuildRegionOctree 0x5f0b4c** — VERIFIED-1:1. `if(!cell) return cell`; free 4 octant
  children; regionMask = `HIBYTE |= 2` (bit 9, 0x0200); WalkAndInvoke recollect with the
  new mask; `return SubdivideOctree(cell, cell+52(minDepth), cell+56(maxDepth), 1)`. The
  reconstruction threads minDepth/maxDepth as params (the live engine reads them from the
  cell, where BuildOctreeForRegion stored them) — interface-equivalent, documented. ✓

## scene_transform.cpp

- **MatrixFromEuler 0x5cb1bc + repack** — VERIFIED-1:1. Decoded the 16-float matrix the
  util writes (rows at byte +0/+16/+32):
  Row0 [cy·cz, sx·sy·cz−cx·sz, cx·sy·cz+sx·sz], Row1 [cy·sz, cx·cz+sx·sy·sz, cx·sy·sz−sx·cz],
  Row2 [−sy, sx·cy, cx·cy]. The repack picks `m[0..2],m[4..6],m[8..10]` into the 3x3 — exactly
  the +0/+16/+32 rows. Header comment matrix verified element-for-element. (Original keeps
  sin/cos products in x87 80-bit; that lives in the reused util MatrixFromEuler, out of
  this file's scope.) WorldToView/CameraForward/Transpose/Multiply/Apply are the standard
  R^T mapping the header documents; consistent. ✓

## scene_lights.cpp

- **LightAffectsObject / CollectAffectedObject 0x5c80a0 (count-pass predicate)** —
  VERIFIED-1:1.
  - Sun arm @0x5c80f8: `0.0==intensity(+148) || (flags+529 & 0x10)` ⇒ not affected; the
    0x10 disable bit and the position-independence (sun ignores distance) match.
  - Point arm @0x5c8150: `L+484 = L+144` (cullRadius=range, linear); `dist =
    sqrt(Σ(L+472..480 − O+472..480)²)` (full 3-D); `0.0==intensity || (L+484 + O+484) <=
    dist` ⇒ not affected. Both operands added verbatim (no sqrt of O+484); matches the
    source's linear add. ✓
- **CollectObjectLights / BuildObjectCache 0x5c8218 (two-pass collect driver)** —
  VERIFIED-1:1 (collection payload). count-pass then alloc(4·count) then fill-pass append
  in walk order then ApplyToCachedVertices; collapsed to one walk here (fill recull-free, so
  byte-identical order). Point→MeshPointLight, sun→single sun. ✓
- **ApplyToCachedVertices 0x5c6f90 (sun-arm note)** — BOUNDARY (wave-7 per-vertex core, not
  reconstructed here). DISASM CORRECTION: 0x5c7044 peeks slot[0] (`cmp [edx+215h],7; jnz
  scan`) and 0x5c704d scans forward to LOCATE the first type-7 — it does NOT "break to one
  sun". The per-vertex sun arm then accumulates from that index onward (can use multiple
  suns). The single-sun behavior in CollectObjectLights is justified by the wave-7 consumer
  `LightMeshVertices` taking a single sun dir/color/intensity — not by a break.
  FIXED (comments only, no behavior change): corrected the misleading "BREAKS at first
  type-7" wording in scene_lights.cpp (driver note + per-sun line), scene_lights.h
  (CollectObjectLights doc), and the (g) FirstSunWins test comment. Cites 0x5c7044/0x5c704d.

## Tests
- scene_recon_octree_test ....... PASS (4 tests: ComputeBounds, CollectSlab, SubdivideSplit, GateStop)
- scene_recon5_rebuild_test ..... PASS (2 tests: RebuildNullCell, RebuildFreesAndRemask)
- scene_transform_test .......... PASS (5 tests: identity/yaw/pitch/ChooseCity/WorldToView)
- scene_lights_test ............. PASS (19 tests: cull predicate, collect driver, handoff,
  hardening w1-w3, const-pins W13a-c/W15, disabled-sun)
- ctest: **4/4 suites passed, 0 failed**.

## Net
No reconstruction logic changed (no churn). All four sources verified 1:1 against the live
disasm; constants/bit-patterns confirmed; the one inaccuracy found was a comment mis-
describing the ApplyToCachedVertices sun-arm mechanism (corrected with addresses + the
single-sun justification preserved). Tests green.
