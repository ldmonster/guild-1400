# HARDEN render_01 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files, `src/render/camera_* clip cloth_anim colorformat coord_* cull
d3_projection daycycle emitter_setup env_map_walk fade falloff_lut floorgfx_recon
floor_reload floorwater fog font`. Every `gilde.exe 0x…` function decompiled and
diffed; every `@0x…` table fetched with `get_bytes` and diffed programmatically.

## Per-function results

### camera_edge_scroll.cpp
- `0x4b2c34 VIBE_Camera_EdgeScroll` — **VERIFIED-1:1** (box math, latch, ±1 axis
  steppers, all five view snaps at node +92/+180/+204/+228/+252, commit tail).
  `dword_4AD1C4` = 16 zero bytes (get_bytes) ✓.

### camera_recon2.cpp
- `0x4b300c VIBE_Camera_RotateView` — **FIXED**: the 4x4 MatrixCopy output was
  indexed 0/3/6, 1/4/7, 2/5/8; the decompile's stack offsets (v7@+0, v10@+0x10,
  v13@+0x20) prove 16-float indices 0/4/8, 1/5/9, 2/6/10. Constants at
  0x61DDA0/0x61DDA8/0x61DDB0 and the 0x4AD1D4..0x4AD200 basis dwords verified
  byte-exact.
- `0x4b41a8 VIBE_Camera_UpdateMovement` — **FIXED** (4 divergences):
  1. pan-init `dword_11BC33C` = viewShift (eax sar'd at 0x4b42d9, preserved
     across ConvertX @0x5c6b08 which only pushes/lea's esp) — was the truncated
     float.
  2. pan-init `dword_62D0D0` (box3) = `(dword_672174>>16) + v59` (0x4b4333:
     `add ebx, eax`, eax still Y) — was `trunc(0.0) + v59`.
  3. pan branch `v7` uses `SHIWORD(dword_672174)` (the Y coord) — was viewShift.
  4. rotate branch basis rows: `v37 = axis·(m0,m4,m8)`, `v39 = axis·(m2,m6,m10)`
     (decompile v31/v33/v35 and v32/v34/v36 stack offsets) — was 0/3/6 & 1/4/7;
     same fix in the below-threshold else branch.
  5. release tail: removed the invented `dword_62D4E4 = 1` (the only xref inside
     0x4b41a8 is the head read @0x4b41b4); added the ConvertY(0x40da48)
     observable writes (packed cursor globals restored to drag-start coords;
     SetCursorPos leg skipped because 62D0D4 was just set 1); the
     `dword_631E00/04/08` stores are ecx = `dword_672238` (ConvertY push/pops
     ecx) — was a bogus `trunc(mvCoordX0)` triple.
- `0x4b5250 VIBE_Camera_ZoomReset` — **VERIFIED-1:1** (matrix cols 0/4/8 etc.,
  0x61DE5C/60/68/70 constants byte-exact; hook-abstracted leftover-register args
  documented).
- `0x4b5974 VIBE_Camera_ZoomOut` — **FIXED**: the terminal
  `VIBE_Sound3d_SetListenerFromVectors` posVec is the **eased position triple**
  `{v19,v20,v21}` (disasm 0x4b5c8c: `lea edx, [esp+var_90]`), not the world
  triple v31 (ebx @0x4b5c85 = angVec). Test pin updated (see below).
- `0x4b562c VIBE_Camera_OrientToTarget` — **FIXED** (2 constants):
  `dbl_61DE80` = **-1.0** (bytes 00..f0 bf; loads @0x4b5765/94/0x4b585c/80 — the
  dot clamp floor), was 0.0; `flt_61DE88` = **700.0f** (bytes 00 00 2f 44; fadd
  @0x4b58e8), was -1.875f. Test pin 898.125 → 1600 updated.
- `0x5e967c VIBE_Camera_UpdateTrackTargetFromMouse` — **FIXED**: the tilt block
  reuses ecx (v3) for the tilt dy (or 0), which feeds
  `VIBE_Scene_HandleDebugKeyToggle` at the tail; the reimpl left v3 at the pan
  dx. Constants 0x62BF2C..3C verified. Axis-routing tree, latch blocks, walk
  kinds 14/6 VERIFIED.

### camera_recon.cpp
- `0x43f528 / 0x43f5dc CmdCameraFlight(Timed)` — **VERIFIED-1:1** (ctx model;
  divisors 17/14, report-on-fail only in the untimed variant).
- `0x4b2900 VIBE_Camera_AnchorToTerrain` — **FIXED (x87)**: the position-Y and
  world-X chains are single-fstp x87 expressions; modeled with double
  intermediates (was float-chain with per-op rounding).
- `0x4b2a0c VIBE_Camera_ClampToTerrainHeight` — **FIXED (x87)**: v1 stays 80-bit
  for the fabs/deadzone compare but is `fst`'d to the float v12 @0x4b2a58, and
  the step multiply reloads the FLOAT (`fld var_28; fmul flt_61DD78`
  @0x4b2a6d) — the reimpl multiplied the double. Also `(spanH-baseH)` stays
  unrounded mid-chain (double diff). Constants dbl_61DD70/78/80/88/90/98
  verified byte-exact (5.0, 1/15, ±10, ∓0.005).
- `0x4c20ec VIBE_Camera_ComputeZoomScale` — **FIXED (x87)**: the band result
  passes through FLOAT locals (v15/v16/v17 fstp dword) before the compare and
  the ConvertX truncation; the reimpl truncated the raw double. Constants
  0x61E578/7C/80 = 500/1600/16000 verified.

### camera_update_recon.cpp
- `0x40da48 VIBE_Coord_ConvertY` — **VERIFIED-1:1** (packed cursor-dword writes;
  SetCursorPos when !62D0D4; ecx preserved).
- `0x4b4c68 VIBE_Camera_Update` — **VERIFIED-1:1** (early box-init path incl.
  the overlapping 69FFBA read; gates; family write with rec[32] last;
  unconditional history mirrors; return dword_631628).

### clip.cpp
- `0x5AD7D8 VIBE_Render_ClipPolygonToPlane` — **FIXED (x87 asymmetries)**:
  (a) first vertex inside-test uses the float-stored dot v24, subsequent tests
  use the 80-bit register dot v15 (`v16 = v15 >= v27[3]`); (b) the X-lerp
  consumes the raw division v18 while Y/Z/W/+44 reload the float store v20;
  (c) the +44 scalar lerp is a single-fstp chain. All modeled exactly; loop
  structure/pool/ping-pong VERIFIED.

### cloth_anim.cpp
- `0x4b5d98 VIBE_Character_AttachFlag` — **FIXED**: `VIBE_Object_ApplyParentTransform`
  @0x5b7e24 is `(obj@eax, pos@edx, mat@ebx)` — the call @0x4b5e17 passes the
  bone-chain pos in edx (matrix still live in ebx); the hook dropped the pos
  argument. Hook signature + call + test updated. Euler {0, π(0x40490FDB), 0},
  texture -62 bias, byte530 (&0xB3)|0x44, byte529 &~2 VERIFIED.
- `0x4b5e9c VIBE_Character_ShowFlag` — **VERIFIED-1:1**, address annotation
  corrected (was 0x4b5e90, which is AttachFlag's epilogue).
- `0x4b5ef8 VIBE_Character_RefreshFlagAnimation` — **VERIFIED-1:1** (universe/
  heraldry gates; build byte ∈ {5,6,7} via byte_12CE912[536*idx]; +496
  suppression documented no-op).
- `0x4b62c0 VIBE_Character_CollectFlagNodes` — **VERIFIED-1:1** (count cap 32).

### colorformat.cpp
- `0x4358d8 ComputeChannelShifts` — **VERIFIED-1:1** (two 32-pass loops, 8-set
  precision; output-pointer mapping consistent with Pack/Unpack pairs).
- `0x434f30 PackColor` / `0x434f7c UnpackColor` — **VERIFIED-1:1** (prec/pos
  pairings 71C/71E, 71B/71A, 719/71D consistent through the ColorFormat model).

### coord_transform_leaf.cpp
- `0x5d8b00 VIBE_Coord_Transform` — **VERIFIED-1:1** (`+4*a2+0x45`).
- `0x5d9104 VIBE_Resource_FlushAndFree` — **VERIFIED-1:1** (disasm: result kept
  in edx across FreeStream).

### coord_view.cpp
- `0x4525b4 VIBE_Coord_ComputeViewScale` — **VERIFIED-1:1** (dbl_619130 = 0.006
  byte-exact; ConvertX chop; clamp [5,16]).

### cull.cpp
- `0x5ad614 VIBE_Render_ComputeVertexClipFlags` — **VERIFIED-1:1** (6 plane
  bits with the exact clear-then-set idiom; poly pass masks 0x3F/0x80).

### d3_projection.cpp
- `0x5de3e4 VIBE_Render_SetProjectionTransform` — **VERIFIED-1:1** (D3DVIEWPORT2
  clipX=-1/clipW=2/clipY=h/w/clipH=(h/w)*flt_62A6F4(=2.0, byte-verified);
  matrix _33 = near/(far-near)+1, _34 = 1, _44 = near).

### daycycle.cpp — fresh campaign fixes re-verified, NOT churned
- `dword_4AD160` 96 bytes byte-exact ✓; `0x4b2438 / 0x4b2504 / band split`
  **VERIFIED-1:1**.

### emitter_setup.cpp — all 17 setters **VERIFIED-1:1**
- 0x43fe9c/0x43ff30 (amplitude/phasespeed incl. +0xB0/+0xB4 = v6[44]/v6[45]),
  0x43ffc4, 0x44002c, 0x440068, 0x4400d4, 0x440144, 0x4401b4, 0x440234,
  0x44028c (202/201/200/203 byte order), 0x4402e4 (mask order), 0x4403c8,
  0x440418, 0x440468, 0x4404b8, 0x440508, 0x440558, 0x440590. Scale constants
  0x617748/74/A0/F0/…/0x6178F0 = 0.01f, 0x617820 = 0.001f byte-verified.

### env_map_walk.cpp
- `0x5c9054` walk fragment — **VERIFIED-1:1** (gate vertex+77, skinned normals
  from keyframe+184 triple vs per-vertex source at *(v+72)+12.., uv writes
  +32/+36, stride 80). (The per-vertex kernel lives in vertex_lighting.cpp,
  outside this chunk.)

### fade.cpp
- `0x41f1cc FadeAlpha` — **FIXED (x87)**: the ratio is fstp'd to the FLOAT v23
  before the 0/1 clamps compare/widen it; the reimpl clamped the raw double.
  FadeIsDone (bit1/==1.0, bit2/==0.0) **VERIFIED**.

### falloff_lut.cpp  ← highest-impact fix
- `0x5f0b9c VIBE_Math_AcosGuarded` — **FIXED**: computes **acos(x)**, not
  asin(x). Proof: VIBE_Math_Atan2 @0x5f5701 executes `fxch st(1); fpatan`, so
  with the caller stack {st0 = x, st1 = sqrt(1-x²)} (after the caller's own fxch
  @0x5f0bc7) the fpatan sees y = x, x = sqrt(1-x²) → asin(x); then
  `pi/2 (tbyte_64A7D4, byte-verified) − asin(x)` = acos(x). The |x|==1 guard
  (0 / π) only makes sense for acos.
- `0x5c88f8 VIBE_Light_InitFalloffTable` — **FIXED**: table[i] =
  1 − acos(i/1024)·(2/π) — a RISING curve ~0 → ~0.97 (matches light.cpp's
  already-correct BuildFalloffLUT and the ApplyToCachedVertices consumer).
  Store base clarified: the loop pre-increments (`add ecx,4` before
  `fstp flt_140510C[ecx]` @0x5c8929), so out[i] models **flt_1405110[i]** —
  exactly what all binary readers index. Constants 1/1024 & float-2/π verified.
- Consequence fix in `tests/integration/render_leaves7_itest.cpp`: the `table+1`
  offset double-counted the pre-increment (and read OOB at idx 1023); now
  `lut1405110 = table`. Golden pins updated in three test files (old→new
  documented in-file, e.g. t[0] 1.0 → 4.03e-8, t[512] 0.66667 → 0.33333,
  t[1023] 0.02814 → 0.97186).

### floorgfx_recon.cpp — **VERIFIED-1:1** (no changes)
- 0x5fc4ec DarkenSurface (in-place 32-bit body, odd-pixel a4 preamble),
  0x5fc554 RemapSurfacePalette, 0x5fc5a4 FadeBlend565 (incl. the u8 blue-carry
  bleed and the (8*v8)&0xF800 red pack), 0x5fc6a8 FadeBlend555 (the 0xFC00 red
  mask is 1:1 — it is NOT 0x7C00 in the binary), 0x5d9078 SetFadeParams
  (565/555 select on byte_76271E==11), 0x5c2a9c minimap step/span extracts,
  0x5bd010 slot resolution (first-empty scan @+6820), 0x5B8C90 mip suffixes
  (11-byte stride), 0x5ba1e8 sub-block torus sample + width clamp,
  0x5db928+88 texel mask `(w-1)|(w*w-1)`.

### floor_reload.cpp
- `0x5bd2d8 VIBE_Floor_ReloadTextures` — **VERIFIED-1:1** for iteration (+6756
  name rows ×64, 3 mips ×32 with buffer at +48, suffix stride 11, LoadBuffer
  flags 17). Documented model deviation: returns the attempted-load count for
  testability (binary returns the loop counter 8; no real caller consumes it).

### floorwater.cpp
- `0x5ba750 FloodFillMask` — **VERIFIED** (explicit-stack transform of the true
  recursion; neighbour set/preds 1:1; documented, order-independent result).
- `0x5ba898 FillHeightGradient` — **FIXED (x87)**: the step quotient is fstp'd
  to a FLOAT slot (v8) and re-added each iteration; the reimpl kept it double.
- `0x5ba824 FindRegionOffset` — **VERIFIED-1:1** (type/lo/hi scan, +24 next-type
  peek, 0xFF marker stamp, 80*(base+query−lo)+*(a1+28)).
- `0x5be428 AnimateWaterVertices` — **VERIFIED-1:1**: all four wave forms and
  constants (2.7/2.4/2.5/2.6/π/2.2/4.0 @0x628AEC.. byte-verified); the wave
  multiplier is the CONSTANT dbl_628AF4 = 2π (fld @0x5be536), and the live
  caller (water_vertices.cpp) passes kTwoPi ✓.
- `0x5ba95c BuildWaterRegions` — **FIXED**: end-of-row gradient seed reads
  terrain[n·row + n − 1] (`a1[4] + *a1 + *a1*row − 1`), the reimpl read one cell
  earlier (n·row + n − 2). Verified 1:1: 9-stamp dilation offsets, height AND /
  copy pass, run open/close seeds (−2 bias, min 1), edge bitcode switch (set on
  1/7/8/9/14, clear on 2/4/6/11/13, bit0 = diag(c+1,k+1), bit1 = right(c+1,k)),
  0xFE seeding + flood-fill ids, 344-byte mesh init dwords (5 + 3×1.0f +
  1106771968), texture "EF_WASS_06A_2T_W_AN0" flags 172 (string @0x6287cc),
  tile clamp N−4, post-increment zero block + [82]=[83]. Documented deviations
  kept: masked (in-bounds) dilation at row/col 0 (the binary's unmasked form is
  a latent OOB write that shipped maps never trigger) and the height-run span
  predicate note.

### fog.cpp
- `0x5ae2a0 SetFogRange` / `0x5ae384 ConfigureFog` — **VERIFIED-1:1** for state
  (255/(far−near) slope, flt_628088=255 byte-verified; latches; v3 = a1<a2);
  view-transform refresh side effects documented as renderer-owned.
- `0x5b8b04 ApplyAmbientBlend` — **FIXED**: (a) the t≤1 guard is a SIGNED BIT
  compare `SLODWORD(a3) > 0x3F800000` (also rejects NaN) — was `t > 1.0f`;
  (b) the near/far lerps are fstp'd to floats (v18/v16) BEFORE the flt_64A018
  scale multiply — was a fused float chain. Colour lerp + ConvertX truncation +
  BYTE2/BYTE1/LOBYTE packing VERIFIED.
- ComputeFogFactor (0x5ac9aa/0x5be668 fragment) — **VERIFIED** (255.0 ceiling).

### font.cpp
- `0x42e350 VIBE_Font_InitGlyphTable` — **VERIFIED-1:1** (memset-256 idiom, all
  scalar entries, both literal blocks byte-for-byte, assignment order, ret 0).

## Test targets run (all green)
camera_recon2_test 61 · camera_recon_test 40 · camera_edge_scroll_test 75 ·
camera_recon5_flight_test 9 · camera_update_recon_test 71 ·
render_falloff_lut_test 32 · render_shadow_test 2196 · render_leaves7_itest 17 ·
render_shadow_e2e_test 759 · render_clip_test 48 · render_clip_e2e_test 20 ·
render_cull_test 32 · render_cull_e2e_test 49 · render_cull_itest 15 ·
d3_projection_test 29 · env_map_walk_test 37 · cloth_anim_test 84 ·
floorwater_regions_test 390 · floorwater_regions_e2e_test 13 ·
water_vertices_test 405 · water_vertices_itest 105 · water_vertices_e2e_test 368 ·
water_render_test 140 · render_fog_test 265 · render_fog_itest 19 ·
render_fog_blend_itest 26 · render_fog_e2e_test 146 ·
render_emitter_setup_test 54 · render_emitter_setup_e2e_test 29 ·
floorgfx_recon_test 54 · skycam_wave20_test 277 · sun_daycycle_test 204 ·
session_atmos_test 2216 · session_atmos_e2e_test 13415 ·
render_tile_lighting_test 126 · render_tile_lighting_e2e_test 16 ·
smallleaves_wave22_test 47 · render_tile_textures_test 79 ·
tile_textures_e2e_test 1130 · wire_scene_fx_test 22 ·
scene_recon2_orchestrator_test 159 · session_camera_test 71 · shim_wheel_test 13 ·
render_leaves7_test 102 · city_gouraud_light_test 27 · texlight_recon_test 63 ·
terrain_texturing_test 32 · render_terrain_test 138 ·
city_terrain_harden_test 3339 · frame_integration_wave6_test 16 ·
rain_weather_wave6_test 61 · particle_spawn_itest 16 · particle_spawn_e2e_test 42 ·
playable_flow_e2e_test 154 · real_city_render_itest 13 · world_render_itest 18 ·
world_render_e2e_test 12 · app_render_frame_itest 10 · render_pipeline_test 15 ·
render_pipeline_e2e_test 29. **0 failures.**

## Golden pins updated (with binary evidence)
- camera_recon2_test: ZoomOut posVec (0.1,0.2,0.3) → (0,450,−1680) [0x4b5c8c];
  OrientToTarget eye-Y 898.125 → 1600 [flt_61DE88 @0x61DE88 = 700.0f].
- render_falloff_lut_test / render_shadow_test / render_shadow_e2e_test /
  render_leaves7_itest: asin-form goldens → acos-form [0x5f0b9c/0x5f5701],
  LUT base offset +1 → +0 [store pre-increment @0x5c8929].

## Post-consolidation adjudication — session_camera_test (6 failing checks)

All six failures were **stale-pin fallout** from the binary-proven
camera_recon2.cpp fixes; **no reimpl edit was wrong** and none was reverted.
(Root cause of the miss: one build batch passed the target list as a single
zsh word, so session_camera_test & friends ran STALE binaries in the first
sweep; all 13 targets have now been rebuilt explicitly and rerun green.)

Per check:
- **:194 zoom 0.33 / :195 disableMove==1** (RightDragMovesEyeViaRotateBranch)
  — RE-PINNED. The old pins encoded the invented `dword_62D4E4 = 1` on drag
  release. Binary: the release tail @0x4b4ae3 never writes 62D4E4 (only xref
  inside 0x4b41a8 is the head read @0x4b41b4; real writers are flight/cutscene
  paths — CmdCameraFlight @0x43f540, Cutscene_Teardown @0x4aa5c9, …). The wheel
  therefore works on the very next frame: zoom 0.33 → **0.43**, disableMove
  1 → **0**.
- **:206 zoom 0.43** — RE-PINNED to **0.53** (a second live wheel step follows;
  the EdgeScroll latch checks @0x4b2c34/0x4b2d95 unchanged and still pass).
- **:224 eyeX unchanged while "detached"** (DetachGatesPanUntilEdgeScrollReattach)
  — RE-PINNED + test renamed to `DragReleaseLeavesPanAlive`: with no 62D4E4
  write on release, the UpdatePan gate @0x4b3685/0x4b3692 stays open and the
  arrows pan immediately (`eyeX > x0`); added `disableMove == 0` after release.
- **:279 yaw pin 0.441 / :280 yaw1 != yaw0** (RotateInputMutatesExposedRotation)
  — RE-PINNED. The old pin encoded `dword_11BC33C = trunc((1-zoom)*666.667)`
  (=446) producing a spurious +0.441 yaw jump on the pan-init frame. Binary:
  0x4b42ee stores eax = viewShift (sar'd @0x4b42d9, preserved across
  VIBE_Coord_ConvertX), so dx = 0 on the init frame → yaw1 == 0 == yaw0. The
  subsequent-frame delta pin (−40·0.0035, kMv_flt_61DE28) was already correct
  and still passes.

Re-run green (fresh builds): session_camera_test 72 ·
scene_recon2_orchestrator_test 159 · shim_wheel_test 13 · render_leaves7_test
102 · city_gouraud_light_test 27 · texlight_recon_test 63 ·
terrain_texturing_test 32 · render_terrain_test 138 · city_terrain_harden_test
3339 · frame_integration_wave6_test 16 · rain_weather_wave6_test 61 ·
particle_spawn_itest 16 · particle_spawn_e2e_test 42 · city_view3d_e2e_test 52 ·
playable_flow_e2e_test 154. **0 failures.**
