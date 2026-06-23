# Session HUD overlay — `play::SessionHud`

In-game HUD overlay for the **native city session**: composites the money +
game date/time caption, the bottom player bar, the selected-entity status text
and map markers directly onto the session's **16bpp RGB565 framebuffer** (the
format `play::RealCityRenderer` renders — its `Render()` contract is a device
init'd to `fbW x fbH x 16bpp`), using **only reconstructed draw leaves**.

Files: `src/play/session_hud.{h,cpp}` (new, additive — no owned file edited).

## What it draws, and through which reconstructed leaf

| HUD element | Leaves (gilde.exe provenance) |
|---|---|
| money caption | `world::MoneyFormatWithSeparators` @0x58f798 (via `play::HudMoneyString`) → `render::DrawText`/`DrawGlyph` @0x434E18/@0x434D0C (native 16bpp gate) |
| date/time caption | day from `sim::GameTime`; time-of-day from `gui::Clock_ComputeTimeOfDay` @0x527778 over the tick accumulator (via `play::HudDateString`) → same DrawText leaf |
| player bar | slot assignment/layout `gui::PlayerBar_AssignSlot`/`_SlotLayout` (VIBE_PlayerBar_BuildContent @0x4b11e4 cluster), fill scaling `play::HudBarFillPixels`; fills via `gui::MenuFillRect` @0x423c70 → `render::SurfaceDrawHLine` @0x423ffc; frame via `render::SurfaceDrawRectOutline` @0x4242d4 |
| slot icon / marker artwork | `play::HudRenderHooks::drawSprite` → **real** `render::ShapeShowFromBank` @0x5d861c → `ShapeBlitColored16` @0x5d7164 (installed by `play::InstallRealHudBridge`; genuinely 16bpp-correct on this surface) |
| selected-entity status | `gui::StatusText_Register` @0x4bcc80 (the real 50-dword-stride table, de-dup by key) + the DrawText leaf for the line |
| map markers | `gui::MapView_ComputeMarkerScreenPos` @0x5440b4 (via `play::HudMarkerScreenXY`) + fill/outline leaves |

No boundary pixel-format conversion is needed: `DrawGlyph` natively rasterizes
16bpp lock targets, `SurfaceDrawHLine`/`SetPixelRgb` pack 16bpp through the
`ColorFormat` (565), and `ShapeBlitColored16` is a u16-destination blit — the
HUD is composed with the 16bpp-native reconstructed leaves, exactly as the task
boundary allows.

## API

```cpp
class play::SessionHud {
  bool Init(shim::IFileSystem* fs);   // load REAL gfx/gilde.gfx + install bridge
  struct Inputs { i64 money; i32 moneyRate; const sim::GameTime* clock;
                  int clockTick; int selectedId; const char* selectedName;
                  const HudBarObject* barObjects; int barObjectCount;
                  const HudMarker* markers; int markerCount;
                  int markerPanX, markerPanY, markerCameraOrigX; };
  void Render(void* fb16, int w, int h, int pitchBytes, const Inputs&);
  const SessionHudResult& lastResult() const;       // per-frame draw counts
  // investigation results: gfxLoaded/gfxObjectCount/bankCount/fmt2BankCount/
  // depth1BankCount/realShapesUsable/missingDecode/archive
};
```

`Inputs.money` is i64 at the session boundary and clamped to i32 before the
real formatter (the engine's money is a 32-bit register value — eax of
@0x58f798). Layout anchors (`SessionHud::Layout`) are caller-tunable, mirroring
`RenderHud`'s caller-supplied-anchor model.

## REAL gilde.gfx investigation (the Init load)

`Init(fs)` reads `gfx/gilde.gfx` (59,545,223 bytes in the shipped install) and
parses it with the **real** loaders:

* `gui::Form_LoadFromBuffer` — the file-parse half of `VIBE_Gui_LoadGfxFile`
  @0x41b888: u32 objectCount = **1806** (cap 2048) + 1806 × 84-byte records
  into `gui::g_gfxObjects` (the `d2:fileobj` table), plus the Form/Window/
  Widget baseline init.
