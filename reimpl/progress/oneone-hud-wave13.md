# Wave-13 1:1 audit — HUD / panels / input / selection (W13-HUD)

MCP was DOWN, so this is the MCP-free half of "1:1 comparison": for each
reconstructed function on the playable HUD/input/selection flow, cross-check the
source against its provenance comments + progress docs, confirm a golden test
pins its recovered 1:1 values, and classify confidence. No bind-site files
(city_view3d / sdl_session / universe_render / scene_view / terrain_render) were
touched. The only source-tree change is TEST additions; no behavior changed.

## Segment inventory (function → provenance address → source)

| Function | Addr | Source file | Confidence |
|---|---|---|---|
| MoneyFormatWithSeparators | 0x58f798 | src/world/money_format.cpp | GOLDEN-PINNED |
| MoneyGroupThousands (positive ≥1000 branch) | inside 0x58f798 | src/world/money_format.cpp | GOLDEN-PINNED |
| Clock_ComputeTimeOfDay | 0x527778 | src/gui/hud.cpp | GOLDEN-PINNED |
| StatusText_Register (50-dword stride, 32 slots) | 0x4bcc80 | src/gui/hud.cpp | GOLDEN-PINNED |
| DamageLabel_Register (same cluster) | 0x4bad5c | src/gui/hud.cpp | GOLDEN-PINNED |
| MapView_ComputeMarkerScreenPos | 0x5440b4 | src/gui/mapview.cpp | GOLDEN-PINNED |
| MapView_StepScrollOffset | 0x543bd0 | src/gui/mapview.cpp | GOLDEN-PINNED |
| Tooltip_Dispatch (lifecycle) | 0x4f7424 | src/gui/tooltip_dispatch.cpp | GOLDEN-PINNED |
| Tooltip_BuildObjectContent | 0x4f7a10 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_BuildUpgradeContent | 0x4f8154 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_BuildBuildingContent | 0x4f78e4 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_BuildPersonContent | 0x4f84ac | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_BuildContactContent | 0x4f83e8 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_ClampToScreen | 0x4f7aa5/0x4f81c2 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_WeaponUserCodes | 0x4f7e32..0x4f80df | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_ObjectMarketRow | 0x4f7ebc | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| Tooltip_ObjectValueRatio | 0x4f7c27..0x4f7c75 | src/gui/tooltip_content.cpp | GOLDEN-PINNED |
| kTooltipProductionTable (486 raw bytes) | 0x6496A9 | src/gui/tooltip_content.cpp | GOLDEN-PINNED (added) |
| InfoPanel_Update / _SelectBuilder / _SelectionChanged / _Snapshot | 0x4b84c0 | src/gui/infopanel.cpp | GOLDEN-PINNED |
| SelectEntity_ComputeResult | 0x4147cc | src/play/input_recon_select.cpp | GOLDEN-PINNED |
| Selection_Reset | 0x4b9444 | src/play/input_recon_select.cpp | GOLDEN-PINNED |
| Coord_ConvertX (round-to-nearest) | 0x5c6b08 | src/play/input_recon_select.cpp | GOLDEN-PINNED |
| Input_PollMouseAndKeyboard (orchestrator) | 0x40da88 | src/play/input_recon_select.cpp | GOLDEN-PINNED |
| Selection_CommitContact | 0x4b950c | src/play/session_select.cpp | GOLDEN-PINNED |
| Selection_ClearAll | 0x4b94d8 | src/play/session_select.cpp | GOLDEN-PINNED |
| Selection_UpdateStatusTextLatch | 0x4bc280 tail | src/play/session_select.cpp | GOLDEN-PINNED |
| Input_LatchMouseState | 0x40dab8 | src/play/session_input.cpp | GOLDEN-PINNED |
| Input_ProcessMouseClicks | 0x40cdd0 | src/play/session_input.cpp | GOLDEN-PINNED |
| Input_PollKeyboardLatch (pure tail) | 0x40d920 | src/play/session_input.cpp | GOLDEN-PINNED |
| GameTickMainLoop (reused on flow) | 0x414a38 | src/sim/gametick_entityscan_recon.cpp | GOLDEN-PINNED (out of segment) |
| ComputeSelectionVolumeSolve (pick kernel) | 0x5b7134 | src/play/picksel_recon.cpp | GOLDEN-PINNED |
| PickBucketsSeed / Nearest / ScreenDistTest | 0x5b5aec/0x5b5b6c/0x5b5938 | src/play/picksel_recon.cpp | GOLDEN-PINNED |
| DragSelect* / DragCursor* / Interaction* | 0x4bdb98.. / 0x4c094c / 0x595e54.. | src/play/picksel_recon.cpp | GOLDEN-PINNED |
| ClassifyOrder / input→command bridge | 0x488ff0 cluster | src/play/input_command.cpp | GOLDEN-PINNED |
| SessionHud::Render (composite layer) | composite | src/play/session_hud.cpp | GOLDEN-PINNED |

No RED-FLAG functions: every reconstructed function in the segment carries a
`// gilde.exe 0xXXXX` provenance header.

## Golden pins added this wave (TEST-only)

`tests/unit/session_panels_test.cpp` (+2 tests, +43 checks → 250 total):

