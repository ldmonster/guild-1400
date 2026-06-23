# Wave-13 1:1 Fidelity Audit — CAMERA segment (W13-CAMERA)

MCP was DOWN, so this is the MCP-free part of the 1:1 comparison: cross-check each
reconstructed camera function against the in-tree evidence (provenance comments +
progress docs + the source body), PIN its recovered 1:1 values with golden tests,
flag drift, and produce a confidence map. No live binary diff was possible; the
in-tree decompile excerpts in the source comments are the reference of record here.

Scope: `src/render/camera*.{h,cpp}`, `src/play/{session_camera,camera_controls,
camera_pick}.{h,cpp}` and their tests. (Did NOT touch sdl_session / city_view3d /
universe_render / terrain_render — owned elsewhere.)

## Result summary

- Segment build: GREEN. All 9 test binaries pass: **399 checks, 0 failures**
  (was 391; +8 new golden checks from this wave).
- Source edits: NONE needed — no evidence-backed drift was found; every constant /
  offset / control-flow claim in the source matches its provenance comment and the
  two progress docs (session-camera.md, camera-recon2-movers.md).
- Test additions: 3 new golden tests in `tests/unit/camera_recon2_test.cpp` pinning
  previously-unasserted recovered values (RotateView inverted-axis pitch, ZoomOut
  listener kind, OrientToTarget eye-offset constants).

## Inventory (function -> address -> entry point -> pin status)

### Reconstructed-from-a-VIBE-function (1:1 translations)

| addr | name | entry point | file | confidence |
|------|------|-------------|------|------------|
| 0x4b4c68 | VIBE_Camera_Update | `render::Camera_Update` | camera_update_recon.cpp | GOLDEN-PINNED |
| 0x40da48 | VIBE_Coord_ConvertY (cursor write) | `render::Camera_CursorCoordWrite` | camera_update_recon.cpp | GOLDEN-PINNED |
| 0x4b2c34 | VIBE_Camera_EdgeScroll | `render::Camera_EdgeScroll` | camera_edge_scroll.cpp | GOLDEN-PINNED |
| 0x4b2900 | VIBE_Camera_AnchorToTerrain | `render::Camera_AnchorToTerrain` | camera_recon.cpp | GOLDEN-PINNED |
| 0x4b2a0c | VIBE_Camera_ClampToTerrainHeight | `render::Camera_ClampToTerrainHeight` | camera_recon.cpp | GOLDEN-PINNED |
| 0x4b300c | VIBE_Camera_RotateView | `render::Camera_RotateView` | camera_recon2.cpp | GOLDEN-PINNED (both axes now) |
| 0x4b41a8 | VIBE_Camera_UpdateMovement | `render::Camera_UpdateMovement` | camera_recon2.cpp | GOLDEN-PINNED (pan/wheel/rotate) |
| 0x4b5250 | VIBE_Camera_ZoomReset | `render::Camera_ZoomReset` | camera_recon2.cpp | UNDER-VERIFIED (structural only) → NEEDS-LIVE-MCP for numerics |
| 0x4b5974 | VIBE_Camera_ZoomOut | `render::Camera_ZoomOut` | camera_recon2.cpp | UNDER-VERIFIED → kind 104 now pinned; numerics NEEDS-LIVE-MCP |
| 0x4b562c | VIBE_Camera_OrientToTarget | `render::Camera_OrientToTarget` | camera_recon2.cpp | GOLDEN-PINNED (kind 40 + eye-offset consts) |
| 0x5e967c | VIBE_Camera_UpdateTrackTargetFromMouse | `render::Camera_UpdateTrackTargetFromMouse` | camera_recon2.cpp | UNDER-VERIFIED (latch/route paths only) |
| 0x4c20ec | VIBE_Camera_ComputeZoomScale | `render::Camera_ComputeZoomScale` | camera_recon.cpp | GOLDEN-PINNED (all 4 bands) |
| 0x43f528 | VIBE_Camera_CmdCameraFlight | `render::Camera_CmdCameraFlight` | camera_recon.cpp | GOLDEN-PINNED (÷17, slot/script gates) |
| 0x43f5dc | VIBE_Camera_CmdCameraFlightTimed | `render::Camera_CmdCameraFlightTimed` | camera_recon.cpp | GOLDEN-PINNED (÷14, no-report) |
| 0x43f4d8 | VIBE_Camera_Flight | `render::Camera_Flight` | camera_recon5_flight.cpp | GOLDEN-PINNED (÷17, selector 2, ret 1) |
| 0x407428 | VIBE_Coord_ProjectPoint | `render::ProjectPoint` | camera.cpp | GOLDEN-PINNED (perspective divide + trunc) |
| 0x407488 | VIBE_Coord_ProjectFramePoint | `render::ProjectFramePoint` | camera.cpp | GOLDEN-PINNED (off-map early-out + project) |
| 0x5ACCD0 | VIBE_Render_BuildViewMatrix (frustum + 64-entry bbox table) | `render::BuildViewFrustum` / `BuildBoundingBoxPlaneTable` | camera_control.cpp | UNDER-VERIFIED (no golden test file for camera_control) → add or flag |
| 0x5E9024 | VIBE_Camera_UpdateOrbitFromMouse (rate math) | `render::ComputeOrbitRates` | camera_control.cpp | UNDER-VERIFIED (no golden test) → flag |

