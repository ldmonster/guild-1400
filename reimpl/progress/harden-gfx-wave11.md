# Wave-11 hardening — GFX/scene/shape/picture/BMP parser cluster (W11-GFX)

MCP was DOWN for this wave: **no new 1:1 reconstruction**, hardening only. The goal
was ASAN+UBSAN coverage + malformed/truncated/oversized-input tests over the
foundational asset-ingest code in this cluster, and fixing every memory-safety /
UB / DoS issue that a degenerate asset could trigger — without changing any
observable output on valid input (goldens stay byte-identical).

## Owned cluster
`src/render/`: `gfx_archive`, `scene_load`, `scene`, `scenegraph`, `scene_node`,
`scene_transform`, `scene_link`, `shapebank`, `shape`, `shape_blit`, `shapeanim`,
`picture_io`, `picture_recon_bmp`, `bmp` (`.h`/`.cpp`).
Tests: `gfx_archive_test`, `render_scene_load_test`, `picture_recon_bmp_test`,
`render_bmp_test` (new), plus the existing `render_scene_test`, `render_picture_test`,
`render_shape_blit_test`, `scene_transform_test`, `scene_link_test`,
`shape_convert16_test` (shapebank coverage; file owned elsewhere — not edited).

Skipped as out-of-cluster / other owner: `scene_floor` (water agent), `scene_view`
(play), `shape_convert16.cpp` (its own malformed test already exists), and
`mirror_scenegraph_test` (hardened by the wave-10 light agent).

## Build / sanitizer setup
Dedicated ASAN+UBSAN build dir (the shared `build-asan` is contended by other
wave-11 agents, so a private one avoids clashes):

    cmake -S . -B build-w11gfx -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
    cmake --build build-w11gfx --target guild -j1      # -j1: avoids a .o.d dir race
    cmake --build build-w11gfx --target <cluster tests> -j$(nproc)

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 when running.

NOTE: parallel `guild` lib builds in a fresh tree intermittently fail to create
the per-object `.o.d` dependency directory (race), and an interrupted parallel
build can leave a truncated `.o` (ranlib "file truncated"). Build the lib with
`-j1` once; tests can then build in parallel safely.

## Fixes (all memory-safety / DoS; valid-input behavior unchanged)

1. **`scene_load.cpp` — `ReadObjectRecord` unbounded recursion (stack overflow).**
   The child/sibling record recursion had no depth limit; a malformed/cyclic
   `.ed3` with a long run of "has child" presence bytes recurses one native frame
   per byte and overflows the stack (the bytes are untrusted asset input). Added
   an internal depth-tracked `ReadObjectRecordDepth` capped at
   `kSceneMaxNodeDepth = 4096` (see `scene_load.h`); the public `ReadObjectRecord`
   signature is unchanged. Valid scenes nest far below the cap, so the in-bounds
   decode is byte-identical. Pinned by `SceneLoadHarden.OverDeepChildChainDepthGuard`.

2. **`scene_load.cpp` — `ReadObjectList` untrusted `reserve(count)` (OOM/DoS).**
   The object-count dword fed straight into `std::vector::reserve`; a `0xFFFFFFFF`
   count tried to reserve ~0x3FFFFFFFC0 bytes (ASAN OOM abort). The read loop
   already stops at end-of-buffer and every top-level record costs >=1 byte, so
   the real count can never exceed the bytes left. Capped the reservation to
   `min(count, reader.remaining())` (added `SceneReader::remaining()`). The
   original used raw recursion with no pre-reserve, so this is a recon artifact;
   the decoded result is identical on valid input. Pinned by
   `SceneLoadHarden.ObjectCountTooLargeNoOverRead`.

3. **`bmp.cpp` — `BmpLoadBuffer` degenerate dims + oversized palette.**
   * `std::abs(height)` was UB for `height == INT_MIN`; a negative/zero width and
     a `width*absH` overflow drove a wild `std::vector` size and OOB indexing.
     Added an up-front guard: reject `width <= 0`, `height == 0`, `height ==
     INT_MIN`, and `width*absH > 1<<28`; compute `absH` without `std::abs`.
   * `biClrUsed` (untrusted) drove the palette unpack loop writing into the
     256-entry (768-byte) stack array `srcPal`; a huge/negative value caused a
     stack-buffer-overflow. Clamped `npal` to `[0,256]`. A valid 8bpp BMP palette
     is always <=256 entries, so valid files are unaffected. Pinned by
     `BmpHarden.NegativeAndZeroDimensions / OversizedDimensionsRejected /
     OversizedPaletteClamped`.

