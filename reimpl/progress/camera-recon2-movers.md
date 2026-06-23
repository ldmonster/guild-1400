# camera_recon2 — VIBE_Camera_* "big movers" (second pass)

Cluster: the deferred large camera-control functions of `gilde.exe` (rotate /
per-frame movement / zoom reset / zoom out / orient-to-target / mouse track),
reconstructed once their dependency leaves (math/transform/object setters/
family record/3D sound) were available.

Files:
- `src/render/camera_recon2.h`
- `src/render/camera_recon2.cpp`
- `tests/unit/camera_recon2_test.cpp` (13 tests, 34 checks — all pass)

Builds on the first pass `src/render/camera_recon.{h,cpp}` (reuses its
`CameraObject` / `CameraState` model and `Camera_AnchorToTerrain` /
`Camera_DefaultHooks`).

## Reconstructed 1:1
| addr | name | entry point | notes |
|------|------|-------------|-------|
| 0x4b300c | VIBE_Camera_RotateView | `Camera_RotateView` | mouse-drag orbit; button-held world-translation yaw branch (Fmod 2π wrap), button-free position-orbit branch (matrix basis, axis-invert via byte_671D6F, XZ-length renormalize, Y locked) |
| 0x4b41a8 | VIBE_Camera_UpdateMovement | `Camera_UpdateMovement` | per-frame state machine: drag-start latch, pan-init, pan branch (height/angle lerp), wheel-zoom branch (→AnchorToTerrain), rotate branch (VectorAngleWrapped sin/cos, optional AABB clamp) |
| 0x4b5250 | VIBE_Camera_ZoomReset | `Camera_ZoomReset` | zoom-to-object reset: bone-chain eye, terrain-eased height, 600u back-off, dist clamp [minZoom..], history mirror + commit + family write |
| 0x4b5974 | VIBE_Camera_ZoomOut | `Camera_ZoomOut` | like ZoomReset but ends in 3D-sound listener (kind 104); 3-band dist clamp |
| 0x4b562c | VIBE_Camera_OrientToTarget | `Camera_OrientToTarget` | yaw/pitch via two normalized dot→AcosGuarded, sign flips, 3D listener (kind 40) |
| 0x5e967c | VIBE_Camera_UpdateTrackTargetFromMouse | `Camera_UpdateTrackTargetFromMouse` | two edge-tracked drag modes (672220 pan / 672234 tilt), axis-lock routing tree (671D8A/671D7D/64A024), ApplyTransformConstraints commit, latched light refresh |
| 0x4b2c34 | VIBE_Camera_EdgeScroll | `Camera_EdgeScroll` (`src/render/camera_edge_scroll.*`) | the edge view-snap / camera RE-ATTACH machine (steps dword_6316CC/6316D0, edge view blocks node+180/204/228/252, center = node+92/+144, clears 62D4E4/62D4E8 @0x4b2d95, latch 631DE0). Reconstructed in the session-camera wave — see [session-camera.md](session-camera.md). New Camera2State fields: `edgeScrollX` (6316CC), `edgeScrollY` (6316D0), `edgeSnapLatch` (631DE0); new CameraObject fields: wpos (+92), wrot (+144), edgeTop/Bottom/Left/Right (+180/+204/+228/+252) |

Recovered float constants (get_bytes, exact): rotate 0x61DDA0=0.0025 /
0x61DDA8=2π / 0x61DDB0=2.5; movement 0x61DE10=666.667, 0x61DE18=0.004,
0x61DE20=5, 0x61DE24=0.0015, 0x61DE28=0.0035, 0x61DE2C=0.1, 0x61DE30=6,
0x61DE38=0.0101010, 0x61DE40=-1, 0x61DE44=20; zoomreset/out 0x61DE5C/8C=-600,
0x61DE60/90=0.5, 0x61DE68/98=1.8, 0x61DE70/A0=0.0005; orient 0x61DE74=600,
0x61DE78=900, 0x61DE80=0, 0x61DE88=-1.875; track 0x62BF2C=π, 0x62BF30=30,
0x62BF34=3, 0x62BF38=256, 0x62BF3C=-256; height params 0x6316B4=450,
0x6316B8=-0.471238911, 0x6316BC=1600, 0x6316C0=-1.082104, 0x6316C8=50;
forward axis 0x5CA2B0=(0,0,1).

## Reused leaves (never redefined)
| addr | name | reused from |
|------|------|-------------|
| 0x5cabf0 | VIBE_Math_MatrixCopy | src/util/matrix.h |
| 0x5d3fb2 | VIBE_Math_Fmod | src/util/float_math.h |
| 0x5cb148 | VIBE_Math_VectorNormalize | src/util/math.h |
| 0x5caa4c | VIBE_Math_VectorWithinTolerance | src/util/math.h |
| 0x5ca504 | VIBE_Math_VectorAngleWrapped | src/util/math.h |
| 0x5f0b9c | VIBE_Math_AcosGuarded | src/util/math.h |
| 0x5c8b38 | VIBE_Transform_PointThroughBoneChain | src/util/transform.h |
| 0x4b2900 | VIBE_Camera_AnchorToTerrain | src/render/camera_recon.h (first pass) |