### Interactive GLUE (NOT from a single VIBE_ function — reuse real reconstructions)

These are explicitly documented in their headers as the interactive glue layer the
binary spreads across input dispatch. They are NOT a provenance red flag: they
reuse the REAL addressed reconstructions via `extern` and cite the grounding
functions. They own the *play-layer* camera model the session adapter drives.

| entry point | file | grounds on | confidence |
|-------------|------|-----------|------------|
| `play::SessionCamera` (Init/Frame/pose/BindTerrain) | session_camera.cpp | Camera_Update @0x4b4c68 + the whole dispatched tree | GOLDEN-PINNED (17 tests) |
| `play::CameraUpdatePan` / `CameraControl` / pan-zoom-edge | camera_controls.cpp | UpdatePan core @0x4b365c, EdgeScroll @0x4b2c34 | GOLDEN-PINNED (kick/ramp/decay/zoom-scale/yaw) |
| `play::CameraState` pick/pan/zoom/edge + RaySphereHit/PickObject | camera_pick.cpp | ScreenToWorldRay @0x426850, ResolveCombatScroll @0x487b2c, GameObjectResolveEntityById @0x583b44 | GOLDEN-PINNED (ray math + reuse) |

### Reused leaf on the flow (not in this segment but cross-checked)

| addr | name | entry point | file | confidence |
|------|------|-------------|------|------------|
| 0x427468 | VIBE_Terrain_AverageAreaHeight | `render::AverageAreaHeight` | heightmap.cpp | GOLDEN-PINNED (8x8 box avg; 210 vector in session_camera_test) |

## 1:1 value pins added this wave (all traceable to the source body)

`tests/unit/camera_recon2_test.cpp` (+3 tests, +8 checks; 34 → 42 checks):

1. `Camera2ReconRotateView.InvertedAxisPitchMatchesGolden` — the inverted-axis
   (byte_671D6F set, rotate-button held) pitch branch was untested. Pins
   `flt_61DDA0 = 0.0024999999441206455` AND `dbl_61DDA8 = 2π` on the worldX
   (pitch) path with the exact `Fmod(v36*scale + worldX, 2π)` where
   v36 = (dword_672174>>16) - dword_631DE8. Source: camera_recon2.cpp:221-226.

