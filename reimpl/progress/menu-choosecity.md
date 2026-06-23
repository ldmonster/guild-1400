# Main menu + New Game (ChooseCity 3D scene)

Reconstruction of the boot → main menu → **New Game** funnel and its 3D city-select
scene, following the call tree from `VIBE_Menu_RunMainMenu`.

## Call-tree position (rule 7)
```
VIBE_GameLogic_MainEntryAndShutdown 0x534bbc
└─ VIBE_Menu_RunMainMenu            0x529d08   main menu loop  (native bridge: src/play/native_main_menu.cpp)
   └─ VIBE_Menu_EnterChooseCity     0x52ee38   New Game funnel (gui::Menu_EnterChooseCity)
      └─ VIBE_Menu_RunChooseCity    0x52e6d8   the 3D scene    (gui::Menu_RunChooseCity + native bridge WIP)
         ├─ VIBE_Scene_LoadFromStream     0x5e7e38   .ed3 loader        (render/scene_load + play/scene_view parse)
         ├─ VIBE_WorldIo_ReadObject       0x5e67c8   object-body grammar (play/scene_view ParseSceneObjects)
         ├─ VIBE_Object_Spawn             0x5b054c   kind→type rule      (replicated in scene_view)
         ├─ VIBE_Mesh_LoadBgfFile         0x5d2348   .bgf model loader   (render/bgf_loader — fast-chunk path)
         │  └─ VIBE_Vfs_FindChunkStart    0x5f86fc   FIXED (token-skip + magic 0xFAB50005)
         ├─ VIBE_Cutscene_LoadAndRunScript 0x4aa01c  A_Stadtwahl.esc     (camera flight — see Camera)
         ├─ VIBE_Render_SetupViewTransform 0x5af5f8  → D3D view/proj      (replaced by perspective renderer, rule 3)
         ├─ VIBE_DragCursor_RenderMouse   0x41fc58   hand cursor + click  (native_main_menu)
         └─ VIBE_Fade_Register/Update     0x41f0e8/0x41f1cc  menu fade-in
```

## Done + wired (rules 11, 13)
- **BGF fast-chunk loader** (`render/bgf_loader`): fixed `VIBE_Vfs_FindChunkStart` @0x5f86fc —
  the token loop now read+seeks for any non-`-`/`+` token, magic = `0xFAB50005` (`-88801275`).
  Real models load (`sp_STADTTURM`: 216 v / 372 p / 5 mats). Tests: `render_mesh_load_e2e_test`,
  `render_mesh_asset_test`, `object_mesh_render_e2e_test` (now loads real `.bgf`).
- **Perspective renderer** (`play/scene_persp_render`): host-side Vulkan-bound perspective
  raster (rule 3) — look-at camera, z-buffer, flat shade. Test: `scene_persp_render_e2e_test`.
- **Scene-object parser + compositor** (`play/scene_view`): `ParseSceneObjects` reconstructs
  the `VIBE_WorldIo_ReadObject` @0x5e67c8 body grammar for scene tag `0x3A6C00BB`; renders the
  parsed mesh objects through the camera; `PlaceMeshAtMarkers` spawns `sp_STADTTURM` at the
  `dummy_<CITY>` markers (host-side `VIBE_Map_SpawnCityPointMarker` analogue). The real
  `Menu/ChooseCity.ed3` parses (47 objects: guild Secretariat interior + map table +
  HANNOVER/BERLIN/AUGSBURG/DRESDEN/KOELN markers). Test: `scene_view_e2e_test` (21 checks).
- **Real camera** (`CameraFromDummy`): `A_Stadtwahl.esc` = `CameraFlightEnhanced(1500,"dummy_A1","dummy_A2",…)`;
  camera seats at the waypoint, aimed by the dummy's euler (forward = `Rz·Ry·Rx·+X`). Backface
  culling so the camera-inside-room renders.
- **Textures** (`Textures.BIN` via `RealTextureSource`): per-material resolve + per-corner UV
  (`uv0`=U,`uv1`=V) + perspective-correct sampling. The office renders with its real surfaces.
- **Menu music** (libmpg123), **fade-in** (90-tick black quad), **hand cursor** (`_MOUSE_CURSOR`
  full-bitmap decode + 1px press), centered 3-slice buttons — all in `native_main_menu.cpp`.

## Done + wired (rules 11, 13) — cont.
- **Exact engine projection** (`render/d3_projection`): faithful reconstruction of
  `VIBE_Render_SetProjectionTransform` @0x5de3e4 (the math half; the DDraw SetViewport/
  SetTransform calls are the swapped GPU API, rule 3). Recovered verbatim: the D3DVIEWPORT2
  clip window `dvClipX=-1, dvClipWidth=2, dvClipY=H/W, dvClipHeight=(H/W)*flt_62A6F4` with
  `flt_62A6F4`@0x62A6F4 `= 2.0` (read from the binary), and the projection matrix z terms
  `_33=far/(far-near)=Q, _34=1, _44=near` ⇒ `w=z+near`. Result: a fixed ~90° **horizontal**
  FOV with the vertical scaled by `aspect=H/W` (⇒ square pixels), screen centre `W*0.5,H*0.5`
  (matches the picking centre `flt_13FCD18/flt_13FCD10`, factor `flt_6280DC`@0x6280DC `=0.5`).
  Wired through `PerspCamera.engineProjection` (set by `CameraFromDummy` — the real
  ChooseCity path) into both `scene_persp_render` and `scene_view` rasterisers. The real
  `Menu/ChooseCity.ed3` renders 17 meshes / 644 tris / 73 816 px through it. Tests:
  `d3_projection_test` (7 golden-vector cases), exercised e2e by `scene_view_e2e_test`.
