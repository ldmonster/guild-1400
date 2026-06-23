# Hardening pass — render scene I/O (scene_drawlist / scene_floor / scene_load)

Module files (all under `/home/cnupt/work/reverse/guild-1400/reimpl/src/render/`):
- `scene_drawlist.cpp`
- `scene_floor.cpp` (+ `scene_floor.h`)
- `scene_load.cpp` (+ `scene_load.h`)

Method: decompile + disasm each provenance'd function via IDA MCP (module
gilde.exe), diff line-for-line against the reconstruction, verify every
version-gate immediate (`cmp`), stream read order/count, and float bit-pattern
(`get_int`). DISASM is authoritative over Hex-Rays.

---

## scene_load.cpp

### ParseSceneHeader @0x5e7e38 (header portion) — VERIFIED-1:1
Tag gate confirmed against disasm: `HIWORD==0x3A6C0000 && tag>=0x3A6C0001`
(dec 980156416 / 980156417), "too old" reject `< 0x3A6C000B` (980156427).
Stream-read order verified verbatim: tag, camName, ambient(0x64A070),
camPos(vec3); then **camTarget (>=0x3A6C00B5 / 980156597) BEFORE the fog block**;
fog block (>=0x3A6C00B3 / 980156595): camFlag read only when >=0x3A6C00B7
(980156599), then fogColor, fogNear, fogFar; then light-init memset; then light
rig (>=0x3A6C00A2 / 980156578). Light count ladder: `>=0x3A6C00BA->7`,
`>=0x3A6C00A5->6`, else 4 — matches `SceneLightCount`. Per-light: pos always;
then if `>=0x3A6C00B3`: color when `>=0x3A6C00B5`, plus 6 keyframes of
(id dword, a float, b float). All offsets/order match.

### ReadObjectRecord @0x5e67c8 (framing) — FIXED
Divergence found vs 0x5e67c8 disasm: when the leading presence byte (v122/v123)
is **zero**, the original does NOT read name/body but DOES spawn a no-stream-read
placeholder ("STRANGEFUCK", spawn @0x5e7839, sets v122=1) and **falls through to
the same child-presence (0x5e6b3e) and sibling-presence (0x5e6b9c) reads** and
the event-binding read (0x5e6c15). The prior recon returned early on present==0
(`if (!rec.present) return rec;`), which would **desync the stream cursor** for
any object list containing an empty node (the next bytes — child/sibling flags —
would be mis-attributed).

- Before: `present` byte read, then `if (!rec.present) return rec;` (skips
  child+sibling reads entirely).
- After: body reads are wrapped in `if (rec.present) { ... }`; the child- and
  sibling-presence `ReadByte`s (and their recursion) now execute for present==0
  too, exactly as the STRANGEFUCK fall-through path does. EOF-safe (the reader's
  over-read returns 0 + sets eof).
- Evidence: 0x5e67c8 `if (v123[0]) {…spawn+switch…} else {STRANGEFUCK spawn}`
  then unconditional `VIBE_Bio_ReadByte(v118,v123)` @0x5e6b3e (child),
  `VIBE_Bio_ReadByte(v118,v124)` @0x5e6b9c (sibling). The STRANGEFUCK spawn
  (0x5e7839) issues no Bio_* reads. Confirmed identical to the proven-correct
  `SkipObject` reader in scene_floor.cpp, which already reads child/sibling on
  present==0.
- No golden changed (existing tests always used present=1; the fix only affects
  the previously-unmodelled present==0 framing). Real-asset e2e (floorwater over
  shipped scenes, drawlist-equiv) still pass.

### ReadObjectList @0x5e7e38 (object-list portion) — VERIFIED-1:1
count dword, then `count` records (EOF-gated), then floor-flag byte
(0x5e81a2 → if set, LoadFloorRegions). The wave-11 reserve cap is a recon
artifact bound (no behavioral change on valid input). Matches.

---

## scene_floor.cpp

### LoadFloorRegions @0x5e78a8 (ParseFloorRegions) — VERIFIED-1:1
Every version gate confirmed against the disasm `cmp` immediates:
- `< 0x3A6C0009` -> "Incompatible Floor-Versions", return (0x5e78bf).
- name; if name[0] && `>=0x3A6C00AD`: gridN dword, ArrayQuick(N) heights;
  `>=0x3A6C00BB` -> ArrayQuick(4N) lightOffsets (0x5e78fd/0x5e7925).