## API updates (session-camera wave)
- `CameraHooks::terrainHeight` (camera_recon.h) now carries the REAL prototype
  recovered from the call sites: `f32 (*)(i32 a1, i32 a2, const f32* world,
  void* user)` + `terrainUser` — a1/a2 are the original's DEAD leftover
  registers (the 0x427468 body stores then never reads them); the real
  argument is `world` = the camera node position triple (eax = node+76 at
  0x4b292a / 0x4b2a2b). Bindable to the reconstructed
  `render::AverageAreaHeight` (heightmap.cpp) — `play::SessionCamera::
  BindTerrain` does exactly that. `Camera2Hooks::terrainHeight` (ZoomReset/
  ZoomOut paths only) keeps the positionless signature until those callers
  join the live tree.
- `Camera_UpdateMovement` gained a trailing optional `const CameraHooks*
  anchorHooks = nullptr`: the wheel branch's `Camera_AnchorToTerrain` call
  @0x4b46b1 now receives the dispatcher's hooks (so a bound terrainHeight
  reaches the anchor exactly as in the original); nullptr keeps the previous
  inert-default behavior — existing callers/tests unchanged.

## Coupled leaves → inert-default hooks (Camera2Hooks)
0x427468 VIBE_Terrain_AverageAreaHeight, 0x42698c VIBE_Mesh_ComputeHeightRange,
0x5cec00 VIBE_Anim_FreeObjAnimData, 0x4262a0 VIBE_Sound3d_SetListenerFromVectors,
0x58c408 VIBE_Person_GetFamilyRecord, 0x5ac738 VIBE_SceneGraph_WalkAndInvoke,
0x5c8538 VIBE_Light_RequestObjectCache, 0x5c886c VIBE_Light_RefreshAllObjects,
0x5e89b4 VIBE_Object_ApplyTransformConstraints, 0x5e872c
VIBE_Scene_HandleDebugKeyToggle, 0x5af38c VIBE_Object_SetPosition, 0x5af50c
VIBE_Object_SetWorldTranslation. Coordinate truncators 0x5c6b08
VIBE_Coord_ConvertX / 0x40da48 VIBE_Coord_ConvertY are x87 round-toward-zero
chops, modeled as `trunc_toward_zero`.

## Faithfulness notes / approximations (documented, not faked)
- `dword_13FCD1C` is a full scene-frame node; `CameraObject` models only the
  touched fields. For ZoomReset/ZoomOut the bone-chain call is run on a
  materialized `FrameBuf` (frame[30..32]=pos2*, null parent link at byte 504),
  reproducing the parent-less camera-node result. OrientToTarget takes a real
  frame buffer (`a1`) from the caller.
- UpdateMovement pan-init's second `VIBE_Coord_ConvertX()` reads the leftover
  x87 stack top (`v5`) into `dword_62D0D0`; modeled as the truncation of the
  immediately-prior FPU value (the only deterministic interpretation headless).
- UpdateMovement's `AnchorToTerrain(a1@eax,…)` first arg is an indeterminate
  leftover register at the call site; passed as 0 (no effect with inert terrain).

## Wiring
| caller | addr | status |
|--------|------|--------|
| VIBE_Camera_Update | 0x4b4c68 (→ UpdateMovement @0x4b4d0e) | DONE — reconstructed in `src/render/camera_update_recon.*`; calls Camera_UpdateMovement directly (see [session-camera.md](session-camera.md)) |
| VIBE_Scene_RunMainFrameLoop | 0x50f0c0 (→ ZoomReset @0x50f394/432) | PENDING — caller not yet reconstructed |
| VIBE_Building_LoadAndAlignGebaeudeModel | 0x50d01c (→ ZoomOut @0x50d890) | PENDING — caller not yet reconstructed |
| RotateView 0x4b300c / OrientToTarget 0x4b562c / TrackTargetFromMouse 0x5e967c | — | no direct xrefs (dispatched via tables / indirect); wire when dispatcher reconstructed |
| Camera_EdgeScroll 0x4b2c34 | 0x48c60f / 0x48c679 / 0x4905e9 (combat loops), 0x4bc4b1 (VIBE_Hud_HandleMouseClick) | reconstructed + wired into `play::SessionCamera::Frame` (the per-frame combat-loop cadence stand-in); the combat/HUD callers themselves are still pending |

All six are exposed via `camera_recon2.h` for their callers; none are dangling
stubs — each is fully implemented and unit-tested. Final caller wiring waits on
the parent functions (Camera_Update / RunMainFrameLoop / building loader) which
are not yet in the tree.
