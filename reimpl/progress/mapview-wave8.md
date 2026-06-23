# Wave-8 — 2D Overview Map View (W8-MAP)

Reconstruction of the **2D overview map panel** — the full-screen city/world map
that opens over the session, showing the map background + entity markers (player,
buildings, NPCs, missions, ambush points) as dots — the RENDER + interaction half
of `VIBE_MapView_PanelDispatcher @0x5441d0`.

## Module
- `src/play/map_view.h` / `src/play/map_view.cpp` — `guild::play::MapView_*`
- `tests/unit/mapview_test.cpp` — 34 golden/round-trip/render checks, all passing
  (`build/mapview_test`).

## Anchor functions

| addr | name | status |
|------|------|--------|
| `0x5441d0` | `VIBE_MapView_PanelDispatcher` (0x1df5 bytes) | RENDER+interaction reconstructed (composition layer); Form/Window/Object widget tree deferred — see Gaps |
| `0x5440b4` | `VIBE_MapView_ComputeMarkerScreenPos` | **reused** as `gui::MapView_ComputeMarkerScreenPos` (already reconstructed) |
| `0x54457e` | marker selection-sort by screenY (inside 0x5441d0) | **reused** as `play::MapViewSortMarkersByScreenY` (ui_recon5_panels) |
| `0x543bd0` | `VIBE_MapView_StepScrollOffset` | **reused** as `gui::MapView_StepScrollOffset` |
| `0x543994` | `VIBE_MapView_UpdateScrollState` | reconstructed in gui::MapView_* (decompiled, drag-band math) |
| `0x5437d8` | `VIBE_MapView_AddCornerObjects` | reused (`gui::kMapCornerPlacements`) |
| `0x543870` | `VIBE_MapView_CollectOfficeMarkers` | reused (`gui::MapView_CollectOfficeMarkers`) |

The dispatcher's pure LOGIC (al-mode dispatch bits, person-query filter literals,
marker stride, radio-button Y/sprite tables, clamp, scroll-delta) was already
recovered 1:1 in `src/play/ui_recon5_panels.{h,cpp}` (`kMapViewMode*`, `kMapView*`,
`MapViewSortMarkersByScreenY`, `MapViewScrollDelta`, `MapViewClampFocus`). This
module adds the **missing RENDER**: composing the background + the projected
marker dots into a software `render::Surface`, plus the **click→world** inverse.

## What 0x5441d0 does (grounded line-by-line)
1. Loads the map FORM around the background asset `"Misc\Landkarte2"`
   (`VIBE_GameTick_Finalize(0,0,aMiscLandkarte2) @0x41beb8 / aMiscLandkarte2 @0x62406c`)
   and adds a 512×360 scrollable child window (`VIBE_Window_AddChildWindow(74,76,356,518) @0x5443c3`).
2. Enumerates entities to mark:
   - persons/buildings via `VIBE_Person_QueryBegin(1,2,5,9,4,player) @0x586c20`,
     filtered: skip object types 68/69, skip if `entity[+90]&1`, skip type 71
     unless the player handle is set; cap 256 (`kMapViewMaxMarkers`).
   - mode `0x04`: `sp_AUFLAUERLEGEN_*` ambush points (`@0x632275`).
   - mode `0x08`: mission markers from `dword_123343C`.
   - the focused/player object (clamped into the visible rect, `@0x544cc7`).
3. Projects each entity world (x,z) through the bone chain
   (`VIBE_Transform_PointThroughBoneChain @0x5c8b38`) + heightmap
   (`VIBE_Heightmap_WorldToTileWithHeight @0x5c6644`) to a tile, then through
   `VIBE_MapView_ComputeMarkerScreenPos @0x5440b4` to the map-surface pixel.
4. Sorts markers ascending by `screenY` (selection sort `@0x54457e`), **centres**
   each marker sprite (`screenX -= (w>>16)/2 ; screenY -= (h>>16)/2`, `@0x5457e1`),
   creates a window object (`VIBE_Object_AddToWindow @0x41ae10`) and draws it; type
   71 ("money") markers get a gold label (`VIBE_MapView_FormatGoldLabel @0x5440a4`),
   others a name copy from `byte_13CD6A0`.
5. Frame loop `VIBE_GameLogic_RunFrameLoop(423879) @0x4c09a0`: each frame steps the
   auto-scroll (`@0x543bd0` / `@0x543994`) and edge-scroll keys
   (`byte_671E28/2B/2D/30`), and on a click over a marker returns/opens the entity
   per the al-mode bits (`v252&1` return entity, `v252&2` person-selection sub).

## Recovered constants (`get_bytes`)
Marker projection (reused leaf, `@0x624058..`):
`flt_624060=5.33` (world→pixel), `flt_624064=0.5` (persp denom), `flt_624068=51.0`
(persp bow); `dbl_624058` low dword = `0x00000000` (origin scale, X term ≈ 0).
Secondary city-point path (`@0x62407c..`): `flt_62407C=5.33`, `dbl_624080≈0.43`,
`dbl_624088=0.5`, `dbl_624090=5.33`, `dbl_624098≈0.48`, `flt_6240A0=0.5`,
`flt_6240A4=51.0`. Viewport clamp: world−512 × world−360 (`@0x543c3e/0x543c50`).