4. **`picture_recon_bmp.cpp` — `PictureLoadBmpPalette` oversized `biClrUsed`.**
   The loop wrote `pal + 4*i` for `i < clrUsed` into the caller's fixed 256-entry
   palette and allocated `4*clrUsed` quads; a malformed `biClrUsed` overran both.
   Clamped `clrUsed` to 256 (the physical 8bpp palette maximum). Valid files
   unaffected. Pinned by `PictureReconBmpHarden.PaletteOversizedClrUsedClamped /
   PaletteNegativeClrUsedClamped`.

5. **`scenegraph.cpp` — `CullOctreeAgainstFrustum` and `scene.cpp`
   `WalkAndInvoke` recursion depth guards.** Both recurse one frame per child
   level over an in-memory node/octree tree; a corrupt or cyclic tree (a child
   pointer re-entering an ancestor) overflows the stack. Added internal
   depth-tracked helpers (`kCullMaxDepth = 256`, `kWalkMaxDepth = 4096`) that stop
   the descent safely; real trees are shallow so traversal/output is unchanged on
   valid data. (These trees are engine-built, not parsed directly from a file, so
   this is defensive — no malformed-file test drives them, but the guard removes
   the crash class the brief called out for the scene-node recursion.)

## Engine-envelope items (documented, NOT changed — faithful 1:1)

* **`shape.cpp` `ShapeDecodeRle` (0x5D70CC) / `shape_blit.cpp` `ShapeBlitColored16`
  (0x5D7164).** These are byte-faithful ports of the original blit loops, which
  perform **no clipping** and take a raw `shape` pointer + an unsized
  `BlitTarget16` — the caller pre-clips and sizes the destination (documented in
  `shape.h`). A malformed shape blob fed directly here will read/write past the
  caller's buffer exactly as the original would; adding a length guard would
  change the faithful signature/contract. The file-format-level decoder
  (`gfx_archive.cpp DecodeShapeBlob`) is the properly length-checked entry and is
  where the malformed-SHAPBANK tests live. **Behavioral guard for these blits is a
  1:1 question — needs MCP** to confirm whether the original bounded the run/skip.

* **`picture_recon_bmp.cpp` `PictureLoadBmpRle` / `PictureLoadBmpUncompressed` /
  `PictureLoadBmp24_226ec`, and `picture_io.cpp` blit/load helpers.** These write
  into a caller-supplied `dst`/surface sized from `ReadBmpDimensions`/`fbWidth`
  (the engine's allocate-then-fill contract). The RLE `dst` writes match the
  original's unchecked `SetGrayColorThunk(...)` spans. Not changed (caller
  contract); the self-contained header readers + palette loader ARE hardened/tested.

* **`shapebank.cpp` (0x5D8330 / 0x5D843C / 0x5D84F4).** Operates in place on an
  engine-built bank via raw offsets; a negative/over-count `index` is a caller
  bug, not untrusted file input. The on-disk SHAPBANK malformed cases (bad record
  offset/size, shape past buffer) are covered through `gfx_archive` (the loader
  that ingests the bytes). Malformed-bank coverage also already exists in
  `shape_convert16_test` (owned by another agent).

## Tests added
* `gfx_archive_test.cpp` — suite `GfxArchiveHarden`: empty/1-byte buffers, absurd
  record count, record dataOffset past EOF, declared shapeCount vs blob mismatch,
  offset-table entry past blob, zero/oversized shape dims, row-table offset past
  shape, FULL-bitmap pixel payload truncated.
* `render_scene_load_test.cpp` — suite `SceneLoadHarden`: empty/tiny buffers,
  header truncated mid-field, NUL-less ReadString, object count too large,
  truncated mid-object record, over-deep child chain (depth guard).
* `render_bmp_test.cpp` (NEW) — `Bmp` golden round-trips (Save24/SaveIndexed ->
  LoadBuffer, ReadHeaderInfo) + suite `BmpHarden`: empty/truncated header, bad
  planes/bpp/compression, negative/zero/oversized dims, oversized/negative
  palette, truncated 24-bit data, degenerate RLE8 streams.
* `picture_recon_bmp_test.cpp` — suite `PictureReconBmpHarden`: empty files,
  header shorter than the info block, oversized/negative `biClrUsed` palette,
  bad-field rejection.

## Status
All cluster ASAN+UBSAN test targets build and pass clean (no `runtime error`, no
AddressSanitizer report, no leaks). Goldens unchanged. The two recon-artifact DoS
issues (#1 #2) and the three OOB issues (#3 #4, plus the depth guards #5) are
fixed and pinned. Normal `build/` stays green.
