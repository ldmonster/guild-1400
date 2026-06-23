# Wave-11 hardening — mesh/anim asset loaders (W11-LOADERS)

MCP DOWN → hardening only (no new 1:1 reconstruction). Memory-safety/UB fixes to the
mesh & animation asset parsers, plus malformed/truncated/oversized/empty-input tests
driven under ASAN+UBSAN. Every valid-asset path is byte-identical; every fix is faithful
(the original engine streamed from a finite file and never ran off its buffer — a
truncated/malformed asset fails safe instead of corrupting memory).

## Cluster (owned)
`src/render/`: `bgf_loader`, `agf_loader`, `agf_anim`, `agf_postprocess`, `mesh_load`,
`mesh_asset`, `model_io` (render, not io — `src/io/model_io` does not exist),
`modelio_recon`, `animation_decode`, `anim_load`.
Tests: `agf_loader_test`, `agf_anim_test`, `agf_postprocess_test`, `render_mesh_load_test`,
`render_mesh_asset_test`, `modelio_recon_test`, new `render_model_io_test`,
+ the `*_e2e` / `*_itest` variants.

## Build / method
```
cmake -S . -B build-asan-w11 -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-w11 --target guild <test targets> -j$(nproc)
```
(Used a uniquely-named `build-asan-w11` dir because a sibling wave-11 agent was
recreating the shared `build-asan`.)

All cluster unit + e2e + integration tests pass clean under ASAN+UBSAN+LSan, AND in the
normal `build/`. Adjacent mesh/anim tests (mesh_recon3_geometry, mesh_attach_textures,
render_mesh, mesh_load_or_find, mesh_normals, anim_normals) also pass clean under ASAN.

## Bugs FIXED (real OOB / UB, caught by the new tests)

1. **`mesh_load.cpp` `LoadBgfPostProcess` — heap OOB on out-of-range material index.**
   `used[mi]`, `placed[mi]`, `tmp[mi]`, `reordered[mi]` were indexed by a polygon's
   `matIndex` with no upper bound. A malformed/oversized material index walked past the
   `matCount`-sized flag arrays / material vectors (heap OOB write **and** a SEGV in the
   UV-bake — reproduced: `SEGV mesh_load.cpp:244`). Added `mi >= 0 && mi < matCount`
   guards at the three index sites (pass-2 `used`, the reorder `placed`/`tmp`, and the
   UV-bake `reordered`). The sibling `agf_postprocess.cpp` already had these guards; this
   brings the older `mesh_load.cpp` path in line. Valid assets keep every `matIndex` in
   `[0, matCount)`, so the in-bounds path is byte-identical.
   Test: `render_mesh_asset_test :: PostProcessOutOfRangeMaterialIndex`.