## Exposed entry (the clean handoff)
```cpp
play::MapView_RenderOverview(Surface&, vector<OverviewMarker> markers,
                             OverviewCamera cam, viewX,viewY,viewW=512,viewH=360,
                             dotSize=6, OverviewPalette);
play::MapView_ProjectMarker(marker, cam)  -> {x,y,inView}  // REAL 0x5440b4 + scroll
play::MapView_ClickToWorld(vx,vy, cam)    -> {worldX,worldZ} // inverse of the affine
play::MapView_PickMarker(markers, cam, vx,vy, dotSize) -> index|-1 // click→entity
```
Markers are projected (real `gui::MapView_ComputeMarkerScreenPos`), Y-sorted (real
`MapViewSortMarkersByScreenY`), centred and clipped to the viewport, then drawn as
filled dots (`gui::MenuFillRect → render::SurfaceDrawHLine`) + outline
(`render::SurfaceDrawRectOutline`) — the same reconstructed 2D leaves the menu/HUD
renderers use. Deterministic: identical inputs → identical pixels.

## Bind-site handoff (how the map opens over the session)
A HUD map key/button raises the al-mode bits and the original calls
`VIBE_MapView_PanelDispatcher @0x5441d0`. In the reimplementation the session HUD
(`src/play/session_hud`) opens the overview by calling `play::MapView_RenderOverview`
into the session framebuffer surface; the al-mode bits map to `play::MapViewOpenMode`
(`ReturnObj=0x01`, `PersonSel=0x02`, `Auflauer=0x04`, `Missions=0x08`, `Tooltip=0x10`).
**The orchestrator wires the key/button → `MapView_RenderOverview`; this module does
NOT edit the bind-site** (`session_hud.*`, `sdl_session.*`, `apps/guild_run.cpp`),
per ownership rules.

## Named gaps (rule 8)
- **`Misc\Landkarte2` background artwork** — the one asset edge. Loaded by
  `VIBE_GameTick_Finalize @0x41beb8` through the SHAPBANK loader; the shipped bank
  is **depth-2 (24bpp)** and is converted to depth-1 at runtime by
  `VIBE_Shape_ConvertRgbTo16 @0x5d7c0c`, which is **not yet reconstructed** (same
  finding as `session_hud.h` for gilde.gfx). So the real bitmap cannot be blitted
  here. Routed through `MapViewHooks::drawBackground` with an **inert default**
  (fills the viewport with the configured backdrop colour) so the deterministic
  marker composition is testable in isolation.
- **Form/Window/Object widget tree** (`VIBE_Form_* / Window_* / Object_AddToWindow`)
  — the original places each marker as a *widget object* in the window tree, which
  the engine window renderer later paints. That tree + the asset-backed per-type
  marker sprites are a deep deferred GUI subsystem; the reconstructed RENDER here
  performs the equivalent **software composition** (project → sort → centre → draw
  dot) that the widget pipeline ultimately produces, using the real 2D leaves.
- **Bone-chain + heightmap projection** (`@0x5c8b38 / @0x5c6644`) — recovered/used
  elsewhere; this module takes the already-projected world (x,z) as input (the same
  contract `gui::MapView_ComputeMarkerScreenPos` consumes), so no duplication.

## Tests (`tests/unit/mapview_test.cpp`, 34 checks)
- `ProjectCenterMarker` — world origin → map centre (mapW/2, mapH/2); edge `inView`
  semantics; scroll brings the centre into the viewport interior.
- `ProjectOffsetMarkerGolden` — worldX=10 → x=570 (golden from 5.33/0.5/51.0
  constants, perspective bow applied).
- `ProjectPanScrollOffset` — pan shifts the full-map pixel; equal scroll cancels it.
- `ClickToWorldRoundTrip` — the click→world inverse recovers the forward-projected
  pixel; a hand pixel maps to worldX≈12.0075.
- `MarkersSortByScreenY` / `MapView_PickMarker` — Y-sorted draw order; click on the
  smallest-Y marker resolves to its entity.
- `RenderOverviewSurface` — inert backdrop painted; markers clipped at the viewport
  edge (512-wide → centres at 512/570 clip out); wider viewport draws both dots at
  their centred pixels with the per-kind colour (verified against a leaf round-trip,
  so independent of the format's channel order).
- `BackgroundHookOverridesInert` — installed background hook replaces the inert fill.

## Build
`cmake --build build --target mapview_test` → links into `libguild.a`; runs clean.
(Concurrent wave-8 edits to non-owned files, e.g. `world::ContactRegisterMenu`,
cause transient link errors in *other* targets — unrelated to this module.)