- **3D tower picking** (`play::PickNearestObject` / `IsCityMarkerName` in `play/scene_view`):
  faithful host reconstruction of `VIBE_Pick_FindNearestObjectAt` @0x5b5a38 +
  `VIBE_Pick_TestObjectAtPoint` @0x5b5938 + `VIBE_Object_ComputeBoundingRadius` @0x5b2cf8. Each
  mesh object's view-space bbox centre (avg of 8 corners, `flt_6282F4=0.125`) is projected with
  the engine pick scale `a7=fbW/2` (`flt_13FCD0C`, Y flipped by `flt_13FCAF8=-a7`, centre
  `fbW/2,fbH/2`); the cursor selects the nearest object whose projected radius contains it. The
  `"stadt_"` city filter (`VIBE_Util_StrncmpN(picked,"stadt_",6)`) is `IsCityMarkerName`. On the
  real ChooseCity scene all 5 city towers pick a city, 4/5 resolve to the exact tower under the
  cursor. Test: `scene_view_e2e_test::PickCityTower` (cursor→tower, empty→none, determinism, name
  filter).
- **Native bridge** (`play::SceneChooseCityHooks`, `play/choosecity_scene`): a concrete
  `gui::ChooseCityHooks` that drives the byte-faithful `gui::Menu_RunChooseCity` @0x52e6d8 over
  the REAL 3D scene — enumerates the real `gamedata/Cities/*.CTY` (`VIBE_SaveBrowser_EnumerateSaveFiles`
  @0x569530 via `IFileSystem::listDir`), spawns a `stadt_<city>` `sp_STADTTURM` tower at each city
  dummy (`VIBE_Map_SpawnCityPointMarker` analogue @0x52e2d0), and resolves the per-frame pick
  through `play::PickNearestObject`. Per-frame input (frame-loop gate / cursor / confirm-cancel
  edges) is injected so it runs headless (scripted) and natively (SDL) alike. So the REAL
  RunChooseCity loop now runs the REAL pick→hover→info→confirm flow end to end. Test:
  `choosecity_scene_itest` — scripts a cursor onto the AUGSBURG tower, hovers it (info rendered),
  confirms → chains `ChooseCharacterIntroVariant`→`RunChooseHistory` (result 1); Esc cancels.
- **Live native 3D New-Game front — WIRED** (`play::RunCityScreen3D`, `play/sdl_city_screen3d`):
  `native_main_menu.cpp`'s `EnterChooseCity` now renders the REAL `Menu/ChooseCity.ed3` scene each
  frame (engine projection), places a `stadt_<city>` `sp_STADTTURM` tower per shipped city, picks the
  tower under the cursor via `play::PickNearestObject` (`VIBE_Pick_FindNearestObjectAt`), overlays the
  hovered city name, and confirms the clicked city (double-click / Enter; ESC / window cancels).
  Reuses `CityScreenConfig/Result`; transparently falls back to the 2D `RunCityScreen` when the `.ed3`
  assets are absent. So **New Game now shows the 3D map-table city pick.** Test:
  `sdl_city_screen3d_itest` (click-tower → confirm AUGSBURG; ESC → cancel). Remaining nicety: the real
  flown cutscene camera (currently a fixed map-table framing) + the `Menu\CHOOSECITY` info-form text.
- **Camera flight — DONE + tested** (`render/object_anim` + `play::CameraFlightAt`):
  `Startmenu/A_Stadtwahl.esc` = `CameraFlightEnhanced(1500,"dummy_A1","dummy_A2",…)` →
  `VIBE_Camera_CmdCameraFlight` @0x43f528 → setup `0x428a84` builds an object-anim on the camera
  node through the dummy waypoints (segment = ticks/N) via `VIBE_Anim_CreateObjectAnim` @0x5cef14.
  Reconstructed the keyframe interpolation: `ComputeFrameTangents` @0x5ccf70 (Catmull-Rom tangent
  `0.5·((Pᵢ−Pᵢ₋₁)/durᵢ₋₁ + (Pᵢ₊₁−Pᵢ)/durᵢ)`, `flt_628D4C=0.5`, stored duration-scaled as the
  segment's M0/M1) + `BuildFrameTangents` @0x5cec40 (interior frames + zeroed ease-in/out
  boundaries) + a cubic-Hermite sampler, reusing `render::AdvanceFrameIndex`. `CameraFlightAt`
  interpolates the camera position **and** euler through the waypoints and seats the camera
  (forward = Rz·Ry·Rx·+X). On the real scene the camera flies dummy_A1 (-162.9,57.1,33.4) →
  dummy_A2 (-144.9,61,43.3) smoothly + monotonically. Tests: `object_anim_test` (waypoint-passing,
  tangent values, clamp, smoothness — 14 checks), `camera_flight_e2e_test` (real A1→A2 motion).
  The byte-exact in-engine evaluator is the 6.6 KB `VIBE_Anim_UpdateSkeletonPose` @0x5cd1d8
  (skeleton + object anims together) — this is the faithful-equivalent of its object path (exact
  keyframes/tangents/timing, canonical Hermite blend). **Not yet wired as the live city-screen
  intro**: A1/A2 are dim office views and the flight ends at A2, not the map-table pick framing —
  driving it live is a UX decision (intro cutscene → settle to the table cam), deferred.

