# TOWN-1 hardening — town-view scene/camera/map/pick

Agent: TOWN-1 (town-view GUI: in-city 3D view + camera + map + pick).
Chunk: src/play/{city_view3d, scene_view, city_frame, city_info, sdl_city_screen3d,
map_view, scene_pick, camera_controls, camera_pick}.cpp (+ headers/tests).
MCP: live. Reference of record = gilde.exe disasm/decompile.

## Summary
These 9 files are almost entirely HOST orchestration/scaffold layers that delegate the
genuine engine math to REAL reconstructions owned by other chunks (render/sim/util/app/
gui). The platform boundaries (rules 3-5) are respected: GPU scene render =
`device.renderScene3D` (Vulkan), SDL input/loop, DDraw/D3D blend state stays a hook.

The ONE genuinely address-mapped 1:1 function in the chunk is the object picker
(`PickNearestObject` <- `VIBE_Pick_FindNearestObjectAt @0x5b5a38` and its callees).
It is verified line-for-line. One real divergence found+fixed in the camera-pan velocity
model vs `VIBE_Camera_UpdatePan @0x4b365c`.

## FIXED

### camera_controls.cpp  `RampAxis` — vs VIBE_Camera_UpdatePan @0x4b365c
- Evidence (decompile @0x4b365c, the per-axis ramp arms, e.g. v0=2 path):
  ```
  if ( (dword_631DF8 & 0x7FFFFFFF) == 0 ) dword_631DF8 = 1092616192;   // kick +10
  if ( *(float*)&dword_631DF8 < dbl_61DDD0 ) *(float*)&dword_631DF8 += v42;  // < 50 -> +step
  ```
  The engine adds the accel step ONLY while strictly below the bound and NEVER clamps
  afterward, so velocity can overshoot the bound by up to one step.
- Before: active arms returned `Clampf(vel, kPanVelMin, kPanVelMax)` (strict cap at ±50).
- After: active arms return `vel` un-clamped (matches `if (vel<50) vel+=step;` with no
  post-clamp). Behavior-identical for the existing dt=0 goldens (ramp 10,15,...,50 lands
  exactly on 50 -> `50<50` false -> stays 50), but now correct for variable-dt frames
  where one step can carry vel past ±50.
- Idle-decay arms were already exact (traced the `||`/comma decompile: `vel-=step; if (<0)
  =0` for vel>0, mirror for vel<0, no-op at 0) -> left unchanged.

## VERIFIED-1:1

### scene_view.cpp  `PickNearestObject`  <- VIBE_Pick_FindNearestObjectAt @0x5b5a38
Wrapper @0x5b5a38: `v12=1.0e10` best init (recon `bestD2=1.0e10f` ✓); cursor relative to
screen centre `flt_13FCD18-a2 / flt_13FCD10-a3`; min-distance select. Per-object work is
`VIBE_Pick_TestObjectAtPoint @0x5b5938`:
- `v5 = 1.0/v13` (1/viewZ); `screenDX = a7*cx*inv + (centre-cursor)`, `screenDY =
  -a7*cy*inv + (centre-cursor)` (flt_13FCAF8 = -a7). Recon `screenX=cx+a7*cx*inv;
  dx=screenX-cursorX` etc. — algebraically identical ✓.
- disc test `d2 < bestSlot && sqrt(d2) < projR` ✓.
- NO float->int site in the whole pick path (all float/double; comparisons in float). No
  ConvertX/fistp truncation concern.
`ComputeBoundingRadius @0x5b2cf8`: 8 corners averaged `* flt_6282F4` (= 0x3e000000 =
0.125, get_bytes ✓ -> recon `vsum*0.125f`); radius = `sqrt(max |corner-centre|^2)` ✓;
out args a3=radius, a4=centre[3] ✓.
The scene-walk/vtable corner fetch is the documented host-recon boundary (no engine bone
matrix in the standalone pick path); the projection MATH is 1:1.

### camera_controls.h constants  <- VIBE_Camera_UpdatePan @0x4b365c (get_global_value/get_bytes)
0x61DDB8=0.4, 0x61DDC0=4.0, 0x61DDC8=5.0, 0x61DDD0=50.0, 0x61DDD8=-50.0, 0x61DDF0=1.5,
0x61DDF8=0.6, 0x61DE00=1.1, 0x61DE08(f)=0.1, kick 1092616192=0x41200000=10.0. ALL match.
Apply chain verified vs decomp: `v41*=flt_6316D4; v41=(flt_6316DC*1.5+0.6)*v41; delta=
vel*v41*1.1` (recon CameraUpdatePan apply block) ✓. (The full 8-direction edge dispatch,
off_649D64 boundary clamp and word_62D310 state machine are NOT modeled — documented host
velocity-model reconstruction; the velocity ramp/decay/apply core is now 1:1.)

