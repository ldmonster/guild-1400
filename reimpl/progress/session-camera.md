# session-camera — VIBE_Camera_Update dispatcher + SessionCamera adapter

The in-city camera made REAL and consumable by the play session: the per-frame
camera dispatcher of `gilde.exe` reconstructed 1:1, plus a thin session-facing
adapter that drives the reconstructed state machine from SDL-shim input and
exposes the eye/zoom the city renderer consumes.

Files:
- `src/render/camera_update_recon.h` / `.cpp` — the dispatcher reconstruction
- `src/render/camera_edge_scroll.h` / `.cpp` — VIBE_Camera_EdgeScroll @0x4b2c34
  (the edge view-snap / camera RE-ATTACH machine — was the named gap)
- `src/play/session_camera.h` / `.cpp` — the `play::SessionCamera` adapter
- `tests/unit/camera_update_recon_test.cpp` (16 tests, 71 checks — all pass)
- `tests/unit/camera_edge_scroll_test.cpp` (14 tests, 75 checks — all pass)
- `tests/unit/session_camera_test.cpp` (17 tests, 71 checks — all pass)

## Reconstructed 1:1

| addr | name | entry point | notes |
|------|------|-------------|-------|
| 0x4b4c68 | VIBE_Camera_Update | `render::Camera_Update` | the per-frame camera dispatcher: no-camera/modal early path (zeroes `dword_631628`, one-shot screen-edge-box init `dword_62D0C4..D4` from packed `dword_69FFBC`, cursor write), gated main path (`dword_11BC24C`/`dword_633908==-1`), UpdatePan when not dragging (`!dword_672238`), UpdateMovement unless frozen (`dword_11BC2D0 & 0x8000`), terrain clamp (`byte_6316D8 && !byte_671D6F`), family-record save on pan (strict gate `631610==631618 && !631744 && !631748` — stricter than the RotateView history gate), unconditional pos/world history mirrors `dword_11BC2D4..DC` / `2E4..EC`, returns `dword_631628` |
| 0x40da48 | VIBE_Coord_ConvertY | `render::Camera_CursorCoordWrite` | despite the inherited IDB name this is a CURSOR COORDINATE WRITE: packs (x,y) into `dword_672174` / `dword_6721C4` / `dword_672210`, and calls Win32 `SetCursorPos` only when the edge box is uninitialized (`!dword_62D0D4`). `SetCursorPos` -> `CameraUpdateHooks::setCursorPos` (rule 4; inert default). `dword_6721C4` has no consumer in the reconstructed tree and is not modeled. NOTE: the "trunc-toward-zero ConvertY" reading in camera_recon2's UpdateMovement notes refers to VIBE_Coord_ConvertX @0x5c6b08-style chops; the 0x40da48 body is the cursor write above (decompile verified) |
| 0x4b2c34 | VIBE_Camera_EdgeScroll | `render::Camera_EdgeScroll` (camera_edge_scroll.\*) | NOT the smooth edge pan (that is UpdatePan @0x4b365c) — the edge-triggered VIEW-SNAP machine: steps the 2-axis edge state `dword_6316CC`/`dword_6316D0` (clamped [-1,1], stepping one axis zeroes the other), selects an edge view block off the camera node ((0,-1) top +180 / (0,+1) bottom +204 / (-1,0) left +228 / (+1,0) right +252, each pos[3]+rot[3]) or, at (0,0), the node's CURRENT world pose (+92/+144) with NO tolerance gate; an edge view within 0.2 of the 16 zero bytes @0x4AD1C4 is "unset" -> bail. Commit (loc_4B2D44): history mirrors `dword_11BC2D4..DC`/`2E4..EC`, `VIBE_Anim_FreeObjAnimData` @0x5cec00 (frees node+464; preserves ecx/edx — push/pop verified, so the xor'ed ecx reaches the stores), **clears `dword_62D4E4`/`dword_62D4E8` @0x4b2d95/9b (the RE-ATTACH)**, 3D listener (@0x4262a0, dist=`dword_6316C8`, kind 8), latch `dword_631DE0=1` until the cursor re-enters the inner box. Returns: latched/vertical early-outs = bottom-1; horizontal early-outs = cursor x; tolerance bail = 1; commit = listener result. Live callers: combat loops 0x48c60f/0x48c679/0x4905e9 + `VIBE_Hud_HandleMouseClick` @0x4bc4b1 (adapter stands in per-frame). The (cc!=0 && d0!=0) commit of uninitialized stack @0x4b2ed4->loc_4B2D44 is unreachable under the clamp/zeroing invariants; reproduced as a zero-view commit (documented) |