* `render::GfxArchive` — the record directory (+48 dataOffset / +56 dataSize)
  over the on-disk `SHAPBANK` containers, including the reconstructed depth-2
  pixel decode `DecodeShapeBlob` (1:1 of `VIBE_FrameTable_Index` @0x5fbb24).

### FINDING — the named gap (why real shapes don't feed ShapeShowFromBank)
### >>> CLOSED by render/shape_convert16 — see progress/shape-convert16.md <<<

* **172** records carry a `SHAPBANK` blob; **all 172 are pixel-format 2
  (24bpp)** (bank format byte @bank+52 == 2; every first shape's depth byte
  @shape+12 == 2). **Zero depth-1 (16bpp) banks ship.**
* The unscaled bank blitter `VIBE_Shape_ShowFromBank` @0x5d861c only
  rasterizes **depth-1** RLE shapes (via `ShapeBlitColored16` @0x5d7164); a
  depth-2 shape is a **no-op success** (returns 1, paints nothing) — proven
  behaviorally in the e2e (`RealDepth2BankIsANoOpInTheBlitter`).
* The original converts every loaded bank lazily:
  `VIBE_State_Helper` @0x40e014 (d2_LoadObj: raw bank read from the +48/+56
  offset/size, pointer stored at record+52) →
  `VIBE_ShapeBank_ConvertNew` @0x5d80a8 →
  `VIBE_Shape_ConvertToNew` @0x5d8080 →
  **`VIBE_Shape_ConvertRgbTo16` @0x5d7c0c** (24bpp RLE → 16bpp RLE) /
  **`VIBE_Shape_Convert8To16` @0x5d7924** (8bpp variant; unused by gilde.gfx).
* ~~`@0x5d7c0c` / `@0x5d7924` are NOT reconstructed~~ **CLOSED**:
  `render/shape_convert16` now carries the 1:1 `ShapeConvertRgbTo16` @0x5d7c0c,
  `ShapeConvert8To16` @0x5d7924 AND the bank driver `ShapeBankConvertNew`
  @0x5d80a8 (previously only a header comment in shape_recon_cluster.h, never
  implemented); `InstallShapeConvertersIntoLeaves9()` binds them into
  `RenderLeaves9Hooks` (installed by `world::InstallRealWorldNetWiring`), so
  `Shape_ConvertToNew` @0x5d8080 dispatches the real converters.
* Fallback (as documented in `play/wire_hud_bridge.h`): the sprite hook blits
  the in-memory real-format depth-1 `DefaultHudSpriteBank` through the REAL
  `ShapeShowFromBank` leaf. The real artwork **pixels** remain recoverable
  (the depth-2 decode @0x5fbb24 IS reconstructed and e2e-verified); only the
  depth-1 bank **conversion** is missing.

UNBLOCKED — the plan above was executed verbatim: `VIBE_Shape_ConvertRgbTo16`
@0x5d7c0c (+ `@0x5d7924`) are reconstructed and installed as the
`RenderLeaves9Hooks` converters; `ShapeBankConvertNew` @0x5d80a8 feeds real
converted depth-1 banks to the bridge via `play::SetHudSpriteBankFromGfx` —
no SessionHud edit was needed. The behavioral no-op proof is FLIPPED in
`tests/e2e/shape_convert16_e2e_test.cpp`
(`RealDepth2BankConvertsAndBlitsRealArtwork`: the same real `_WIN_BORDER` bank
that painted nothing now paints real pixels through the same @0x5d861c leaf).

## Wave 2 — tooltips + selected-entity info panel (ADDITIVE)