### scene_pick.cpp  constants + delegation
biasX 0x628B94 = 0x3f600000 = 0.875 ✓; lightCap 0x628B98 = 0x437e0000 = 254.0 ✓.
Projection delegated to REAL `ProjectVerticesToScreen @0x5c5120`; pick is host pixel-
distance glue (no separate binary formula). 1:1.

### map_view.cpp  <- VIBE_MapView_PanelDispatcher @0x5441d0 (float->int audit)
The marker sort @0x54457e is INLINED in the dispatcher (xref confirms), reconstructed as
`MapViewSortMarkersByScreenY` (other chunk): selection sort ascending by screenY (record
+3 dword), 24-byte qmemcpy swaps — matches. Critical float->int audit: the dispatcher
projects via `ComputeMarkerScreenPos` then `VIBE_Coord_ConvertX()` (@0x5c6b08, TRUNCATES
toward zero) then `(int)`. My `static_cast<int>(sx/sy)` truncates toward zero — SAME
semantics ✓. Sort key is the truncated-int screenY in both. ClickToWorld uses
`g_mapWidth/2` integer-div matching `dword_1233440/2`. 1:1.
(flt_624060=5.33, dbl_624058=0.5 live in gui/mapview.h — other chunk's constants.)

### scene_view.cpp  RenderSceneObjects + blend semantics  <- SetBlendMode @0x5e0358
`SetBlendMode @0x5e0358` decompiled = pure D3D `SetRenderState` (vtable+88) fixed-function
state setter (alpha-blend enable / src factor select / z-write) — the DDraw/D3D boundary
(rule 3). Reconstruction uses it as the SEMANTIC reference: blend!=0 -> transparent,
deferred after opaque, depth-tested but NO depth write (`if(!transparent) zbuf[idx]=z;`).
mode 2 = additive, mode 1 = alpha (material byte0=mode/byte1=opacity @0x5F87B8). Semantics
1:1; GPU state = Vulkan boundary. The CPU rasteriser is the rule-3 reference path; engine
projection delegated to REAL `ProjectViewPoint`/`MakeProjection`.

## BOUNDARY / host-scaffold (no 1:1 binary function to diff)
- city_frame.cpp — host frame compositor; color helpers (green ground / warm objects) are
  test/debug visualizers, NOT engine logic; cites DrawUniverseAndStats @0x5b3bbc only as
  the layer-ORDER reference. Real terrain/world/HUD/present delegated. lround in the quad
  fill is host-visualizer only (no engine site).
- city_info.cpp — host ChooseCity info-card renderer; real text DB (BuildTextArray), gfx
  decode, DrawTextCp1251; `$`-markup parser is a host interpretation of the engine grammar.
- sdl_city_screen3d.cpp — SDL/Vulkan ChooseCity event loop (rules 3-5 boundary); cites
  VIBE_Menu_RunChooseCity @0x52e6d8 as the object model; delegates all math to leaves.
- city_view3d.cpp — town-view integration class; delegates ComputeSunState/ComputeSkyFog/
  GenerateVertexNormals etc. to REAL render-module reconstructions; no local fixed-point
  or float->int engine site. `computeSunForFrame` uses a documented deterministic
  stand-in for the RNG sun-pitch (rule 8, noted in progress/frame-integration-wave6.md).
- camera_pick.cpp — host camera/pick glue (RaySphereHit, edge-scroll) over REAL
  ScreenToWorldRay / ResolveCombatScroll / GameObjectResolveEntityById; no address-mapped
  1:1 function of its own.

## Test status
BLOCKED at link, NOT by my code. The shared `guild` lib fails to compile in
src/play/slice_council.{h,cpp} + dialog_council.cpp ("`sim` does not name a type",
CommandPacket encode signature mismatch) — the council-dialog chunk (another agent),
mid-edit. I must not touch those files or run git. My edited camera_controls.cpp passes
`g++ -std=c++17 -fsyntax-only` standalone. Once the council files compile, my targets
(camera_controls_test/itest/e2e, scene_*_test, map/city/sdl suites) should link and run;
the RampAxis fix keeps all existing camera_controls goldens green (verified by inspection:
no test feeds a dt that triggers the >±50 overshoot path; VelocityClampsAtFifty lands on
exactly 50.0 via the dt=0 step ladder).

## Counts
- FIXED: 1 (camera_controls RampAxis over-clamp vs UpdatePan @0x4b365c).
- VERIFIED-1:1: 5 areas (PickNearestObject chain @0x5b5a38/@0x5b5938/@0x5b2cf8;
  camera_controls constants+apply @0x4b365c; scene_pick constants; map_view truncation
  vs ConvertX/sort @0x5441d0/@0x54457e; scene_view blend semantics @0x5e0358).
- BOUNDARY/host-scaffold: 5 files (city_frame, city_info, sdl_city_screen3d, city_view3d,
  camera_pick) — no own address-mapped 1:1 function; correctly delegate to real leaves.
- Constants confirmed via get_bytes/get_global_value: 13 (9 pan + 2 scene_pick + 0.125 +
  blend mode map).