### Calling convention (recovered)
Hex-Rays shows `__thiscall VIBE_Camera_Update(void* this)`, but the disasm
never loads ecx — it only forwards it into the
`VIBE_Camera_ClampToTerrainHeight` call at 0x4b4d25. Both live call sites
(below) issue a bare `call` with ecx as an indeterminate leftover register
(same situation as the AnchorToTerrain first arg inside UpdateMovement).
Surfaced as the explicit `thisXLeftover` argument; the reconstructed call
sites pass 0 (inert with the headless terrain hook).

### Verified callers (xrefs_to 0x4b4c68)
| caller | addr | call site | status |
|--------|------|-----------|--------|
| VIBE_Scene_RunMainFrameLoop | 0x50f0c0 | 0x50f1ea (the per-frame call) | caller not yet reconstructed |
| VIBE_Building_LoadAndAlignGebaeudeModel | 0x50d01c | 0x50da2e | caller not yet reconstructed |
| scene_recon2_orchestrator | — | `SceneRecon2Hooks::CameraUpdate` (0x4b4c68 slot) | hook slot exists; wireable now |

## Callee routing (rule 13)

| addr | callee | routing |
|------|--------|---------|
| 0x4b41a8 | VIBE_Camera_UpdateMovement | `Camera_UpdateMovement` (camera_recon2) — called DIRECTLY |
| 0x4b2a0c | VIBE_Camera_ClampToTerrainHeight | `Camera_ClampToTerrainHeight` (camera_recon) — called DIRECTLY |
| 0x4b365c | VIBE_Camera_UpdatePan | `CameraUpdateHooks::updatePan` (inert default: 0). The pan core IS reconstructed (`play::CameraUpdatePan`, camera_controls) but lives in the play layer's `CameraControl` model; the render-layer dispatcher cannot depend on src/play, so the adapter wires the hook to the real core |
| 0x58c408 | VIBE_Person_GetFamilyRecord | `Camera2Hooks::personGetFamilyRecord` (inert default: nullptr). The dispatcher's record write (`rec[33..35]=pos`, `rec[37..39]=world`, `rec[32]=dword_6316E0`) is reproduced on the returned pointer; the row argument `&word_12CE910[268*word_63CC5C]` is owned by the hook |
| 0x40da48 | VIBE_Coord_ConvertY | `Camera_CursorCoordWrite` (this module, see above) |

## play::SessionCamera (the adapter)

```cpp
struct CameraPose {                // the wave-2 / 3D-view consumption struct
    float eyeX, eyeY, eyeZ;        // node +76/+80/+84 (== world +92/96/100)
    float rotX, rotY, rotZ;        // node euler +132/+136/+140 (== world +144..)
};                                 // view basis = MatrixFromEuler(-rot) @0x5af50c

struct SessionCamera {
    void Init(float eyeX, float eyeZ, int fbW, int fbH);
    void Frame(const shim::MouseState& ms, bool keyLeft, bool keyRight,
               bool keyUp, bool keyDown, float wheelDelta, float dtMs);
    float eyeX() const;            // camera node +76
    float eyeZ() const;            // camera node +84
    float zoom() const;            // flt_6316DC zoom fraction [0,1]
    float pixelsPerUnit() const;   // 1.0f + zoom (the city_frame.cpp mapping)
    CameraPose pose() const;       // full eye + rotation for the real-3D view
    void BindTerrain(const render::Heightmap* hm);  // REAL terrain follow
    int scrollSpeed = 50;          // dword_1233564 ([Game] scroll_speed INI option)
    int edgeMargin  = 8;           // edge band (the original box inset, width-8)
    // + the reconstructed state structs, exposed for tests/advanced wiring
};
```

