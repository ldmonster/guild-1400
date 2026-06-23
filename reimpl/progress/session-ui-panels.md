# Session UI panels — tooltips + selected-entity info panel (`play::SessionPanels`)

In-game UI layer for the **native city session**: the original's tooltip
lifecycle and the selected-entity info panel, composited over the session's
16bpp RGB565 framebuffer through the same reconstructed draw leaves
`play::SessionHud` uses.

Files: `src/gui/tooltip_content.{h,cpp}` (NEW — the tooltip content builders),
`src/play/session_panels.{h,cpp}` (NEW — the session layer),
`src/play/session_hud.{h,cpp}` (additive `Inputs.panels` extension).

## Newly reconstructed functions (gilde.exe provenance)

| Address | Symbol | Where |
|---|---|---|
| 0x4f7a10 | `VIBE_Tooltip_BuildObject` | `gui::Tooltip_BuildObjectContent` — full content emission: Waffen/Handelsgut form select (Avatar_LookupById verdict), windows 2/1/3/4, durability vs market-price pair, owner value-ratio line, the 4 ingredient (count,item) rows at the 40+22k pitch, the **@0x6496A9 producer-table walk**, the weapon-user rows, the "+market" row, the `$C` empty token |
| 0x4f8154 | `VIBE_Tooltip_BuildUpgrade` | `gui::Tooltip_BuildUpgradeContent` — class-29 gate (REUSES `Tooltip_UpgradeApplies`), the `(dword_63C744*0.25+0.5)`-scaled price, the 64-slot owner scan (`word&0x7FFF` match → +419 kind / +483 value), kind-3 `"+%a %s$N"` vs `"%i%% %s$N"` |
| 0x4f78e4 | `VIBE_Tooltip_BuildBuilding` | `gui::Tooltip_BuildBuildingContent` — emission order (windows 1/2/3, templates 0x27/0x2A/0x28/0x29); values REUSE `Tooltip_BuildingLayout`/`kBuildingColors` (tooltip_build) |
| 0x4f84ac | `VIBE_Tooltip_BuildPerson` | `gui::Tooltip_BuildPersonContent` — header `$Z$[%1N3$]`, person card (166,5), cash/job(+294/+370)/traits(+525/+560)/religion(+272/+279)/wealth/spouse-betrothed-unmarried/class(+1070)/5-children loop, the 5 skill label+bar rows (text 4810+i, 15px pitch, bar gfx 1162) |
| 0x4f83e8 | `VIBE_Tooltip_BuildContact` | `gui::Tooltip_BuildContactContent` — found-branch emission (windows 1/2, `$Z$[%s$]` + index+1); key resolve REUSES `Tooltip_ResolveContact` |
| 0x4f7aa5 / 0x4f81c2 | the tooltip screen-edge clamp block | `gui::Tooltip_ClampToScreen` — `x+w > screenW-16 → x' = screenW-16-w` (object/upgrade builders only, exactly as in the binary) |
| 0x4f7e32..0x4f80df | weapon-user mapping (inside 0x4f7a10) | `gui::Tooltip_WeaponUserCodes` — 449–451→{24,25}, 452–454→{30}, 464–466/445–448→{32}, 439–441/458–460→{30}, 461–463/442–444→{31}, 455→none |
| 0x4f7ebc gate | the "+market" row gate | `gui::Tooltip_ObjectMarketRow` |
| 0x4f7c27..0x4f7c75 | owner value-ratio math | `gui::Tooltip_ObjectValueRatio` — `base / (market + worker*dbl_6206D8*dbl_6206E0)`, exact bit-pattern doubles |

Static data recovered byte-exact (`get_bytes`):
* `gui::kTooltipProductionTable` — 486 bytes @0x6496A9 (23 rows × 21 bytes +
  the 3 bytes the last unaligned int read touches). Golden: item 468 is
  produced by building codes 41/42/43 (rows 0–2).
* `dbl_6206D8` (0x3F70410441041010 ≈ 1/252), `dbl_6206E0` = 0.25,
  `dbl_620728` = 0.25, `dbl_620730` = 0.5.

## Reused (NOT redefined)

* `gui::Tooltip_Dispatch` @0x4f7424 (tooltip_dispatch) — the per-frame
  lifecycle SessionPanels drives.
* `gui::Tooltip_ClassifySubject` / `Tooltip_SelectBuilder` (tooltip.cpp) — run
  over SessionPanels' **shadow scene tables** (the pick-boundary model
  session_select.h documents): a `kObjectSpan` 65-stride object span (live
  class byte), a `kBuildingSpan` span, one 268-byte person record.
* `gui::InfoPanel_Update` @0x4b84c0 + `InfoPanel_SelectBuilder` /
  `_SelectionChanged` / `_Snapshot` (infopanel) — rebuild-on-change dedup.
* `gui::InfoPanel_Build{Object,Building,Transporter,Standard,Person}`
  @0x4b6930/0x4b64b0/0x4b6c80/0x4b6db8/0x4b7104 (infopanel_build) — run
  through a recording `InfoPanelHost`.
* `gui::Tooltip_ResolveContact` / `Tooltip_BuildingLayout` / `kBuildingColors`
  (tooltip_build).
* Draw leaves: `gui::MenuFillRect` @0x423c70, `render::SurfaceDrawHLine`
  @0x423ffc, `SurfaceDrawRectOutline` @0x4242d4, `DrawText`/`DrawGlyph`
  @0x434E18/0x434D0C, the sprite hook → `render::ShapeShowFromBank` @0x5d861c.