2. **`anim_load.cpp` `LoadBinaryAnimation` — signed-int overflow (UB) + runaway loop on
   an oversized per-frame vertex count.**
   - The point read loop computed `int base = (accumulated + j) * 3`; an oversized
     `vertexCount` (~2e9) overflowed the `int` (`UBSAN: signed integer overflow`). It also
     spun the loop billions of times because `Reader::Vec3` ignored its short-read result.
     Fix: `Vec3` now returns whether it fully read; the loop **breaks at EOF** (the same
     truncation the engine's per-field reader hit) and the offset is computed in `size_t`.
     `accumulated += read` (only the points actually read) — identical on valid input.
   - The post-load point passes guarded with `(int)pts.size() >= nPts * 3`; the `nPts * 3`
     `int` multiply overflowed before the comparison (`UBSAN: 1000000000 * 3`). Fix:
     clamp `nPts` non-negative and compare against a `size_t needPts = (size_t)nPts*3`.
     Tests: `agf_anim_test :: OversizedVertexCount`, `OversizedFrameCount`,
     `NegativeFrameCount`, `TruncatedMidFrame`.

## Bugs FIXED (heap exhaustion / bad_alloc DoS on a declared-too-large count)

These turn a would-be giant `std::vector::assign` (driven by a malformed count read
straight from the buffer) into the same clean parse failure the per-record reads already
produce. A valid file always has the backing bytes, so no real asset is rejected.

3. **`bgf_loader.cpp` `LoadFastChunk`** — bound `vertexCount`/`polyCount` by `remain/24`
   and `remain/12`, `materialCount` by `remain/9`, `dummyCount` by `remain/25` before the
   `assign`s. Also closes the `vertexCount + 8` u32 **overflow** (a near-`UINT_MAX` count
   would wrap the +8 slack to a tiny allocation, then `BuildParsedFromBgf` reading
   `vertexCount` records would OOB-read).
   Tests: `render_mesh_asset_test :: FastChunkVertexCountTooLarge`, `…PolyCountTooLarge`,
   `…VertexCountOverflowGuard`, `…TruncatedMidVertex`, `…UnterminatedMaterialName`,
   `…EmptyAndTiny`.

4. **`model_io.cpp` `LoadSyntheticModel`** — bound `vcount` by `remain/21` and `pcount` by
   `remain/26` before allocating. (The reader already bounds-checked indices and reads;
   this only prevents the up-front giant alloc.)
   Tests: new `render_model_io_test` (empty/tiny, bad magic, oversized v/p count,
   truncated mid-vertex, out-of-range poly index).

5. **`anim_load.cpp`** — bound `frameCount` by `size` before the `192*frameCount`-shaped
   allocation; clamp the per-frame point alloc `n` to `[0, size]`; stop the uv-face skip
   loop at EOF (`SkipVec3` now returns false on a short read) so an oversized `nf` cannot
   spin to no effect.

## Tests ADDED (all under ASAN+UBSAN)
- `render_mesh_asset_test.cpp`: 8 malformed fast-chunk/.BGF cases (0-byte, header-only,
  oversized vertex/poly counts, +8 overflow, truncated mid-vertex, NUL-less material name,
  OOB poly index → `BuildGeometry` reject, OOB material index → post-process safe).
- `agf_loader_test.cpp`: 6 malformed AGF cases (empty/1-byte, header-only, truncated
  point block, oversized point count, NUL-less material name, OOB poly index → normals/
  bounds safe).
- `agf_anim_test.cpp`: 5 malformed .baf cases (oversized/negative frame count, oversized
  vertex count, truncated mid-frame, bad magic guard).
- `render_model_io_test.cpp` (new file): 7 cases for `LoadSyntheticModel`.

## Items NOT changed — flagged BEHAVIORAL / engine-envelope (need MCP)

- **`animation_decode.cpp`** blitters (`FrameDataInterpolate`, `FrameTable_*`,
  `FrameDataProcess`, `AnimationBasic`) take a **raw `frame`/`bank` pointer with no buffer
  size** and index it by on-frame offsets (`bank+0x2A`, `bank + 4*n + 0x45`, the per-row
  RLE offset table, `skipBytes`, the `dest` surface). This is 1:1 with the original, which
  trusts the in-memory frame record built by the asset pipeline. There is no size parameter
  to bound against without changing the signature/contract. **Not a parser of untrusted
  file bytes** — these consume an already-decoded in-RAM record. Left as the engine's
  envelope; revisit with MCP if a faithful bounds field is recovered.
- **`modelio_recon.cpp`** `ModelComputeNormals` / `ModelComputeBounds` index
  `verts[face.v*]` and write 8 corner verts at `verts[vertCount + c]` with no validation —
  faithful to the engine, which relies on its loader having validated indices and allocated
  the +8 slack. The owning loader (`LoadFastChunk`) provides the +8 slack and (now) bounds
  the indices it reads; these compute passes are post-load and trust that contract.

## Non-owned issue observed (for that owner — NOT edited)
- During the ASAN build, `src/net/transport.h` momentarily failed to compile
  (`'rx_cap_' was not declared in this scope` at line 112 while line 151 declares it) —
  a transient half-written file from a concurrent net/IO wave-11 agent's edit. It compiled
  cleanly on retry. No action needed from this cluster; noted for the net/IO owner in case
  it persists.