### Camera-node field layout (IDA-verified, the 3D-view contract)
| offset | field | written by | read by |
|--------|-------|-----------|---------|
| +76/+80/+84 | LOCAL translation (the eye) | `VIBE_Object_SetPosition` @0x5af38c (dword lanes [19..21]) | every camera mover; the terrain sampler's world arg (eax at 0x4b292a/0x4b2a2b) |
| +92/+96/+100 | WORLD translation | scene-graph propagation (== local for the parent-less camera node; adapter mirrors per frame) | EdgeScroll center commit @0x4b2d0b; the ChooseCity MegaCam eye |
| +132/+136/+140 | LOCAL rotation EULER (pitch/yaw/roll) | `VIBE_Object_SetWorldTranslation` @0x5af50c (historical name; it is the euler setter) | AnchorToTerrain pitch, UpdateMovement pan-branch yaw, RotateView |
| +144/+148/+152 | WORLD rotation euler | scene-graph propagation (== local, parent-less) | EdgeScroll center commit; MegaCam basis = `MatrixFromEuler(-(node+144))` |
| +180/+204/+228/+252 | edge view blocks (pos[3]+rot[3]) top/bottom/left/right | scene/cutscene setup (producer not yet in tree) | EdgeScroll @0x4b2c34; combat scroll arrows @0x487b2c |
| +396 | 4x4 world basis | `MatrixFromEuler(-euler)` inside 0x5af50c when type byte `*(node+533) == 3` (camera) | RotateView/UpdateMovement basis reads |

So the 3D view consumes `pose()`: eye = (eyeX,eyeY,eyeZ), view basis =
`MatrixFromEuler(-(rotX,rotY,rotZ))` — exactly the engine's camera-node math.
rotX = pitch (zoom-driven), rotY = yaw (left+right drag / RotateView),
rotZ = roll (never written by the movers).

### Terrain follow (BindTerrain)
`CameraHooks::terrainHeight` now carries the REAL prototype recovered from the
call sites: `f32 (*)(i32 a1, i32 a2, const f32* world, void* user)` — a1/a2 are
the original's dead leftover registers, `world` is the camera node position
triple the original passes in eax (node+76). `BindTerrain(hm)` wires the hook
to the reconstructed `render::AverageAreaHeight` @0x427468 (heightmap.cpp, the
8x8 box average over `VIBE_Heightmap_WorldToTileWithHeight` @0x5c6644) and
turns on the `byte_6316D8` clamp gate so the dispatcher runs
`Camera_ClampToTerrainHeight` @0x4b2a0c every frame; the wheel-zoom
`AnchorToTerrain` @0x4b2900 sees the same hook (`Camera_UpdateMovement` now
forwards the dispatcher's `CameraHooks` to the anchor call @0x4b46b1, as the
original call chain does).

A THIN BRIDGE, not a reimplementation:
- `Init` runs the dispatcher once BEFORE the camera node exists (the original
  boot order), so the REAL early path initializes the screen-edge box; then
  materializes the node and anchors it with the REAL `Camera_AnchorToTerrain`
  @0x4b2900 at zoom 0 (eye height 450 headless).
- `Frame` translates shim input into the original's input-global model
  (mouse -> `unk_67220E`/`dword_672210` + the 672170/672174 view mirrors;
  right button -> `dword_672238` drag; left button -> `dword_672220`;
  wheel notches -> the `dword_672254`/`dword_672250` accumulator pair;
  `scrollSpeed` -> `dword_1233564`), then calls `render::Camera_Update`.
- the `updatePan` hook resolves arrows/edges with the REAL
  `play::ResolveEdgeScroll` and runs the REAL `play::CameraUpdatePan`
  (0x4b365c core: velocity kick/ramp/decay, zoom-scaled speed, 1.1 apply),
  committing the eye into the camera node (the `VIBE_Object_SetPosition` role).
- mouse-drag pan/rotate and wheel zoom run through the REAL
  `Camera_UpdateMovement` @0x4b41a8 (rotate branch verified against the exact
  `mouseDelta * scroll_speed * 6/99` constant; wheel branch ends in the REAL
  `Camera_AnchorToTerrain`, which moves the eye height/pitch and clamps the
  zoom fraction to [0,1]).