`SessionHud::Inputs.panels` (default null = wave-1 frames byte-identical) now
routes a per-frame `play::SessionPanelsInputs` into the owned
`play::SessionPanels` layer (`src/play/session_panels.{h,cpp}`): the REAL
tooltip lifecycle `VIBE_Tooltip_DispatchByType` @0x4f7424 with the newly
reconstructed content builders (`VIBE_Tooltip_BuildObject` @0x4f7a10,
`_BuildUpgrade` @0x4f8154, `_BuildBuilding` @0x4f78e4, `_BuildPerson`
@0x4f84ac, `_BuildContact` @0x4f83e8, the screen-edge clamp 0x4f7aa5, the
byte-exact @0x6496A9 producer table — all in `gui/tooltip_content`), plus the
REAL selected-entity info panel (`VIBE_InfoPanel_Update` @0x4b84c0 + the
@0x4b64b0.. builders of `gui/infopanel_build`), composited through the same
16bpp leaves. `SessionHudResult` gains additive `tooltipVisible /
tooltipTextOps / tooltipIconOps / panelVisible / panelTextOps / panelIconOps`
counters, and panel/tooltip icon blits fold into `spriteBlits`. The full
inventory, the wave-2 per-frame call contract for sdl_session, and the named
gaps (rich-text renderer @0x59d6e8 = plain-glyph projection; .form geometry =
caller-tunable anchors) live in **progress/session-ui-panels.md**.

## Tests (all passing)

* `tests/unit/session_hud_test.cpp` — 11 tests / 58 checks, synthetic 16bpp fb:
  * `InitWithoutFsInstallsBridgeAndReportsNoGfx` — fallback install + gap string
  * `RenderDrawsCaptionBarStatusAndMarkers` — per-element pixel-band deltas
  * `BarFillScalesWithProductionRatio` — fill pixels vs ratio + golden fill math
  * `StatusLineRegistersSelectionInRealTable` — real @0x4bcc80 table, de-dup
  * `RenderIsDeterministic` — bytewise-identical re-render
  * `MoneyClampAndNegativeAreSafe` — i64 clamp boundary, negative form
  * `DegenerateTargetsAreNoOps` — null/zero/odd-pitch guards
  * `SpriteBlitsComeFromTheRealLeafAndCanBeDisabled` — bridge on/off pixel diff
  * `PanelsNullKeepsLegacyFrameAndZeroPanelCounters` — additive-API guarantee
  * `PanelsInputsCompositeTooltipAndInfoPanel` — tooltip + panel pixel bands
  * `PanelsRenderIsDeterministicAcrossFreshInstances` — bytewise-identical
* `tests/unit/session_panels_test.cpp` — 18 tests / 207 checks (see
  progress/session-ui-panels.md)
* `tests/e2e/session_hud_e2e_test.cpp` — 4 tests / 35 checks, GUARDED on
  `GUILD_GAME_DIR` (skip cleanly when absent):
  * `InitLoadsRealGildeGfxAndPinsTheGap` — 1806 records, 172 banks, all fmt-2
  * `RealDepth2BankIsANoOpInTheBlitter` — behavioral proof of the named gap
  * `ReconstructedDepth2DecodeReadsRealShapes` — real `_WIN_BORDER` 8×8 decode
  * `RenderFullHudOverRealAssetsIsNonTrivialAndDeterministic` — full-frame HUD

## Deferred / out of scope

* ~~`VIBE_Shape_ConvertRgbTo16` @0x5d7c0c, `VIBE_Shape_Convert8To16` @0x5d7924~~
  — no longer deferred: reconstructed in `render/shape_convert16` (see
  progress/shape-convert16.md; the named gap above is CLOSED).
* ~~Tooltips (`gui/tooltip_dispatch`)~~ — no longer deferred: wired through
  `play::SessionPanels` with the reconstructed @0x4f7a10.. content builders
  (see progress/session-ui-panels.md).
* The full `VIBE_State_Helper` @0x40e014 lazy-load/eviction bookkeeping
  (`dword_62D20C` budget + `VIBE_Resource_EvictOldestEntry` @0x40decc) — a
  memory-manager edge; SessionHud loads the archive whole, the on-disk bytes
  consumed are identical.