## Scene infra generalizes — CharCreate village renders (next funnel step)
The reconstructed scene stack (engine projection, `BuildSceneCamera` = the MegaCam-object
camera, `PointToBoneLocalSpace` world→view, `PickNearestObject`, baked 7-band lighting) is
**general, not ChooseCity-specific**. Validated on the char-creation scene
`Cutscenes/Spielerauswahl.ed3` (the player-selection village `RunChooseCharacter` @0x52bcd4 uses,
Universe slot 1): it parses (156 objects / 104 meshes), carries a `MegaCam` object + all 9 ancestry
actor dummies (`dummy_NACHT_UND_NEBEL_*`, `_HANDWERKSKUNST_*`, `_KAMPF_MANN`, `_VERHANDELN_*`,
`_RHETORIK_*` → professions 2/1/3/0/4), and renders from its real MegaCam (eye=(985,76,876),
band-4 lighting, 54 meshes — the Fachwerk houses + selection tent + campfire). Test:
`charcreate_scene_e2e_test` (13 checks). `RunChooseCharacter` decompiled: 9 `CreateMenuDummyActor`
actors at those dummies; click an actor → its profession/gender into the dynasty slots
`dword_122F258`; camera flies to the picked ancestor; chains Talent→Profession→PreviewScene. The
remaining piece for a full 3D CharCreate is the **animated character actors** (the
`VIBE_Character_*` skinned-mesh/.baf system) — the gui selection logic is already reconstructed
(`gui/charcreate`), and the scene/camera/pick/lighting are now proven to work for it.

## Camera fidelity FIX (was not 1:1)
The earlier `CameraFromDummy` used the dummy's **+76 position** as the eye and a
forward-from-euler(+92), and `RunCityScreen3D`/the bridge papered over the resulting
mis-framing with a hand-invented "map-table" camera + an invented on-screen title — **not
1:1**. Corrected against `VIBE_Camera_SetToDummy` @0x43fbbc: the camera EYE is the camera
dummy's **+92** field (`VIBE_Object_SetPosition(cam, dummy+92)`) and the look/world-translation
is **+144** (`VIBE_Object_SetWorldTranslation(cam, dummy+144)`) — *not* +76/euler. The flight
setup @0x428a84 reads the same +92/+144 tracks. `SceneObjectInst` now captures +144 (`camLook`);
`CameraFromDummy` seats eye=+92 looking at +144; `CameraFlightAt` interpolates both tracks.
With the real eye (+92), `dummy_A2` sits at z≈271 and frames the map-table city markers (4/5 on
screen; A1 3/5) — so `RunCityScreen3D` + the bridge now use the **real cutscene camera**
(`CameraFromDummy("dummy_A2")`), the invented table cam + title overlay are gone, and only the
hovered city name remains as the info stand-in. Tests updated to the real camera
(`camera_flight_e2e`, `choosecity_scene_itest`, `sdl_city_screen3d_itest` pick a city that
projects on-screen). **Camera — EXACT source + eye + world→view; orientation still approximate.** Decompiled the
world→view `VIBE_Transform_PointToBoneLocalSpace` @0x5c8c40:
`view = R_cam·(world − cam[+76] − cam[+120])`. Decompiled `VIBE_Scene_LoadFromStream` @0x5e7e38:
the active camera is the scene OBJECT named **"MegaCam"** (type 3; found by name in the object
tree, then `SetActiveCamera` @0x5b0bf0, positioned `SetPosition(cam, cam+92)` /
`SetWorldTranslation(cam, cam+144)`) — verified present in `ChooseCity.ed3` at +92=(17.4,62.5,322.5)
(= dummy_A0). The header `camPos=(17,10,40)/camTarget=(0,0,0)` are read into `flt_64A074/84` (the
**lighting** seed, overwritten by `BlendBandLighting`) — **NOT** the camera (my earlier
header-orientation claim was wrong). The cutscene flight then repositions this camera object to the
A-dummies via the same +92/+144 fields, so **eye = dummy+92 + dummy+144 is EXACT**, and the
orientation is the **MegaCam object's rotation matrix (+396)** (never changed by the flight).
**RESOLVED — the camera is now byte-exact.** `VIBE_Object_SetWorldTranslation` @0x5af50c for a
type-3 (camera) node sets `node+132 = src` then `node+396 = VIBE_Math_MatrixFromEuler(-(node+132))`
@0x5cb1bc. `SetToDummy` feeds `node+76 ← dummy+92` (eye) and `node+132 ← dummy+144`, so the camera
euler is **−(dummy+144), in RADIANS** (the earlier "look-at +144" / degrees readings were wrong).
For `dummy_A2`, +144=(−0.9, 3.1, 0) → euler (0.9, −3.1, 0) rad: the `ry=−3.1≈−π` flips forward to
−Z and `ex=0.9` pitches down, giving **forward = (0, −0.77, −0.64)** — the camera looks **down at the
map table**. `BuildSceneCamera` now reproduces this exactly (eye = dummy+92; orientation =
`MatrixFromEuler(−(dummy+144))` via `render/scene_transform`), and **all 5 city markers are on
screen** (the real pick view — the carved Secretariat map table). Reconstructed + tested:
`MatrixFromEuler`, `PointToBoneLocalSpace` world→view, `AccumulateBoneMatrices` (parent chain),
`PointThroughBoneChain` — `scene_transform_test` (11 checks). The whole ChooseCity camera/pick is
now 1:1 from the real `.ed3` fields, no invented framing. (Dim view + low markers match the data; `A_Stadtwahl` = "flight 1 of 3";
real markers are animated glowing `STADTPUNKT_ANM_K.baf` points, here static towers.)