`pixelsPerUnit()` matches `city_frame.cpp` (`opt.pixelsPerUnit = 1.0f +
camera.zoom`), so `RealCityRenderer::Options{eyeX, eyeZ, pixelsPerUnit}` can
be fed directly from the accessors.

## Deferred leaves / named gaps (rule 8)
- ~~VIBE_Camera_EdgeScroll @0x4b2c34~~ — **RECONSTRUCTED**
  (`src/render/camera_edge_scroll.*`). The adapter's manual flag-clear
  stand-in is GONE: `Frame` step 4 now calls the real `Camera_EdgeScroll`
  per frame (the combat-loop cadence; the city caller is
  `VIBE_Hud_HandleMouseClick` @0x4bc4b1). Behavior change vs the stand-in
  (now matching the original): after a drag release the camera STAYS
  detached (mouse-move/wheel dead, pan gated) until the cursor steps the
  edge state back to center — e.g. touches a boundary row (`y == top` or
  `y == bottom-1+1`) or the opposite edge — which fires the center commit
  and clears `dword_62D4E4`/`E8` @0x4b2d95.
  Remaining sub-gap: the PRODUCER of the node's edge view blocks
  (+180/+204/+228/+252) is not in the tree (scene/cutscene camera setup);
  with zero blocks the edge snaps bail on the 0.2 tolerance gate exactly
  like the original with unset views — only the center re-attach commits.
- **UpdatePan detach gate** — the original's entry gate @0x4b3685/0x4b3692
  (`dword_62D4E8 || dword_62D4E4` -> return 0, loc_4B3B7D tail) is modeled in
  the adapter's `UpdatePanThunk` (the play-layer pan core itself does not
  model the globals). The `word_62D310 = cx` store in that tail has no
  consumer in the reconstructed tree and is not modeled.
- **`dword_69FFBC` producer** — the packed screen-extent global the edge box
  is built from is fed by the (unreconstructed) display-mode setup; the
  adapter packs `(fbW<<16)|fbH` to land the same box (`width-8`, `height`,
  `0`, `0`).
- **Family / 3D-sound leaves** — inherited inert-default hooks from
  camera_recon/camera_recon2 (`personGetFamilyRecord` @0x58c408,
  `sound3dSetListener` @0x4262a0, `animFree` @0x5cec00, etc.); see
  camera-recon2-movers.md. The eye still moves on pan/zoom with these inert
  (verified by tests).
- **Terrain** — `terrainHeight` @0x427468 is now BINDABLE to the real
  reconstruction (`render::AverageAreaHeight`, heightmap.cpp) via
  `SessionCamera::BindTerrain`; headless/unbound it stays inert (0) and the
  `byte_6316D8` clamp gate stays off. The live heightmap producer
  (`VIBE_Heightmap_BuildTerrainMesh` @0x5c5610 full builder / the city
  loader) is the remaining wiring, owned by the terrain/session waves.
  `Camera2Hooks::terrainHeight` (the ZoomReset/ZoomOut paths @0x4b5250/
  0x4b5974, not reachable from the session loop) keeps the old positionless
  signature until those callers join the live tree.
- **`scrollSpeed` default** — `dword_1233564` is the `[Game] scroll_speed`
  INI option (read at 0x56bcd3 in VIBE_Config_ReadGfxAndSoundSettings
  @0x56b834 with the caller's ebx as the INI default, range 0..99 per the
  `* 6.0 * 1/99` scaling). Not a binary constant; exposed as a knob
  (midpoint 50) until the config reader joins the tree.
- **`dword_6721C4`** — the third packed cursor mirror written by 0x40da48 has
  no consumer in the reconstructed tree; not modeled.
- **Return value of 0x40da48 on the SetCursorPos path** — the original
  returns the Win32 BOOL; the shim hook is void and the reconstruction
  returns x. The only reconstructed caller (the dispatcher) discards it.

