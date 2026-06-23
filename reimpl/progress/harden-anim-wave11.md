# Wave-11 hardening — W11-ANIM cluster (anim / skeleton / camera / surface / text)

MCP was DOWN for this wave: NO new 1:1 reconstruction. This pass HARDENS the anim/
render foundational modules with an ASAN+UBSAN build and malformed/degenerate-input
tests, and FIXES the memory-safety / UB it surfaced. Every golden value stays
byte-identical; only out-of-envelope (degenerate/malformed) inputs are made fail-safe.

## Cluster owned

Sources (edited only these + the test files + this doc):
- `src/render/anim.{h,cpp}`, `anim_normals.{h,cpp}`, `anim_relight.{h,cpp}`
- `src/render/anim_recon4_mesh_lru.{h,cpp}` (mesh LRU / evict / shape-register)
- `src/render/animation_mesh.{h,cpp}`, `animation_playback.{h,cpp}`
- `src/render/bone_palette.{h,cpp}`, `bone_sample.{h,cpp}`
- `src/render/skeleton.{h,cpp}`, `skeleton_pose.{h,cpp}`, `skeleton_pose_driver.{h,cpp}`
- `src/render/morph_blend_walk.{h,cpp}`
- `src/render/camera*.{h,cpp}` (camera, camera_recon, camera_recon2, camera_control,
  camera_edge_scroll, camera_update_recon, camera_recon5_flight)
- `src/render/font.{h,cpp}`, `text_raster.{h,cpp}`, `present.{h,cpp}`,
  `surface.{h,cpp}`, `surface_blit.{h,cpp}`, `colorformat.{h,cpp}`

SKIPPED (wave-10 / other clusters): raster/shadow/particle/water/terrain/sky/light/
mesh_normals/sprite/node_lod/texture/env_map_walk/object_light_shade/vertex_lighting.

## ASAN+UBSAN build

```
cmake -S . -B build-asan-w11anim -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
make -C build-asan-w11anim <test targets> -j3
```
(Used a uniquely-named build dir because sibling wave agents were racing on the
shared `build-asan` name and deleting it.)

## FIXES (memory-safety / UB) — all faithful, in-bounds path unchanged

1. **`animation_playback.cpp` `FindFirstActiveBone` (0x5cd184)** — misaligned 8-byte
   load (UBSAN: "load of misaligned address ... requires 8 byte alignment").
   The original reads `a1[87]` / `a1[81]` as 32-bit words (byte offsets 348 / 324).
   The reimpl read `a1[87]` via `*(const void* const*)` — an 8-byte host-pointer load
   at a 4-byte-aligned slot, both misaligned AND over-reading the 4-byte field.
   FIX: `memcpy` a 4-byte `u32` and test `== 0` (the binary's `if (!a1[87])` is a
   32-bit nonzero test); `a1[81]` likewise via `memcpy`. Confirmed by UBSAN.

2. **`bone_sample.cpp` `CPtrAt` (used by GetBonePosition 0x5cc850 /
   GetBoneFramePose 0x5ccea0)** — misaligned 8-byte pointer load. The chained
   indirection `*(*(bone+104)+348)` reads pointer slots at byte offsets 104 and 348;
   348 % 8 == 4, so `*(const void* const*)(base+348)` is a misaligned, aliasing-
   violating load on a 64-bit host. FIX: `memcpy` the stored pointer. Same value,
   no UB. (The shared `mesh_transform_test` that drives these previously tripped
   UBSAN; the dedicated `bone_sample_test` now pins it too.)

3. **`skeleton_pose.cpp` `AdvanceTrackPhase` (the simplified standalone advance,
   sibling of the 0x5cd1d8 driver)** — unbounded `durations[st.fromFrame]` read.
   FIX (faithful fail-safe): early-out `if (frameCount <= 0 || !durations) return 0;`
   (a track with no frames has nothing to advance — the engine only reaches this
   for an active header with frameCount > 0), and clamp the per-iteration duration
   index to `[0, frameCount)` (mirrors `skeleton_pose_driver`'s own `dur()` helper,
   which already bounds the same read). Valid input is unaffected.

## DEGENERATE / ENVELOPE items — DOCUMENTED, not changed (need MCP to alter)

- **`camera.cpp` `ProjectPoint` (0x407428)** zero view depth (`camera[4] == 0`):
  `inv = 1.0 / camera[4]` is `+inf`, exactly as the original's x87 divide. The
  binary then FISTPs the infinite product -> the x87 integer-indefinite 0x80000000.
  That is a well-defined result of the faulting instruction = the engine's envelope.
  The C++ `(i32)` cast of a non-finite double is UB, so the camera_project tests do
  NOT drive `camera[4] == 0` under UBSAN. Pinning the exact 0x80000000 result (and
  whether the engine ever feeds a zero depth) is a 1:1 question — **BEHAVIORAL,
  needs MCP**. The valid (non-zero depth) path is fully tested.

- **`camera_recon.cpp` `Camera_ComputeZoomScale` (0x4c20ec)** divides by `area`;
  a zero area is the same x87-divide envelope as above — not exercised, not changed.

- **`anim_recon4_mesh_lru.cpp` `AssignSubMeshBones` (0x5cbfc0)** — within one bone-
  name iteration the inner free-slot cursor `n = v4++` is written to `slot+112+n`
  for EACH matching child, and the `while (v4 < 4)` bound is only checked at the loop
  bottom. If > 4 children match a single bone name, `n` can exceed 3, writing past
  the 4-byte bone-index field (+112..+115). Whether the binary clamps this is a 1:1
  question I cannot resolve with MCP down — **BEHAVIORAL, needs MCP**. The slot is a
  large record (no heap OOB in tests); the existing test uses <= 4 matches. NOT
  changed (a speculative `n < 4` guard could break 1:1).