## Lighting (the 7-band rig) — in progress (faithful path chosen)
The `.ed3` rig is a **7-band time-of-day** system: each band carries an AMBIENT colour
(`SceneLight::pos` → `flt_13FD1B8/BC/C0`) + a DIRECTIONAL "sun" colour (`SceneLight::color`
→ `flt_13FD1C4/C8/CC`) + 6 keyframes. `VIBE_SkyColor_BlendBandLighting` @0x5b85e4 (which
`RunChooseCity` calls as `BlendBandLighting(0,0,1.0)`) blends band→band+1 and publishes the
active ambient (`flt_64A074/78/7C` + luma `flt_64A070`) + directional (`flt_64A084/8/C`).
`VIBE_Light_BuildObjectCache` @0x5c8218 then bakes per-vertex Gouraud colours: seed ambient,
accumulate scene light-nodes (`VIBE_Light_ApplyToCachedVertices` @0x5c6f90 — point:
`colour·(k/dist²)·LUT[N·L]`, directional type-7: `colour·intensity·LUT[N·L]`), reduce to a
shade index.
- **Done + tested**: the band blend `render::BlendBandLighting` @0x5b85e4 already existed in
  `render/sky` (reused, not redefined — covered by `render_effects_e2e_test`). New this pass
  (`render/light`): `BuildFalloffLUT` (`VIBE_Light_InitFalloffTable` @0x5c88f8 =
  `1 − (2/π)·acos(−k/1023)` — recovered from the x87 disasm, no longer the deferred acos blocker;
  `light_band_test`). `play::ComputeSceneAmbient` parses the real rig + feeds band 0 through the
  existing blend; the real `Menu/ChooseCity.ed3` band-0 ambient is the dim blue **(17,10,40)**,
  luma 15.4 (band 0 has no sun). Test: `choosecity_flow_render_e2e::RealRigBand0Ambient`.
- **Per-vertex baked lighting — DONE + wired** (`render/light` + `play/scene_view`):
  reconstructed `VIBE_Light_ApplyToCachedVertices` @0x5c6f90 as golden-tested primitives —
  `AccumulatePointLight` (`colour·(intensity·10/(rangeParam·dist²))·LUT[N·L]`, range-gated,
  front-facing only) and `AccumulateDirectionalLight` (type-7 sun, `colour·intensity·0.001·LUT[N·L]`).
  **Fixed a sign bug in `BuildFalloffLUT`**: `flt_628CB4 = +1/1024` (read from the binary), so the LUT
  is `1 − (2/π)·acos(k/1024)` — a rising [0,1] softened-Lambert (the earlier negated arg inverted it).
  RE'd the real `.ed3` light-node fields from `VIBE_WorldIo_ReadObject` @0x5e67c8 (colour +92, pos +76,
  dir +132, range/intensity/rangeParam +144/+148/+152) and parse them into `SceneObjectInst`
  (`lightColor/lightDir/lightParam`). The real ChooseCity lights: warm candles `rLicht_KERZE`
  (255,170,0), blue window (40,119,255), bluish sun `pLicht`. `RenderSceneObjects` now has an opt-in
  baked path (`PerspRenderOptions::bakedLighting` + `ambientRGB`): per-object world-space vertex
  positions + smoothed normals → per-vertex `accum = ambient + Σ lights`, hue-preserving clamp,
  Gouraud-interpolated to modulate the texel. Wired into `RunCityScreen3D` (the live New-Game 3D pick)
  with the rig band-0 ambient. Real scene: flat avg (75,78,45) → baked (12.6,25.7,31.7) — the dim,
  cool, candle-lit secretariat. Tests: `light_band_test` (LUT + point/directional accum, 14 checks),
  `choosecity_flow_render_e2e::BakedLightingIsDimAndTinted`.
- **Remaining nicety:** the animated-light keyframe track (the `.ed3` per-light 6/7 keyframes drive
  time-of-day colour animation) and the exact `VIBE_Light_CollectAffectedObject` range cull (the host
  applies every light to every object rather than the scene-graph affected-set); both cosmetic here.

## Deferred — needs decision/work (rule 8: named, not faked)
- **Camera *flight* interpolation** (A1→A2 over 1500 ticks) — camera is static at a waypoint for
  now. Scoped (see above): the flight is an **object animation** on the camera node built by
  `VIBE_Anim_CreateObjectAnim` @0x5cef14, which uses keyframe **tangents**
  (`VIBE_Anim_BuildFrameTangents` @0x5cec40) — i.e. a spline, not a plain lerp. Faithful
  reconstruction pulls in the object-anim frame store + the per-frame advancer
  (`VIBE_Anim_AdvanceFrameIndex` @0x5ccf18) and applies to the real `SceneNode3` layout (camera
  pos +76/+92, euler +132/+144), which the host `SceneObjectInst` does not yet carry — a
  multi-function cluster (rule 9 candidate), not a one-off.