2. `Camera2ReconZoomOut.ListenerKindIs104` — pins the terminal 3D-sound listener
   KIND constant 104 (distinct from ZoomReset and OrientToTarget's 40) via a
   capturing hook. Source: camera_recon2.cpp:723.

3. `Camera2ReconOrientToTarget` (extended) — pins the eye-offset constants
   `flt_61DE74 = 600`, `flt_61DE78 = 900`, `flt_61DE88 = -1.875`: with the
   identity frame the eye collapses to exactly (-600, 898.125, 0).
   Source: camera_recon2.cpp:747-754, 811.

## Internal consistency check (drift scan) — CLEAN

Verified the SOURCE constants/offsets/control flow against the provenance comments
and the two progress docs. All consistent; no drift fix required:

- camera_recon2.cpp float constants (lines 26-82) match camera-recon2-movers.md's
  "recovered float constants" list byte-for-byte: rotate 0.0025 / 2π / 2.5;
  movement 666.667 / 0.004 / 5 / 0.0015 / 0.0035 / 0.1 / 6 / 0.010101 / -1 / 20;
  zoomreset/out -600 / 0.5 / 1.8 / 0.0005; orient 600 / 900 / 0 / -1.875;
  track π / 30 / 3 / 256 / -256; height 450 / -0.471238911 / 1600 / -1.082104 / 50;
  forward axis (0,0,1).
- The UpdateMovement rotate-branch scalar = `dword_1233564 * 6.0 * 0.010101...`
  (= scroll_speed * 6/99), matching session-camera.md's `mouseDelta *
  scroll_speed * 6/99` claim (camera_recon2.cpp:459) — and the session test
  golden `40 * 50 * 6 / 99` confirms it end-to-end.
- ClampToTerrainHeight step factor `flt_61DD78 = 0.06666667` = 1/15, matching
  session-camera.md's "dy/15 steps" + the 5.0 deadzone (`dbl_61DD70`) and ±10
  saturation. Pinned in both camera_recon_test and session_camera_test.
- The camera-node field contract (eye +76/+80/+84 == world +92/96/100; rot euler
  +132/+136/+140 == world +144/148/152; basis @+396 = `MatrixFromEuler(-euler)`
  for type byte *(node+533)==3) is consistent across camera_recon.h, the
  CameraPose doc, and session_camera.h, and is exercised by
  `SessionCamera.PoseExposesEyeAndRotation`.
- AnchorToTerrain (0x4b2900): zoomT bits aliasing (`LODWORD(flt_6316DC)=a3`,
  `dword_6316E0=a3`), the `baseHeight + (spanHeight-baseHeight)*zoomT` height and
  `baseAngle + (spanAngle-baseAngle)*zoomTAsFloat` pitch — consistent and pinned
  (zoom-0 → 450/baseAngle, zoom-1 → 1600/spanAngle, terrain-add).
- EdgeScroll (0x4b2c34): the [-1,1] per-axis clamp + cross-axis zeroing, the 0.2
  unset-view tolerance (3E4CCCCDh) against the 16 zero bytes @0x4AD1C4, the center
  no-tolerance commit (node+92/+144), the 0x4b2d95 re-attach (clears 62D4E4/E8),
  listener (pos, dword_6316C8=50, rot, kind 8), latch 631DE0 — all consistent and
  pinned.

## Confidence map

GOLDEN-PINNED (high 1:1 confidence — constants + control flow tested):
- Camera_Update, Camera_CursorCoordWrite, Camera_EdgeScroll, Camera_AnchorToTerrain,
  Camera_ClampToTerrainHeight, Camera_RotateView (both axes), Camera_UpdateMovement
  (pan/wheel/rotate branches), Camera_OrientToTarget (kind + offsets),
  Camera_ComputeZoomScale, both CmdCameraFlight variants, Camera_Flight,
  ProjectPoint/ProjectFramePoint, AverageAreaHeight, the whole SessionCamera /
  CameraUpdatePan / camera_pick glue.

UNDER-VERIFIED (reached on the flow but 1:1 numerics not fully pinned in-tree):
- `Camera_ZoomReset` @0x4b5250 — only structural goldens (returns mesh handle,
  clears 62D4E4/E8). The multi-step bone-chain / mesh-height-range easing numerics
  (`-600` dolly, `0.5` height-range weight, `1.8` back-off, `0.0005` dist scale,
  the dist clamp) depend on the inert meshHeightRange / PointThroughBoneChain
  hooks; a full numeric golden needs the exact bone-chain output. Deferred — see
  below.
- `Camera_ZoomOut` @0x4b5974 — kind 104 now pinned; the 3-band dist clamp
  (`minZoomDist` / `3*minZoomDist`) and the dolly numerics are not numerically
  golden for the same reason.
- `Camera_UpdateTrackTargetFromMouse` @0x5e967c — latch/route control-flow paths
  are tested (null-active, pan-latch snapshot, pan-release light-refresh); the
  axis-routing tree (671D8A/671D7D/64A024) full coverage and the
  ApplyTransformConstraints commit numerics are not exhaustively pinned.
- `BuildViewFrustum` / `BuildBoundingBoxPlaneTable` / `ComputeOrbitRates`
  (camera_control.cpp @0x5ACCD0 / 0x5E9024) — reconstructed with documented
  constants (eps 1.9999999949504854e-06, the orbit rate formulas) but there is NO
  dedicated golden test file for camera_control.{h,cpp}. The constants live only
  in the source. RECOMMEND a `camera_control_test.cpp` (out of this wave's
  test-only budget to keep it focused, but flagged).

NEEDS-LIVE-MCP (cannot confirm 1:1 from in-tree evidence; exact decompile targets
for the binary diff when MCP returns):
- 0x4b5250 VIBE_Camera_ZoomReset — verify the bone-chain dolly math, the
  `(hi-lo)*0.5` height-range term, the `v22*v31 - v22*1.8` back-off, and the
  `sqrt(...)*minZoomDist*0.0005` dist easing against the disasm; confirm the
  meshHeightRange (0x42698c) and PointThroughBoneChain (0x5c8b38) call args.
- 0x4b5974 VIBE_Camera_ZoomOut — verify the 3-band dist clamp branch
  (`3*minZoomDist` vs `minZoomDist`) and the listener posVec/angVec (the +132
  world-translation triple) against the disasm.
- 0x5e967c VIBE_Camera_UpdateTrackTargetFromMouse — verify the full axis-routing
  decision tree and the ApplyTransformConstraints (0x5e89b4) result-bit handling.
- 0x5ACCD0 / 0x5E9024 — confirm the frustum eps signs and the orbit-rate constant
  globals (flt_62BF10..28) byte-for-byte.

## Deferred / named gaps carried forward (rule 8, unchanged from prior waves)

- Producer of the node's edge-view blocks (+180/+204/+228/+252) is not in the
  tree (scene/cutscene camera setup); with zero blocks EdgeScroll bails on the 0.2
  tolerance exactly like the original with unset views — only the center re-attach
  commits. (Documented; not a fidelity defect.)
- Family / 3D-sound / anim-free / scene-graph leaves remain inert-default hooks
  (personGetFamilyRecord @0x58c408, sound3dSetListener @0x4262a0, animFree
  @0x5cec00, etc.) — see camera-recon2-movers.md.
- `dword_1233564` scroll_speed is the [Game] INI option (read at 0x56bcd3); not a
  binary constant — a 0..99 knob (midpoint 50) until the config reader joins the
  tree. Its 6/99 scaling IS pinned.
- ZoomReset/ZoomOut callers (RunMainFrameLoop @0x50f0c0 / building loader
  @0x50d01c) are not yet reconstructed, so those movers are off the live session
  loop today — hence UNDER-VERIFIED rather than wired-and-pinned.

## Files touched this wave

- `tests/unit/camera_recon2_test.cpp` — +3 golden tests (inverted-axis pitch,
  ZoomOut kind 104, OrientToTarget eye-offset constants); shared listener-capture
  helper hoisted to the file's anon namespace. No source edits.