## Per-frame contract (wave 2 — how the session feeds the layer)

`sdl_session.cpp` keeps its existing `SessionHud::Inputs hi{}` block and adds:

```cpp
play::SessionPanelsInputs pin;
pin.hoveredTooltipId = hoverId;          // dword_75BF3C model: stable id of the
                                         // entity under the cursor, -1 = none
pin.scenePickActive  = 1;                // dword_672238 (3D pick this frame)
pin.cursorX = ms.x; pin.cursorY = ms.y;  // unk_67220E/dword_672210 >> 16
pin.hoverKind = ...;                     // kObject/kBuilding/kPerson from the
pin.hoverObjectCode / hoverObjectClass / hoverBuildingCode / hoverPersonId
pin.object/.building/.person = &feeds;   // record-field views + economy results
pin.selection = ...;                     // the dword_631744.. handles from
                                         // SessionSelect (InfoPanel_Update)
pin.selObject/.selBuilding/.selPerson    // the modelled records
pin.textDb = &textDb;                    // optional localized strings
hi.panels = &pin;                        // ADDITIVE — null keeps wave-1 frames
hud.Render(...);                         // panels composite after the markers
hud.lastResult().tooltipVisible/.panelTextOps/...  // additive counters
hud.panels().lastResult()                // full SessionPanelsResult
```

The hover id is derived from `RealCityRenderer::Pick` at the cursor without a
click (the same hit-test boundary `SessionSelect::OnPick` documents); the
selection block mirrors what `SessionSelect` already latches on click.

Lifecycle semantics are the dispatcher's own: build once per hover target
(box anchored at the build-time cursor — the `Form_CenterChildWindows` model
— then the REAL right-edge clamp), dedup while `hoveredTooltipId ==
shownForId`, teardown on hover loss; the info panel rebuilds only when the
selection snapshot differs (@0x4b84c0).

## Named gaps (documented, not papered over)

* `VIBE_Text_RenderRichString` @0x59d6e8 — the rich-text renderer (0x22b2
  bytes / 345 blocks; text cluster). The composited line text is a
  **plain-glyph projection**: byte-exact templates + original argument order,
  text ids resolved via `gui::text::TextDb` when supplied, `$x` tokens
  stripped, `%i/%s/%a/%dNd` substituted. Every template/argument pair is
  pinned by tests.
* `ToolTip\*.form` / `panel\infopanel_*.form` window geometry is asset data
  (form loader); box anchors are caller-tunable (`SessionPanels::Layout`,
  the SessionHud precedent) while per-widget coordinates inside the box are
  the builders' recovered values.
* `VIBE_Tooltip_BuildPersonDetailed` @0x4f882c — only reachable from
  `VIBE_MapView_PanelDispatcher` @0x5441d0, outside the dispatch call tree
  (rule 7) — deferred to the MapView cluster.
* The economy/person-table leaves the builders call (ComputeMarketPrice
  @0x58f3d0, LookupCachedMarketPrice @0x58f6b8, ComputeMarketValue @0x594df0,
  Person_GetCashAmount @0x58bc9c, ComputeTotalWealth @0x591f7c,
  ComputeRankWithinGroup @0x58a560, ResolveStatusFlags @0x553ce8,
  BuildPersonCardSimple @0x55433c, Interaction_DispatchPanelEvent @0x595b98)
  — supplied through the env views / host edges; the consuming control flow
  is byte-exact.

## Tests (all passing)

* `tests/unit/session_panels_test.cpp` — **18 tests / 207 checks**:
  * `TooltipClampGoldenVectors`, `WeaponUserCodeMapping`,
    `ObjectMarketRowGate`, `ObjectValueRatioUsesExactDoubles` — leaf goldens
  * `BuildObjectEmitsOriginalSequenceAndProducerRows` — window order, icon and
    text ids, ingredient row, the REAL @0x6496A9 rows for item 468
  * `BuildObjectWeaponFormMarketRowAndClearToken`,
    `BuildObjectClampFiresAgainstScreenEdge`
  * `BuildUpgradeGateScanAndEffectLine` — class-29 gate, masked slot scan,
    kind-3 vs percent line, class-2 owner suppression
  * `BuildBuildingEmitsTitleIconAndValueLines`,
    `BuildPersonEmitsOriginalLineSequence`, `BuildContactEmitsFoundBranch`
  * `TooltipLifecycleBuildDedupHide` — build → dedup → hide → rebuild over the
    REAL @0x4f7424 dispatch, with pixel-band deltas
  * `ObjectClassByteSelectsObjectVsUpgradeBuilder` — the REAL class-byte
    arithmetic over the shadow object table
  * `TooltipBoxClampsAtTheRightScreenEdge`,
    `ContactTooltipResolvesHilfeKeyThroughTextDb`
  * `InfoPanelRebuildOnlyOnSelectionChange` — @0x4b84c0 dedup + slider fill +
    pixel band; `FrameIsDeterministicAcrossFreshInstances`;
    `DegenerateSurfaceIsANoOp`
* `tests/unit/session_hud_test.cpp` — extended to **11 tests / 58 checks**
  (+`PanelsNullKeepsLegacyFrameAndZeroPanelCounters`,
  `PanelsInputsCompositeTooltipAndInfoPanel`,
  `PanelsRenderIsDeterministicAcrossFreshInstances`).
* `tests/e2e/session_hud_e2e_test.cpp` — regression-green (4 tests / 35
  checks, GUILD_GAME_DIR-guarded).