- `>=0x3A6C00AA`: water flag; if set, count dword; if count>0: `>=0x3A6C00AD`
  ArrayQuick(N) waterHeights, then count water records (0x5e7945..0x5e79c2).
- textureName; `>=0x3A6C00AD && name[0]` -> ArrayQuick(N) textureGrid.
- `<=0x3A6C000F` -> 5 legacy strings (0x5e7c79, `cmp 0x3A6C000F; ja`).
- typeNames: **8 when `>=0x3A6C00B0` (end base+0x200), 6 otherwise
  (loc_5E7DDF, end base+0x180)** — both string loops confirmed by their
  `lea ecx,[edi+0x200]` / `[edi+0x180]` bounds.
- cellScale, heightScale (two Bio_ReadDword, treated as floats).
- `<=0x3A6C000F` -> 256 legacy strings.
- LoadFromHeightmap, then `>=0x3A6C000E` -> origin vec3 OVERWRITES
  floor+144/+148/+152 (`mov [ecx+90h/94h/98h]`). Order/offsets all match.

### ReadArrayQuick @0x5dcca0 — VERIFIED-1:1
First dword = elemSize, second = count; gate `expectedCount == count`; payload
read = count*elemSize (alloc `*v9 * v8`, read `a2 * v8` with `a2==count`); else
Vfs_Seek forward count*elemSize and null. Matches.

### ReadWaterRegion (loop @0x5e79d3..0x5e7c3c) — VERIFIED-1:1
Read order: texName string; texByteA(0x5e79ea, unused); texByteB(tex flag
`4*(b&0xF)`); texByteC(tex flag `b&1`); 4 flag bytes packed into rec+8 as
`b0 | b1<<8 | ((b2&1)|((b3&1)<<1))<<16` (BYTE3=0) — confirmed via the
0x5e7a89/0x5e7aa5/0x5e7ad8/0x5e7b04 bit ops. rec+20: `<0x3A6C00B6` -> 0x427C0000
(63.0f, get_int verified 1115422720) else stream dword (0x5e7b43/0x5e7dbf/
0x5e7b4e). rec+12, rec+16 dwords; ReadVec4 rec+24; `<0x3A6C00AC` -> each
component *= flt_62BEA0 @0x62BEA0 = 0x3EAAAAAB (get_int 1051372203), x87
single*single modelled with double then stored single; ReadVec4 rec+40;
rec+340: `<0x3A6C00B9` -> 0 else low byte of a dword. All match.

### SkipObject (mirror of ReadObject @0x5e67c8) — VERIFIED-1:1 (shipped ver 0x3A6C00BB)
Verified the full body byte-grammar against 0x5e67c8 for the shipped version:
- explicit-kind byte read when `>=0x3A6C00A6` (0x5e6c7b).
- type 0: byte, dword, 3×vec3.
- type 1/4: 2 bytes; `>=0x3A6C000D` byte; `>=0x3A6C00A6` 2 bytes + B4 byte +
  B9 byte (the `<=0x3A6C00B8` block has NO reads, only bit-fiddling — confirmed
  0x5e6e13..0x5e6e27); dword(+532); lodN dword; if lodN>0 one string (ver>=A4
  mesh path, 0x5e6f3d); 2×vec3; byte + optional 2×vec3; `>=0x3A6C00AF` 10×
  (vec3,vec3).
- type 2/3: LABEL_88 (2×vec3; byte + optional 2×vec3; `>=0x3A6C00AF` 10×vec3,vec3).
- type 5..8: byte; `>=0x3A6C00A9` byte; 3 dwords; 3 vec3; `>=0x3A6C00AC`
  2 dwords; kf = (`<0x3A6C00BA`?6:7) iterations of (3 dwords, 3 vec3,
  `>=0x3A6C00AC` 2 dwords).
- framing: child byte (+recurse), sibling byte (+recurse), `>=0x3A6C00A7`
  event bindings.

