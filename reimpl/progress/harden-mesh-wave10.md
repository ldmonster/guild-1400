# Wave-10 hardening — W10-MESH cluster (MCP-free memory-safety + edge pass)

Scope: HARDEN the wave-6/7/8 reconstructions in this cluster for memory safety
(OOB/UB/null) and add degenerate/edge-case coverage. NO new 1:1 reconstruction
(IDA MCP down). All existing golden values kept byte-identical.

Owned source: `src/render/{mesh_normals,scene_lights,reflective_nodes,sprite_scale,
node_lod,vegetation_anim,cloth_anim}.{h,cpp}`
Owned tests: `tests/unit/{mesh_normals_test,scene_lights_test,reflective_nodes_test,
render_billboard_project_test,render_sprite_scale_test,node_lod_select_test,
vegetation_anim_test,cloth_anim_test}.cpp`

## Build / run

ASAN+UBSAN (no-recover):
```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan --target <the 8 targets> -j$(nproc)
```

Result (all 8, ASAN+UBSAN, zero sanitizer hits):

| target                         | checks | failures |
|--------------------------------|-------:|---------:|
| mesh_normals_test              |   145  |    0     |
| scene_lights_test              |    38  |    0     |
| reflective_nodes_test          |  1074  |    0     |
| render_billboard_project_test  |    71  |    0     |
| render_sprite_scale_test       |   305  |    0     |
| node_lod_select_test           |   143  |    0     |
| vegetation_anim_test           |    42  |    0     |
| cloth_anim_test                |    84  |    0     |

Normal (non-ASAN) `build/` re-verified green for all 8 targets (same check counts).

## Memory-safety / UB fixes (faithful — in-bounds path byte-identical)

### 1. `mesh_normals.cpp` GenerateVertexNormals — OOB read of `verts[]`
- **Bug:** pass 1 dereferenced `verts[tri.vtx[0/1/2]].pos` with NO bounds check on
  the triangle index. A corrupt/out-of-range poly index reads the source-vertex
  array out of bounds (OOB read).
- **Fix:** skip any triangle whose index >= vertexCount. `faceScratch` is
  value-initialised to {0,0,0}, so a skipped triangle keeps a zero face normal —
  which the pass-2 `VectorNormalize` collapses to (0,0,0), exactly the engine's
  zero-length behavior. For a well-formed .BGF (all indices in range) the guard
  never fires; the in-bounds output is byte-identical.
- **1:1 note:** the original `VIBE_Mesh_ComputeVertexNormals @0x5D1A6C` would also
  read OOB on a bad index, but it never reaches that with a valid mesh. The guard
  is a memory-safety floor, not an observable behavior change. (No MCP to confirm
  whether the original has its own bound; flagged as a memory-safety guard, not a
  behavioral change.)
- **Pinned by:** `MeshNormals.HardenOutOfRangeTriangleIndexGuard`,
  `HardenAllTrianglesOutOfRange`.

### 2. `node_lod.cpp` SelectLodFrame — null `frames` deref
- **Bug:** with `drawDataReady && lodCount>0`, `obj.frames[index]` was
  dereferenced even if `frames == nullptr`.
- **Fix:** added `|| !obj.frames` to the early-out. In the engine the frames array
  lives at drawData+244, so `drawDataReady && lodCount>0` implies a non-null
  frames pointer; the guard never fires for a well-formed object and the in-bounds
  path is identical.
- **Pinned by:** `NodeLodSelect.HardenNullFramesGuard`.

### 3. `sprite_scale.cpp` RleScaledImpl — shift-count UB (`>>` count >= 32)
- **Bug:** `phase = (skip >> (phase + 1)) % scale`. `phase < scale` and `scale`
  can be up to 255, so `phase + 1` can exceed 31. A C++ `u32 >> count` with
  `count >= 32` is UNDEFINED BEHAVIOR.
- **Fix:** mask the shift count to 5 bits: `skip >> ((phase + 1) & 31u)`. This is
  the faithful translation of the x86 `shr r/m32, cl` instruction the binary
  executes (x86 masks CL to 5 bits). For the common small-scale case (`phase+1 <
  32`) the result is byte-identical; for large scale it now matches the actual x86
  semantics instead of being UB.
- **1:1 note:** the masked shift IS what the original 32-bit x86 instruction does,
  so this is a faithful fix, not a behavior change. (Hex-Rays often renders the
  mask implicitly; could not re-confirm against the decompile with MCP down, but
  the x86 ISA semantics are unambiguous.)
- **Pinned by:** `RenderSpriteScale.HardenLargeScaleShiftNoUB`,
  `HardenMaxScaleLightTableNoUB`.

## Edge / degenerate tests added (per brief checklist)

- **GenerateVertexNormals:** 0-triangle, 1-vertex, degenerate (collinear),
  out-of-range index, all-OOB, faceNormalsOut on empty/degenerate.
- **BindInstanceNormals / FlattenInstanceNormals:** count==size bounds, null/zero
  guards, empty-vector overload.
- **scene_lights CullForObject / CollectObjectLights:** 0 lights, 1024 lights
  (capacity stress), object AT light origin (dist 0 -> well-defined sqrt), zero-
  range-at-zero-dist culled by `<=`.
- **billboard project:** depth 0 (invZ = 1/0 = +inf, faithful fdiv), behind camera
  (cz<0), depth-fade at zero depth (distSq 0 -> full alpha) and far (clamps to 0),
  tiny depth / max scale (huge finite/inf projected coord), zero-count no-op
  across all three arms + quad pass + tint broadcast.
- **node_lod SelectLodFrame:** distance 0, large distance (clamps in-bounds vs
  the verbatim oracle), all forced-flag (0x30) bit combos stay in-bounds, null
  frames guard.
- **BuildVegetationCache:** count==0 (hooks still called, no element write),
  null buffer + null hooks no-op, single-vertex lit arm.
- **RefreshFlagAnimation:** 0 nodes (gate passes, loop skips), null `produced`
  write-back guard. **CollectFlagNodes:** the 32-node cap (`slot < outCapacity`
  prevents OOB write past kMaxFlagNodes; count still advances past the cap), null
  out buffer, null node name.
- **reflective detect:** null/empty texture array, count 0, all-null records,
  "null material" (present==false -> never reflective, no deref), full-byte-range
  ApplyReflectiveBit non-bit5 preservation.

## Behavioral ambiguities flagged for MCP (none changed)

- None required a behavioral change. All three fixes are memory-safety/UB floors
  that leave the documented in-bounds behavior byte-identical. The only items that
  *touch* behavior on out-of-domain input (OOB triangle index; shift count >= 32)
  are noted above as needing MCP re-confirmation against the decompile, but the
  chosen behavior (skip OOB triangle; x86-mask the shift) is the conservative,
  in-bounds-preserving choice and matches the x86 ISA where applicable.

## Not changed (contract, not bugs)

- `BillboardQuadVisibilityPass` derefs `q.v0/v1/v2` when bit7 set & bit4 clear; the
  engine always supplies valid vertex pointers (no null in the live call tree). Not
  guarded (would be speculative; no OOB for valid data).
- `BillboardVertex 1.0f/cz` at cz==0 yields IEEE +inf — this is the original's
  `fdiv` behavior, faithful (tested, not "fixed").
- `ShapeBlitScaled16` with xStep==0 loops as the original does — not a memory bug.
