# Hardening report — render scene core (wave-20)

Scope: `src/render/{scene,scene_walk,scene_link,scene_node,scenegraph}.cpp`.
Method: decompile + disasm each provenance'd function via IDA MCP (module gilde.exe,
imagebase 0x400000) and diff line-for-line against the reconstruction. Only the five
listed source files were touched.

## Per-function status

### scene.cpp

- **RadixSortDrawList @0x5AEF34 — FIXED (return value).**
  Disasm shows two distinct exits: the count<=1 fast path at `0x5aef46` returns eax
  untouched (= input `count`), but every sort exit clears eax — each scatter loop does
  `xor eax,eax; mov al,key; ...; xor eax,eax` so eax==0 at the `retn`
  (`0x5af09a` for the 2-pass path, `0x5af1f4` for the 4-pass path). The reimpl returned
  `count` for the sort case.
  Before: `return count;` (sort path). After: `return 0;` (with the count<=1 path still
  returning `count`). All callers ignore the return; the unit test only asserts the
  count==1 path (`r==1`), which is unchanged. Verified the ping-pong (pass0 src=13FC584
  dst=13FC51C, even passes leave the result in base1), the per-pass key byte (record
  bytes 0/1/2/3 = little-endian bytes of the 4-byte sortKey), the histogram clear
  (the `(3*lo)&3` stosb trick nets a full 1024-byte zero == reimpl `memset(...,1024)`),
  the 255-step prefix sum, and the descending `--histogram` placement — all 1:1.

- **WalkAndInvoke @0x5AC738 (vtable form) — BOUNDARY (null-path abstraction).**
  The non-null sibling loop is 1:1 (childMask = walkMask&0xFDFF, `r>=0` child gate via
  `(v11&0x80)==0`, 0x200 sibling gate, +528 bit0 terminator). The original's null path
  (`a2==0`) seeds the four root-init copies then walks `*(a1+128)` as a separate
  recursion using the FULL walkMask, terminating on `&dword_13FCF4C` or null. The
  vtable form models this by feeding `vt.child(root)` into the sibling loop, which
  re-applies the 0x200/terminator-bit logic at the root-child level the original does
  not. For all live callers (shadow_light_list, scene_recon_octree, wire_scene_bridge)
  the walk masks used do not set 0x200 and the in-bounds output is identical; the
  faithful null-path reconstruction lives in scene_walk.cpp (UniverseRoot form). The
  `kWalkMaxDepth` recursion bound is a documented anti-stack-overflow guard that never
  trips on valid (shallow) trees. No ODR clash: the two WalkAndInvoke overloads have
  distinct signatures.

### scene_walk.cpp

- **TestNodeFlag @0x5ac6d8 — VERIFIED-1:1.** switch arms 0..8 map to mask bits
  0x20/0x80/0x100/(==1)/0x40/2/4/8/0x10; default false. Byte-exact vs decompile.
- **GetFirstActiveChild @0x5ac684 — VERIFIED-1:1.** parent(+504)!=0 ⇒ return +496; else
  walk +496 chain until (+528 bit0); offsets confirmed.
- **MarkDirtyFlag @0x5af298 — VERIFIED-1:1.** `+528|=4; if(a2) +530&=~0x80; +531&=~1;
  return 1` — exact.
- **LinkAsSibling @0x5b0b3c — VERIFIED-1:1.** `+528 = (head+504==0) | (+528&0xFE)`;
  inherit parent; walk to tail (+496); tail+496=node; node+500=tail; return tail.