### Object_Spawn type byte @0x5b054c — VERIFIED-1:1
`a1<5` -> obj+533 = a1. `a1>=5` -> from name[0]: `==0x72('r')`->6, `==0x73('s')`
->8, `==0x70('p')`->7, else 5. Note the editor-only `byte_649D54 && 533==6 ->5`
remap is byte-neutral for SKIP (types 5 and 6 take the same read branch).
`TypeFromName` matches. The `<0x3A6C00A6` (pre-explicit-kind) switch-on-(char)n
path is a documented limitation — no shipped scene uses it (all are 0x3A6C00BB).

### Event bindings @0x5f4bc8 (SkipEventBindings) — VERIFIED-1:1
count dword, then count×(string,string). Matches.

### DeriveFloorPlacement (LoadFromHeightmap @0x5bd44c) — VERIFIED-1:1
x87 trace 0x5bd711..0x5bd7c1 confirmed: originX(floor+144)=-N*0.5*cellScale,
originY(floor+148)=heightScale*-64.0, originZ(floor+152)=0.5*N*cellScale;
axisU=(cellScale,0,0), axisV=(0,0,-cellScale), axisH=(0,heightScale,0);
tileSpan(floor+4)=signed N/8 (`sar/shl/sbb/sar` by 3). Constants get_int
verified: flt_628ADC=0x3F000000(0.5), flt_628AE0=0xC2800000(-64.0),
floor+0x1C64=0x41A00000(20.0), floor+0x1C68=0x42200000(40.0).

### BuildCityHeightmapFromFloor / NormalizeFloorTextureGrid — VERIFIED-1:1
scaleY constant flt_628BA8 @0x628BA8 = 0x3B81848E (get_int 998343822);
flt_628BA4 @0x628BA4 = 0xBFE00000 (-1.75, get_int 3219128320). Min/max
normalize updates (`if(max<=b)`, `if(min>=b)`, then `b-=min`) match.

---

## scene_drawlist.cpp

### SetBlendMode @0x5e0358 — BOUNDARY (DirectDraw/D3D fixed-function; Rule 3)
The original is the DirectDraw/Direct3D render-state setter (vtable `+88` =
SetRenderState pushes alpha-blend / fog / Z-write states into the device). Per
Rule 3 the fixed-function GPU path is replaced by the CPU/Vulkan rasteriser, so
the function is not reconstructed byte-for-byte. scene_drawlist.cpp cites it only
for the load-bearing **semantic**: when blending is active the Z-write state is
forced off (`v5 = a3 && !byte_64A352`; renderstate write @0x5e051d). This is
faithfully mirrored as `if (!transparent) zbuf[idx] = z;` — transparent
fragments are depth-tested but never write depth. Citation accurate.

### BuildSceneTextureMips / SampleLevel / RasterizeDrawList — VERIFIED (backend-neutral mirror)
These are the backend-neutral CPU rasteriser that re-implements the
projection + winding-cull + depth-buffered barycentric raster math of
play::RenderSceneObjects for Vulkan validation (no single original function;
the box-filter mip pyramid + trilinear sampling are the validation aid). The
only original-behavior claim is the SetBlendMode z-write semantic above, which
holds. No divergence.

---

## Test results

Built & ran (sandbox-clean, GUILD_GAME_DIR set):
- `render_scene_floor_test` — 138 checks, 0 failures (PASS).
- `render_scene_load_test` — all `SceneLoad.*` / `SceneFloor.*` scene-IO suites
  PASS (incl. ObjectFramingChildSiblingRecursion + all wave-11 hardening cases).
  1 FAIL: `ScriptRun.LoadCompileRunMain` (line 332) — PRE-EXISTING and UNRELATED:
  exercises the `.esc` script VM (`LoadScriptFromSource`/`RunMain` in
  `src/sim/script_run.cpp`), not any of this module's three files; it only shares
  the test binary via the CMake glob. Not caused by, and out of scope for, this
  pass.
- `floorwater_regions_test` — PASS.
- `scene_blend_test` — 4/4 PASS.
- `floorwater_regions_e2e_test` — PASS (real shipped scenes).
- `terrain_ground_test` — PASS.
- `scene_drawlist_equiv_e2e_test` — PASS (CPU-vs-drawlist equivalence).

The present==0 framing fix is verified not to regress real-asset scene parsing
(floorwater e2e over the shipped Staedte scenes still passes).