- **`bone_palette.cpp` `ComputeBoneMatrices` (0x5cc0d0)** outer loop bound is
  `g < groupCount && g < 4 + 4` (8), while the header documents "up to 4 groups".
  The 4-record palette cap (`recCount >= 4 -> continue`) already prevents any OOB on
  the 4-record scratch regardless of group count, so this is not a safety bug; the
  exact group-iteration count is a **BEHAVIORAL** question for MCP. NOT changed.

- **`surface.cpp` 15bpp system-memory surface (Create 0x42311c / SetPixelRgb
  0x423e5c)** — a size/stride INCONSISTENCY surfaced by ASAN. `SurfaceCreate`
  computes `bytespp = bpp>>3`, so a 15bpp surface allocates `width` bytes per row
  (1 byte/px), but `SurfaceSetPixelRgb`'s 15bpp branch stores a `u16` (2 bytes/px),
  overrunning the buffer (heap-buffer-overflow at surface.cpp:90 on the last pixel).
  This is faithful to the original's own `pitch = (bpp>>3)*width`, which would
  corrupt identically — the engine only uses 15bpp as a DDraw DISPLAY format, never
  a sysmem surface, so this config is out of envelope. NOT changed (matching the
  binary's stride math is the 1:1 contract); flagged here for the surface owner /
  MCP to confirm. The W11 test uses an odd-WIDTH 16bpp surface instead.

- **`text_raster.cpp` `DrawText` (0x434E18)** walks the input string until a NUL
  byte (`while (next)`), exactly as the binary. A genuinely NUL-less buffer would
  over-read — but the engine only ever passes NUL-terminated C strings, so this is
  the envelope. Tests feed terminated strings (empty / spaces / run-past-edge).

## NON-OWNED finding (for that cluster's owner — NOT edited per ownership rule)

- **`src/render/mesh_transform.cpp:18` `PtrAt`** has the SAME misaligned 8-byte
  pointer-load UB I fixed in `bone_sample.cpp`/`animation_playback.cpp`:
  `*reinterpret_cast<void**>(base + off)` over a 4-byte-aligned 32-bit-pointer slot.
  UBSAN: "load of misaligned address ... requires 8 byte alignment" (tripped by the
  shared `mesh_transform_test`, which is also not in my cluster). Same fix applies:
  `memcpy` the stored pointer. Flagged here for the mesh_transform owner; I did NOT
  edit it (it is outside the W11-ANIM cluster). My `bone_sample.cpp` fix resolved the
  bone-sample portion of that test; the remaining trip is entirely in mesh_transform.

## TESTS ADDED (all ASAN+UBSAN exercised)

- `text_raster_test.cpp` (+6): font table never exceeds the 91-glyph atlas; draw all
  256 ASCII codes in-bounds; glyph exactly fills the framebuffer; DrawText empty /
  spaces-only / runs-past-right-edge (clipped).
- `animation_playback_test.cpp` (+5): FindFirstActiveBone with a 4-byte-only frames
  slot (UBSAN-fix regression), null frames slot + bogus count, zero count;
  SeekToFrame with no header, frame past the track (clamps).
- `morph_blend_walk_test.cpp` (+5): null/zero/negative guards; null-points layer
  skipped; zero-weight layer skipped; exact-sized point buffer read in-bounds;
  mismatched (gated) second layer.
- `render_skeleton_pose_test.cpp` (+7): ComputeBoneMatrices zero-groups / all-
  inactive / record-cap (5+ names -> 4); AdvanceTrackPhase zero-frames / null-
  durations / out-of-range fromFrame / zero-duration terminates.
- `render_surface_test.cpp` (+7): HLine/line/rect past edge clipped; 1x1 surface;
  15bpp odd pitch; clone tiny; null-surface guards.
- `anim_recon4_mesh_lru_test.cpp` (+4): mesh-size null guards; evict empty list;
  evict-all-busy at capacity (evict-during-use -> 0, nothing freed); release with
  zero sub-count.
- `bone_sample_test.cpp` (NEW, 4): position/pose value goldens + misaligned-slot
  indirection (UBSAN-fix regression) + frame-0 lower bound + high-frame in-bounds.
- `camera_project_test.cpp` (NEW, 6): ProjectPoint unit/scaled depth, negative trunc-
  toward-zero, large-finite in-range; ProjectFramePoint off-map early-out / on-map.

## STATUS

All 18 owned-cluster test targets build clean under
`-fsanitize=address,undefined -fno-sanitize-recover=all` and pass with 0 failures:

| target | checks |
|---|---|
| text_raster_test | 1620 |
| animation_playback_test | 55 |
| morph_blend_walk_test | 20 |
| render_skeleton_pose_test | 55 |
| render_surface_test | 212 |
| anim_recon4_mesh_lru_test | 125 |
| bone_sample_test (NEW) | 12 |
| camera_project_test (NEW) | 14 |
| skeleton_pose_driver_test | 39 |
| render_skeleton_test | 72 |
| render_camera_test | 117 |
| surface_present_test | 70 |
| camera_recon_test | 40 |
| camera_recon2_test | 34 |
| camera_edge_scroll_test | 75 |
| camera_update_recon_test | 71 |
| camera_controls_test | 34 |
| camera_recon5_flight_test | 9 |

Normal (non-sanitized) `build/` confirmed green for the touched targets — all golden
values byte-identical. 3 UB fixes landed (animation_playback, bone_sample,
skeleton_pose); 1 non-owned UB (mesh_transform.cpp:18) + several engine-envelope
degenerate cases (camera zero-depth, 15bpp sysmem surface, AssignSubMeshBones >4
matches, ComputeBoneMatrices group bound) documented above for MCP / their owners.