- **SetParent @0x5b0b9c (trampoline form) — VERIFIED-1:1 (return is a documented
  don't-care).** Control flow exact. The original's return on the parent-with-no-
  firstChild branch is `(char)&node->flags528` (low byte of a heap address — a
  non-reproducible don't-care; all callers only test truthiness). The reimpl returns
  the flags byte there; on the LinkAsSibling path both return 1; on the no-parent path
  both return the linkIntoScene byte. Behaviorally equivalent (truthiness preserved on
  the live paths).
- **TraverseTree @0x5ac86c — VERIFIED-1:1.** Non-null sibling loop and null root-child
  loop both match; root-init seed (root[32/33/41/42]) gated on `a1==off_649D64`;
  sentinel `&dword_13FCF4C` and +496 advance confirmed.
- **WalkAndInvoke @0x5ac738 (UniverseRoot form) — VERIFIED-1:1.** This is the faithful
  reconstruction of the full original (both the non-null sibling walk and the separate
  full-walkMask root-child recursion terminating at the sentinel). `r>=0` modeled as
  `(v11&0x80)==0`.
- **FreeNodeRecursive @0x5efe50 — VERIFIED-1:1.** Drain +60 list caching `next` (ecx)
  before the cellListPool free; recurse the four octant slots (+0..+12); free the cell
  block. Pools 64A7D0 (list) / 64A7CC (cell) mapped to cellListPool/cellBlockPool.
- **AddMeshToCell @0x5efea0 — VERIFIED-1:1.** Gate: drawCell(+492) && drawCell+260 &&
  (+531 bit2); ++cell+48; alloc 8 (zeroed pool); node[0]=obj; append to +60/+64 list.
- **RemoveMeshRecursive @0x5f0a74 — VERIFIED-1:1.** Scan +60 list for obj, unlink
  (fix prev->next/head and +64 tail), --refCount(+48), free node; recurse octants and
  free+null a child whose refCount hit 0; clear +531 bit2; return 1.

### scene_link.cpp

- **LinkIntoScene @0x5b0a20 — VERIFIED-1:1 (with a documented LP64 boundary).**
  make-current-camera leg (nodeType==3 && !currentCamera ⇒ set dword_13FCD1C,
  InvalidateCurrent(0), SetWorldTranslation), the nodeType!=0 prepend-as-scene-head
  splice (+496=sentinel, +500=oldHead, oldHead+496=node, +528|=3, dword_13FD140=node),
  and the `return node` are exact. The original's `*(node+520)=off_649D64` per-node
  universe stamp cannot survive a 4-byte slot under LP64 (64-bit pointer); modeled
  out-of-band on the LiveScene — no read site depends on the per-node copy. BOUNDARY.
- **SetParent @0x5b0b9c (LiveScene form) — VERIFIED-1:1.** Same control flow as the
  trampoline form; the no-parent path returns `(char)LinkIntoScene(...)` (the node
  ptr low byte) — more faithful than the trampoline form. No ODR clash (distinct
  signature; the body reproduces the original 1:1 rather than wrapping the other).

### scene_node.cpp

- **ProcessSceneNode @0x5add1c (append core) — VERIFIED-1:1 for the append output;
  BOUNDARY for two documented items.**
  The two draw-list append loops match the original byte-for-byte for the observable
  draw-list state (poly ptr at entry+4, count, texture frame stamp at tex+84):
    * Software path (byte_649D70!=0): front-facing gate `(i8)flags36<0`; textured ⇒
      sortKey = baseKey + ((tex-1406A84)>>7) (the arithmetic `>>7` is supplied
      pre-computed in texSortId per the parameterization), else sortKey=0.
    * Hardware path (byte_649D70==0): sortKey = (i32)(scale*maxZ), maxZ = max of the
      three vertex z; (int) cast truncates toward zero — matches the original `(int)`.
  BOUNDARY 1 — shade-ramp pointer: the original also writes
  `*(poly+12) = &dword_13DB398[26*(flags36&0x3F)]` in BOTH loops. The reimpl `Polygon`
  does not model offset +12 (it has no field there) and the reimpl rasterizer resolves
  shading via lightIdx/matIndex, so this write has no destination and is intentionally
  omitted; documented in scene_node.h.
  BOUNDARY 2 — hardware depth scale: the original chains three x87 float multiplies
  (flt_13FC774 * flt_628080 * flt_628084 * maxZ); the reimpl folds the three constants
  into a single `depthScale` supplied by the caller, so a chained-x87-vs-prefolded
  rounding difference is possible at the LSB before the truncation. Documented as a
  parameterization in scene_node.h.

### scenegraph.cpp

- **ClassifyBoundingBoxPlanes @0x5ad1f4 — VERIFIED-1:1.** 6-bit outcode with the
  progressive masks 0xDF/0xCF/0xC7/0xC3; OR (v24) and AND (v23) accumulators; final
  `result = ((last_v7 & v23)!=0)<<6 | ((last_v7 | v24) & 0xBF)`; near/far compares
  promote to double (`pz > (double)farZ`); min/max-z loop uses `<`/`<=` exactly as the
  original; running-near=min, running-far=max. The frustum planes/near/far/running
  globals (13DCDA0.. / 13FCAFC / 13FC76C / 13FD168 / 13FCF3C) are parameterized into
  the Frustum / out-pointers; behavior-identical.
- **TransformNodeBoxCorners @0x5f0664 — VERIFIED-1:1.** max=box[4..6]-origin,
  min=box[8..10]-origin; corner 0 = (ax,ay,az) WITH translation row m[12..14], corners
  1..7 WITHOUT translation (engine quirk preserved); matrix indexing m[0/4/8]+m[12]
  (X), m[1/5/9]+m[13] (Y), m[2/6/10]+m[14] (Z); corner order
  0(ax,ay,az)1(bx,ay,az)2(ax,by,az)3(bx,by,az)4(ax,ay,bz)5(bx,ay,bz)6(ax,by,bz)7(bx,by,bz);
  output stride 20 floats (0x50 bytes, confirmed from the unk_ target addresses).
- **CullOctreeAgainstFrustum @0x5f09f0 — FIXED (0x40-path return value).**
  Disasm: on the fully-out 0x40 fast path the leaf-stamp loop
  `for(i=head; i; i=i->next)` drains the iterator to null, so eax==0 at `return (char)i`
  (`0x5f0a23`). The reimpl returned the classification byte (0x40, nonzero) instead.
  Before: `cb.stampLeaf(...);` then fell through to `return (char)code;`.
  After: `cb.stampLeaf(...); return 0;`. The recursive call site (`0x5f0a63`,
  `LOBYTE(i)=recurse`) propagates this; the only non-recursive caller
  (VIBE_GameLogic_RunFrameLoop @0x4c0c87) discards eax, so there is no observable
  top-level change, but the value is now byte-exact. The null-node and recurse-path
  returns were already correct; `kCullMaxDepth` is a documented anti-overflow guard.

## ODR / consistency note
WalkAndInvoke and SetParent each appear in two of the files. In both cases the two
copies have DISTINCT signatures (vtable/UniverseRoot WalkAndInvoke;
trampoline/LiveScene SetParent) and each reproduces the original control flow; there
is no duplicate definition of the same symbol. The UniverseRoot WalkAndInvoke is the
fully-faithful 0x5ac738 reconstruction.

## Fixes (addresses)
- 0x5AEF34 RadixSortDrawList: return 0 for count>1 (was count). scene.cpp.
- 0x5F09F0 CullOctreeAgainstFrustum: return 0 on the 0x40 leaf-stamp path (was the
  classification byte). scenegraph.cpp.

## Tests
Built + ran (all PASS):
- unit: render_scene_test, render_scene_walk_test, scene_link_test, render_mesh_test
  (octree cull), render_pipeline_test, scene_recon_octree_test,
  object_transform_ops_test, wire_scene_bridge_test, render_recon2_test, render_cull_test
- integration: render_cull_itest
- e2e: render_scene_e2e_test, render_scene_walk_e2e_test, scene_link_chain_e2e_test,
  render_cull_e2e_test
Total: 15/15 test executables passed (GUILD_GAME_DIR set; asset-gated suites skip
cleanly when absent).