* **`ProducerTableByteExactGolden`** — pins the RAW recovered bytes of the
  @0x6496A9 producer table that previously had only a *behavioral* row-walk
  assertion (item-468 rows). Now asserts: total size = 486 (23×21+3), row-0
  producer code 0x29 (41) at +3 with item words 468/341/342 at +8/+10/+12,
  row-1 code 0x2A with 468/344/343, row-21 (offset 441) code 0x1F (31) with
  item 461 (0x01CD) at +12, and the 3 trailing overflow bytes = 0 (the last
  unaligned int read at +482). All values traced to the source array itself
  (verified by re-parsing `kTooltipProductionTable`), not invented.

  NOTE during pinning: the table's per-item slot offsets are *not* simply
  row+4 — the original walk reads `(int at row+off+2)>>16` for off=0..18 and
  the real item words sit at +8/+10/+12.. (the misaligned read the doc warns
  about). The added golden therefore pins **raw bytes at fixed indices**, the
  truly byte-exact form, rather than re-deriving the (misaligned) slot model.

* **`ObjectTextBiasConstantsGolden`** — directly pins the recovered immediate
  constants the tooltip builders use (previously exercised only transitively):
  obj name/desc text bias 2151/2152, icon bias 206, building icon bias 1010,
  building name stride/bias 14/1078, upgrade-effect bias 3203, market icon/text
  1039/1484, screen margin 16, row geometry 40/22, the full person text-base set
  (294/370/525/560/272/279/1070/4810, 5 rows, 15px pitch, bar gfx 1162), and the
  exact double bit pattern `dbl_6206D8 = 0x3F70410441041010` plus the 0.25/0.25/0.5
  scale doubles.

## Internal consistency check (drift hunt)

No drift found. Spot-checked the source constants against their provenance
comments and the progress docs:

* Clock: `kClockTickScale = 0.00625f` (flt_622958, = 1/160), `kClockTickBias =
  0.5` (dbl_622960), `g_dayLengthSeconds = 100` (dword_63CC60) — match
  session-hud.md and the in-test golden `(tick*0.00625+0.5)*100`.
* Status table: stride 50 dwords / 32 slots — matches the de-dup + overflow
  golden (slot 0..31, then -1).
* Marker projection scales (dbl_624058=0.5, flt_624060=5.33, flt_624064=0.5,
  flt_624068=51.0) match mapview.cpp and the roundtrip golden.
* Money: `kMoneyFormatRoundBias = 0.5`, `kCurrencyGlyph = 0x11`, flt_6269C4 =
  0.333333343f thousands grouping — match the grouping/negative/extreme goldens.
* SelectEntity: Y-bias `flt_610EBC = 5.0f`, the type-64 keep-scanning branch,
  the half-tile `+256`, ConvertX round-to-nearest-ties-even — all golden.
* Producer table size/values now self-consistent between source array and test.

## Confidence map

* **GOLDEN-PINNED (high 1:1 confidence):** every function in the inventory
  above. Constants, table bytes, control-flow edge cases, fixed-point/rounding,
  integer wraparound and observable side effects all carry golden assertions.
* **UNDER-VERIFIED:** none remaining in this segment after the two added pins
  (the producer-table raw bytes and the builder text-bias immediates were the
  only values reached on the flow without a direct golden; both are now pinned).
* **NEEDS-LIVE-MCP (cannot be confirmed from in-tree evidence — exact decompile
  targets to binary-diff when MCP returns):**
  - The economy/person leaves whose *results* are supplied through env views
    (the builders' consuming control flow is byte-exact, but the leaf bodies are
    out of this segment): ComputeMarketPrice @0x58f3d0, LookupCachedMarketPrice
    @0x58f6b8, ComputeMarketValue @0x594df0, Person_GetCashAmount @0x58bc9c,
    ComputeTotalWealth @0x591f7c, ComputeRankWithinGroup @0x58a560,
    ResolveStatusFlags @0x553ce8, BuildPersonCardSimple @0x55433c,
    Interaction_DispatchPanelEvent @0x595b98.
  - The platform-boundary device bodies routed via hooks (rule 4): the
    DirectInput relative-mickey decode `dword_62D0B8 * flt_610C5C` @0x40d588 and
    the keyboard GetDeviceData ring drain @0x40d920 (the *pure* repeat tail is
    1:1 and golden; only the OS read is boundary).
  - The InfoPanel `playerbar == -1` (dword_631768) gate and the rich-text
    renderer `Text_RenderRichString @0x59d6e8` (projected to plain glyphs) —
    documented named gaps; their exact form needs the binary.
  - Named gaps already documented in the progress docs (not regressions):
    Shape converters @0x5d7c0c/@0x5d7924 (CLOSED in render/shape_convert16),
    Object_UpdateGateContact @0x4b8ba8 (resolveContact provider boundary),
    the unk_75BA38 hotspot-strip scan @0x421793 (→ -1, gate-neutral),
    Object_ComputeBoneScreenExtents @0x5b7134 corner fill (provider-supplied).

## Build / test status

Segment suites all green after the additions:

| suite | checks |
|---|---|
| session_panels_test | 250 (was 207; +43) |
| world_money_format_test | 32 |
| gui_hud_test | 271 |
| mapview_test | 58 |
| input_recon_select_test | 40 |
| session_select_test | 106 |
| session_input_test | 119 |
| picksel_recon_test | 86 |
| gui_tooltip_dispatch_test | 53 |
| gui_infopanel_build_test | 57 |
| play_input_command_test | 18 |
| session_hud_test | 58 |

All existing goldens kept byte-identical; only additive TEST cases were added.