## Tests
- `camera_update_recon_test` — 16 tests, 71 checks, all pass. Golden vectors
  from the decompile/disasm: early-path box init (packed-word split, sign
  extension, one-shot gate), modal gates returning the STALE `dword_631628`,
  UpdatePan drag gate, UpdateMovement freeze bit, terrain-clamp gate
  (saturated +10 step), family-record write/gate variants (incl. the strict
  631748 gate), unconditional history mirrors, cursor-write semantics
  (mirrors, SetCursorPos gating, i16 truncation).
- `camera_edge_scroll_test` — 14 tests, 75 checks, all pass. Golden vectors
  from the 0x4b2c34 decompile: inner-box inertness; per-edge stepping with
  [-1,1] clamps and cross-axis zeroing; the 0.2 unset-view tolerance bail
  (return 1); the full commit (history-mirror bits, animFree call, the
  0x4b2d95 `62D4E4`/`62D4E8` re-attach on BOTH global mirrors, listener args
  `(pos, dword_6316C8, rot, 8)`, latch, listener-result return); latch
  block/re-arm; the no-tolerance CENTER commit (node+92/+144); the boundary
  rows (no-step center re-attach / vertical-state re-commit); per-path return
  values.
- `session_camera_test` — 17 tests, 71 checks, all pass. Previous 10 (Init
  anchor + edge box; idle; arrow pan ramp/decay; Z pan both ways; edge-pan;
  wheel zoom 450->565 / pixelsPerUnit 1.1 / [0,1] clamps; zoom-scaled pan;
  right-drag `mouseDelta * scrollSpeed * 6/99`; scroll_speed 0) — the
  right-drag test now proves the REAL detach/re-attach (wheel dead while
  detached, boundary-row EdgeScroll re-attach, latch re-arm) — plus 7 new:
  detach gates pan until the EdgeScroll re-attach (the 0x4b3685 gate);
  `pose()` exposes eye + rotation (pitch = `baseAngle + span*zoom`); ROTATE
  INPUT mutates the exposed rotation (left+right drag yaw, golden
  `-dx * 0.0035` against `dword_11BC33C = trunc((1-zoom)*666.667)`);
  right-only drag keeps rotation; terrain bind anchors the eye on a real
  Heightmap (210 + 450 = 660); the per-frame clamp eases a displaced eye back
  (dy/15 steps, 5.0 deadzone stop); wheel zoom follows bound terrain
  (210 + 450 + 1150*0.1 = 775).
- Regression: `camera_recon_test` (40), `camera_recon2_test` (34),
  `camera_controls_test` (34), `render_terrain_test` (135, heightmap suite)
  still pass.

## Wiring
- `Camera_Update` is the reconstructed body for the existing
  `SceneRecon2Hooks::CameraUpdate` slot (0x4b4c68) in
  `src/play/scene_recon2_orchestrator.h` — connect when that orchestrator's
  frame loop is bound to a live session.
- camera-recon2-movers.md's "VIBE_Camera_Update 0x4b4c68 PENDING" row is now
  satisfied: UpdateMovement @0x4b41a8 is dispatched from its real caller.
- **Session integration lands in `sdl_session.cpp` wave 2** (that file is
  owned by another agent this wave): replace the ad-hoc
  `CameraControl`/`CameraUpdatePan` block with one `SessionCamera` and feed
  `RealCityRenderer::Options{eyeX(), eyeZ(), pixelsPerUnit()}` per frame.
- **The real-3D city view consumes `SessionCamera::pose()`** —
  `CameraPose { eyeX, eyeY, eyeZ, rotX, rotY, rotZ }`: eye is the camera-node
  position (+76/+80/+84 == world +92/96/100), rot is the node rotation euler
  (+132/+136/+140 == world +144/148/152); build the engine view basis as
  `MatrixFromEuler(-rot)` (the 0x5af50c camera-type path), i.e. the exact
  MegaCam convention ChooseCity already uses. When the city heightmap is
  loaded, call `BindTerrain(&hm)` so eye height follows the real terrain.
- `Camera_EdgeScroll` is also the body for the combat frame loops'
  0x48c60f/0x48c679/0x4905e9 call sites (`src/sim/combat_loop.cpp` lists it
  as "skipped") and `VIBE_Hud_HandleMouseClick` @0x4bc4b1 — wire those when
  their owners pick them up.