- **Live native 3D New-Game front** — adopt `play::SceneChooseCityHooks` (done, see above) into the
  SDL/Vulkan New-Game path: render the 3D scene + present each frame, feed real mouse/keys into its
  `ChooseCityInput`, and resolve the real `Menu\CHOOSECITY` info-form text. The headless loop + pick
  are wired and tested; this is the per-frame render/present + real-text binding only.

## 2026-06-09 — flight wired + room-render root-cause fixed (decompile-grounded)
The live `RunCityScreen3D` now plays the A_Stadtwahl camera flight (`dummy_A0→A1→A2`) and
renders the full enclosed Secretariat room. Three reported defects ("camera is not fly /
room has no walls / blue filler") all traced to **one** cause: **backface culling was off**.
The camera sits *inside* the enclosed room, so a near wall's back-face painted over the whole
frame (a flat brown/blue fill) and hid the office + map table. Setting
`PerspRenderOptions::backfaceCull = 1` (the engine's D3D fixed-function cull RS) fixes it:
A0 shows the office (desk/chair/bookshelf/window/walls/ceiling/floor), the pan crosses the
desk, and A2 settles top-down on the Germany map table with the city pins. No blue void.

Decompile verification (the camera + transform were already correct, only cull was wrong):
- `VIBE_Camera_SetToDummy` @0x43fbbc: camera `+76`(pos) ← dummy `+92` (`SetPosition`),
  then `SetWorldTranslation(cam, dummy+144)`.
- `VIBE_Object_SetWorldTranslation` @0x5af50c: sets `+132`=euler, then **type 3 (camera)** →
  `+396 = MatrixFromEuler(−euler)`; **all other types** → `+396 = MatrixFromEuler(+euler)`.
- `VIBE_Transform_PointToBoneLocalSpace` @0x5c8c40: `view = M_cam^T · (world − cam+76 − cam+120)`
  (subtracts both `+76` and `+120`; `+120` is 0 for the cutscene cam).
- `VIBE_Transform_PointThroughBoneChain` @0x5c8b38: world = object `+120` then, up the `+504`
  parent chain, each parent applies `M_parent^T·(p+pivot+108) − pivot+108 + pos+76 + +120`.
  For ChooseCity, `+108`/`+120` are 0 (loader sets only `+76`/`+132`), so the host
  `ComputeWorldXforms` (parent compose of `M^T·local + parent+76`) matches the engine.
- Confirmed empirically: A2 with the room shell removed frames the map table exactly; with the
  shell + `backfaceCull=1` the grey stone walls, shelf, chairs and table all render correctly.

Tests: `choosecity_flight_montage_e2e_test` (renders A0→A1→A2 + settled, asserts the room draws
in every frame — 19 checks), all existing ChooseCity tests green (full suite 1148/1148).
Status of former "deferred" items: **camera flight = DONE** (`CameraFlightAt`, object-anim
spline), **live native 3D front = DONE** (`RunCityScreen3D`, flight + pick + per-frame present).

## 2026-06-09 — single moving tower (1:1 city-marker model)
RE'd `VIBE_Menu_RunChooseCity` @0x52e6d8: the real screen spawns one INVISIBLE pick-point
marker per city (`VIBE_Map_SpawnCityPointMarker`, registered "stadt_<city>") as the only pick
targets, plus exactly ONE animated tower (`VIBE_Object_AttachToUniverseNode "sp_STADTTURM"` +
`VIBE_Character_LoadObjectAnimation "sonstiges\wimpel_STADTTURM.baf"`). On every selection
change the tower is repositioned to the picked marker: tower pos/rot = offset globals
`dword_52762C/527648` (pos) + `dword_52763C/527658` (rot) + marker pos — and all four offset
globals are ZERO in the binary (verified via get_bytes), so the tower sits exactly on the
selected marker. `RunCityScreen3D` now matches: per-city `noRender` pick markers + one
`noPick` `sp_STADTTURM` tower that starts on the default (first) city and moves on click
(`moveTowerTo`). Added `SceneObjectInst::noRender`/`noPick`. Tests: `choosecity_tower_e2e_test`
(exactly base+1 mesh drawn regardless of city count; tower moves), `sdl_city_screen3d_itest`
green. Deferred (rule 8, named): the animated wimpel-flag `.baf` on the tower (cosmetic; the
tower mesh + move behavior are the observable 1:1 piece).

## 2026-06-09 — tower MOVE is a smooth object-anim glide (1:1, not a teleport)
RE'd the tower-move path in VIBE_Menu_RunChooseCity @0x52e6d8 (call at 0x52eadb): on a
selection change it calls VIBE_Sound3d_SetListenerFromVectors → VIBE_Sound3d_SetListener-
Orientation @0x426014 with the picked city's position and flags 0x20. That function is
misnamed — for a non-camera object with `(flags & 8)==0` it does NOT snap
(SetWorldTranslation is skipped); instead it computes the position/rotation delta from
the object's CURRENT transform to the target and calls **VIBE_Anim_CreateObjectAnim
@0x5cef14** to build a 2-keyframe object animation (start transform → target). The frame
loop then advances it, so the tower **glides** across the map. Because a 2-keyframe anim
has no interior frames, render::BuildFrameTangents leaves the endpoint tangents zero and
the Hermite reduces to an **ease-in-out (smoothstep)** — verified: glide progress 0 / 28 /
90 / 153 / 181 over fracs 0/.25/.5/.75/1 (small-big-small = eased).

Reconstruction: `play::SampleTowerGlide(from,to,frac,out)` (scene_view) builds the same
2-keyframe `render::object_anim` and samples it. `RunCityScreen3D` no longer snaps the
tower on click — it starts a wall-clock glide (~500 ms, framerate-independent) from the
tower's current spot to the picked city and advances it each frame (rebuilding the tower
geometry only while gliding). Tests: `tower_glide_test` (endpoints, smoothstep mid +
ease, monotonic), `tower_glide_montage_e2e` (renders the slide HANNOVER→AUGSBURG).

## 2026-06-09 — ChooseCity info window (real per-city description, 1:1)
RE'd the info-window text in VIBE_Menu_RunChooseCity @0x52e6d8: on selection change it
renders the picked city's $C name + `_STADTAUSWAHL_<CITY>_BESCHR` description (and `_INFO`)
from the localized text DB (VIBE_Text_FindTextArrayIndex). The BESCHR is rich-text markup:
`$[title$]` then `$N`, then two-column rows `$B label : $N> value $A $0>` (label left,
value at a column tab).

Reconstruction:
- `play::CityInfoText` loads the real text DB (textbin_deutsch.BIN PKZIP → every `.res`
  member via the reconstructed `gui::text::BuildTextArray`, tiling by baseIndex) and
  resolves `_STADTAUSWAHL_<CITY>_BESCHR/_INFO+0`. Verified: all 5 shipped cities resolve
  (idx 11920–11929 in the 23 707-entry DB).
- The shipped text in this install is **Russian CP1251**; the original draws menu text
  with a built-in bitmap font (the GDI `TextOutA` path is dead code) and the reversed
  Latin binary's font ROM has no Cyrillic. Added `render::text_cp1251` — a CP1251 Cyrillic
  5x7 glyph set in the engine's row format (bit 0x10 = col 0), reusing the engine ASCII
  ROM for <0x80; lowercase = uppercase shape (small-caps 5x7 reduction). `DrawTextCp1251`
  blits it (6 px advance) onto 16/32 bpp surfaces. (Per the user's choice — extend the
  bitmap font, no new dependency — not a 1:1 copy of a specific shipped Cyrillic font,
  which isn't in the reversed binary.)
- `play::RenderCityInfoCard` parses the BESCHR markup 1:1 ($[title$] highlighted header,
  $N/$A newlines, $N> column tabs, label/value columns) into a framed panel.
- Wired into `RunCityScreen3D`: loads the DB once, draws the hovered (else selected) city's
  card bottom-left each frame. Verified render shows ГАННОВЕР + ИГРОКИ: Макс. 8 династий /
  СЛОЖНОСТЬ / СТРОЙКИ / ОСОБЕННОСТЬ rows with the real data.

Tests: `text_cp1251_test` (ASCII==engine ROM, Cyrillic present, small-caps, draw advance),
`cyrillic_font_e2e`, `city_info_card_e2e` (all 5 cities' real BESCHR render),
`city_info_screen_e2e` (scene + info window composited). Both presets green (1159/1159).
Deferred (rule 8, named): the exact shipped Cyrillic font glyph shapes (use a host 5x7
set) and the form-defined window rectangle (panel placed faithfully bottom-left).

## 2026-06-09 — info window 1:1: real texture, position, choose button
Replaced the synthetic info panel with the original's real assets + layout:
- **Position**: parsed Menu/CHOOSECITY.form (FRM2, Form_ParseResourceFile @0x41beb8) —
  window 0 = (120,392,548,204) **bottom-centre** of the 800x600 layout, text sub-window
  (129,400,527,141), button strip (128,545,529,43). Decoded the window geometry from the
  record (x=u16@0, y=u16@2, w=u16@4, h=u16@6) and the bg-texture field (+3916).
- **Texture**: the real `_PERGAMENT_MB` parchment panel from gilde.gfx (render::GfxArchive
  decode), scaled to the panel rect — dark text on parchment.
- **City crest**: the real `_STADTWAPPEN_<CITY>` heraldic shield on the left.
- **Choose button**: the **red main-menu button** `_BUTTON_RED` (gfx 174), composed left-cap +
  stretched centre + right-cap exactly like RenderMainMenu, captioned "OK", right-aligned on the
  button strip; clicking it confirms the selected city (same as Enter). (Earlier used `_AUSWAHL`
  — gfx record 1210, the confirm id — but that sprite is the blue selection radio, not the action
  button; the visible control is the red button, matching the main menu.)
New: `play::CityInfoGfx` (loads/caches the panel/crest/button shapes) + `RenderCityInfoWindow`
(blits panel+crest+text+button at the form rect, returns the button hit-rect). Wired into
`RunCityScreen3D` (load gfx once; render the window each frame; click the button => confirm).
Tests: `sdl_city_screen3d_itest::ChooseButtonConfirms` (clicking _AUSWAHL confirms HANNOVER),
`city_info_screen_e2e` (asserts the panel + button layout). Both presets green (1159/1159).

## 2026-06-09 — lighting fix + projected shadows for the 3D scene
- **Light fix**: `RunCityScreen3D` now uses the reconstructed baked per-vertex lighting
  (VIBE_Light_BuildObjectCache: the two warm candles, the blue window light, the sun, over
  the .ed3 band-0 ambient) instead of flat shading — the warm, candle-lit Secretariat. The
  earlier near-black result was a side effect of the wrong light world-positions; the
  parent-transform fix corrected them, so baked now renders correctly (baked total luma is
  ~40% of flat and non-uniform). `ComputeSceneAmbient` supplies the band-0 ambient.
- **Shadows**: a host-side projected contact-shadow pass in `BuildSceneDrawList`
  (`PerspRenderOptions::shadows`): each prop's triangles are flattened onto its own base
  plane (the surface it rests on) along `shadowDir`, emitted as one darkening batch
  (`render::kSceneShadowBatch` = texId -2). Receivers (walls/ceiling/floor/shelf) only
  receive. Both render paths darken the receiver: `RasterizeDrawList` multiplies the dest
  pixel by the shadow factor (two-sided, single-layer via depth-write); the Vulkan backend
  uses a second pipeline with multiply blend (`dst = dst * src`, cull none). The tower/
  candle/props now cast grounded shadows on the table/floor.
- Validated: `cc_light_probe_e2e` (baked < flat luma; the shadow pass adds a batch + lowers
  luma), `scene_gpu_vs_cpu_e2e` extended (GPU baked+shadows matches CPU: 95% within 4/255,
  mean 1.8). Both presets green (portable 1160/1160). Note (rule 3/8): the projected
  contact shadow is a host-renderer grounding pass — the engine bakes per-vertex light but
  has no cast-shadow pass for these menu props; the lighting itself is the 1:1 reconstruction.

## 2026-06-09 — fix camera-motion flicker (bilinear filtering)
The cutscene camera flight shimmered. Isolated with a sub-pixel (0.2-tick) camera delta:
835 pixels flipped >60/255 — concentrated on the detailed stone walls/floor. Ruled out
shadows (843 without) and confirmed deterministic (static delta = 0). Untextured fell to
184, so ~78% of the shimmer was **NEAREST texture-sampling aliasing**: a sub-pixel camera
move flips which texel each pixel point-samples on high-detail surfaces. (Perspective-correct
depth + a coplanar depth-slack were tried first; the slack stays as a cheap z-fight guard.)
Fix: `PerspRenderOptions::bilinear` (+ `Scene3DDrawList::bilinear`) — a 4-tap bilinear texel
fetch (REPEAT-wrapped, texel centres at +0.5) in `RasterizeDrawList`, and a LINEAR Vulkan
sampler (`sceneSamplerLinear_`) selected per draw list. Default off (NEAREST stays byte-exact
== SampleTexel for the existing tests); `RunCityScreen3D` turns it on. Sub-pixel flip pixels
drop 835 → 87. Test: `cc_camera_stability_e2e` (NEAREST shimmers >300, bilinear < nearest/3
and < 200; static delta == 0). Both presets green (portable 1161/1161).

## 2026-06-09 — residual flicker = minification; added mipmaps (trilinear)
Bilinear fixed magnification shimmer but the walls/floor/map still flickered: those
textures are MINIFIED (many texels per pixel), where bilinear (4 taps) still aliases.
Added mip pyramids: `render::BuildSceneTextureMips` box-downsamples each scene texture to
a chain (stored on `SceneDrawTexture::mips`), built in `BuildSceneDrawList` when filtering
is on. `RasterizeDrawList` computes a per-triangle LOD (texels-per-pixel = uv-area·w·h /
screen-area) and trilinear-samples two levels; the Vulkan backend uploads the full mip
chain per texture (`createSceneTexture` multi-level copy) and uses a trilinear sampler
(`sceneSamplerLinear_`: LINEAR mip mode, full LOD). Gated behind the filtering flag, so the
NEAREST byte-exact paths/tests are unchanged. Verified: GPU render mips all 67 textures and
is clean; CPU sub-pixel flicker 835(NEAREST) → 69(bilinear+mips). `cc_camera_stability_e2e`
+ `scene_gpu_vs_cpu_e2e` (GPU mip path runs, non-blank) green; both presets 1161/1161.

## 2026-06-09 — residual flicker = grazing-angle aliasing; added 2x supersampling
After bilinear+mips, the back wall still shimmered: viewed at a grazing angle the texture
is anisotropic, which isotropic trilinear can't resolve, and the software Vulkan ICD
(lavapipe) has no anisotropic filtering. Measured on the real GPU device (two adjacent
flight frames, sub-pixel delta): NEAREST=824 big-flips, bilinear+mips=64, and
bilinear+mips+**2x SSAA = 1**. So `RunCityScreen3D` now supersamples the 3D scene 2x and
box-downsamples. `renderScene3D` was refactored to render at the draw-list resolution into
its OWN colour+depth image (`sceneColor_`/`ensureSceneTargets`, dynamic viewport) and
downsample on readback into the caller's surface — so passing a 2x draw list + a 1x target
gives SSAA on the GPU; the CPU fallback rasterises into a 2x surface and downsamples in
`RunCityScreen3D`. Anisotropic filtering is also enabled on the GPU sampler when the device
supports it (real hardware), as a cheaper path. Tests: `gpu_camera_stability_e2e` (GPU
NEAREST≫bilinear+mips, +2x SSAA ≈ 0) and `cc_camera_stability_e2e`. Both presets 1162/1162.

## 2026-06-09 — fix whole-screen near-black flashes (near-plane clipping)
During the camera flight, ~2 brief moments the whole screen went near-black. The GPU path
was fine; the CPU rasterizer was the culprit. Diagnosed with a flight-luma probe scanning
ticks f=0..90: at f=37,38,52,62-64,77-80 the triangle count collapsed (~2000 → ~20) and
avgLuma fell below 12, while the camera basis stayed perfectly smooth (right/up/fwd nearly
identical between adjacent ticks). Root cause: `RasterizeDrawList` clamped each behind-near
vertex with `if (vz < nearZ) vz = nearZ;` — a wall/ceiling triangle straddling the near
plane (one vert behind the camera) got that vert pinned onto the near plane, producing a
huge near-field triangle that filled the z-buffer and blacked out the frame. The GPU
hardware-clips (via `gl_Position.w = vz`), so it never showed the bug.
Fix: proper Sutherland-Hodgman **near-plane clipping** in `RasterizeDrawList`. The
per-triangle projection+raster body is now a `drawTri` lambda; before calling it each
source triangle is classified by view-space z against `nearZ`: fully in front → draw as-is;
fully behind → skip; straddling → clip to a 3-or-4-vertex polygon (interpolating pos/color/uv
at the crossings) and fan-triangulate. The old per-vertex clamp stays only as a defensive
no-op. Verified: flight probe now shows tris declining smoothly 2232→867 with NO near-black
ticks (avgLuma climbs 15→35 monotonically). `scene_drawlist_equiv_e2e` still matches the
oracle (static A2 view has no straddling tris). Both presets 1163/1163 (1162 + the temporary
probe, since removed). Probe test was throwaway and deleted.

## 2026-06-09 — window light shaft & candle flame transparency (mode-2 additive, 1:1)
The window light shaft and candle flame rendered as SOLID opaque slabs (the shaft
covered the window with a grey panel; the flame was a cream blob). Reverse-engineered the
engine's transparency from `gilde.exe`:
- **0x5F87B8 VIBE_Model_LoadFastChunk** — each .bgf material's first flag byte (v62) is the
  transparency MODE (0=opaque, 1=alpha, 2=additive) and the second (v63) is the OPACITY
  (0..255); they're packed into the dword passed to VIBE_Texture_LoadByName and land at the
  runtime texture record's +108 (opacity, 0xFF=opaque) / +110 (blend bits).
- **0x5ae434 VIBE_Render_DrawTexturedTriangles** — calls SetBlendMode(material+108 != 0xFF,
  +110 bit0, +110 bit1, …) per material run.
- **0x5e0358 VIBE_Render_SetBlendMode** — mode!=0: ALPHABLENDENABLE(27)=1, ZWRITE(14)=0,
  ZBIAS(47)=16, FOGENABLE off; additive branch (byte0==2) sets DESTBLEND(20)/SRCBLEND(19)
  from the additive-blend globals. byte2 bit0 = COLORKEY/ALPHATEST.
A material-flag dump of the ChooseCity meshes confirmed the only translucent materials are
`ub_LICHTKEGEL_ZUFTH_SEKRETARIAT` (mat0 `gl_FensterZunfthalle_v01` op=25, mat1 `fx_stau_01a`
dust motes op=140) and `ub_KERZENFLAMME` (`fx_feua_01a` op=127) — **all mode 2 (additive)**.

Reconstruction (rule 3 — only the GPU API is swapped):
- `render::SceneBlendMode` + `SceneDrawBatch::{blend,opacity}` carry the per-material mode.
- `BuildSceneDrawList` reads each poly's `model.materials[matIndex].flag/b1`; opaque polys
  draw inline, transparent polys are DEFERRED and flushed after all opaque + shadow batches
  (additive never casts a shadow). Opacity = byte1/255.
- `RasterizeDrawList` (CPU reference) + `RenderSceneObjects` (oracle, refactored to a shared
  `rasterTri` lambda + deferred pass): transparent batches are depth-TESTED but write NO
  depth; additive composites `dst += src*opacity`, alpha lerps `dst*(1-op)+src*op`.
- Vulkan backend: two new pipelines `scenePipelineAdditive_` (SRC_ALPHA/ONE) and
  `scenePipelineAlpha_` (SRC_ALPHA/INV_SRC_ALPHA), both depthWrite OFF; `scene.frag` outputs
  alpha = per-batch opacity via the free `pc.fwd.w` push-constant slot (pushed per batch).
Verified: the window shaft + flame are now translucent (CPU + GPU). `scene_drawlist_equiv`
still bit-matches the oracle (mode 0 maxDiff 0, mode 1 ≤1 LSB); `scene_gpu_vs_cpu` ≈95% within
4 LSB. Tests: `scene_blend_test` (4 unit golden vectors: additive/alpha/faint-glow/no-z-write)
+ `scene_transparency_e2e_test` (the ChooseCity scene yields additive batches; the glow only
brightens). Both presets 1164/1164.
