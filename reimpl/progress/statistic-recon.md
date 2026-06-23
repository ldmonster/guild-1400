# Statistics reconstruction (VIBE_Statistic / VIBE_StatChart / VIBE_StatPanel)

Cluster: game-statistics computation/aggregation — the per-round NPC text dump and the
official-comparison chart.  Strict 1:1 port from the Hex-Rays decompile.

## Files
- `src/world/statistic_recon_dump.{h,cpp}` — 9 round-dump text formatters.
- `src/world/statistic_recon_chart.{h,cpp}` — official-comparison aggregation + panel hook.
- `tests/unit/statistic_recon_test.cpp` — 36 golden-vector checks (all passing).

## Functions translated (addr -> symbol -> file)
| addr | original | reimpl symbol | file |
|------|----------|---------------|------|
| 0x594fd0 | VIBE_Statistic_DumpRoundHeader   | StatDumpRoundHeader        | dump |
| 0x594ff8 | VIBE_Statistic_DumpNpcIdentity   | StatDumpNpcIdentity        | dump |
| 0x5950a4 | VIBE_Statistic_DumpNpcAttributes | StatDumpNpcAttributes      | dump |
| 0x595168 | VIBE_Statistic_DumpNpcNeeds      | StatDumpNpcNeeds           | dump |
| 0x5951ac | VIBE_Statistic_DumpNpcTraits     | StatDumpNpcTraits          | dump |
| 0x595208 | VIBE_Statistic_DumpNpcSkills     | StatDumpNpcSkills          | dump |
| 0x595260 | VIBE_Statistic_DumpNpcInventory  | StatDumpNpcInventory       | dump |
| 0x5952dc | VIBE_Statistic_DumpNpcRecord     | StatDumpNpcRecord          | dump |
| 0x5953bc | VIBE_Statistic_DumpRoundFooter   | StatDumpRoundFooter        | dump |
| 0x58beb8 | VIBE_StatChart_BuildOfficialComparison | StatChartBuildOfficialComparison | chart |
| 0x55fa88 | VIBE_StatPanel_ShowCompareChart  | StatPanelShowCompareChart (UI hook) + CompareSliderColor | chart |

## Coupled leaves (routed through injectable env hooks — NOT faked, defaults inert)
- Person table `word_12CE910` (768 x 536-byte records, 268-word stride): type byte +2,
  needs +128..+132, religion +13, trait/office ranks +358/+361 — via `StatChartEnv`/raw
  record pointer + `StatDumpEnv`.
- Localized string tables `dword_8C4774/8C4768/8C3AF0/8C3B48/8C3E0C/8C3EE4`, identity
  `aDummyNpc/dword_649CB4` — `StatDumpEnv::*Name()`.
- `VIBE_Person_SumCurrencyHeld` 0x59152c, `VIBE_Person_ComputeTotalWealth` 0x591f7c,
  `VIBE_Person_FindRecordById` 0x58bc6c — `StatDumpEnv` / `StatChartEnv`.
- `VIBE_Office_GetDefinition` 0x47f008 (byte +2), `VIBE_Ai_ComputePersonFavorability`
  0x594330, `VIBE_Coord_ConvertX` 0x5c6b08 — `StatChartEnv`.
- Round counter `qword_13CE852` — `StatDumpEnv::CurrentRound()`.

These already have refactored reimplementations elsewhere (inventory_wealth.cpp,
office.cpp, favorability.cpp, util/coord.cpp) with different (view/vector) signatures;
the env-hook boundary lets a caller forward to them without an ODR clash or a fake.

## Float constants (chart, gilde.exe .rdata, defined from exact IEEE bits)
flt_6267CC=1e-4 (maxWealthScale), 6267D0=0.3 (ratioW), 6267D4=0.15 (mid W),
6267D8=0.25 (needsW), 6267DC=needsScale, 6267E0=religionScale(1/6),
6267E4=officeScale(1/13), 6267E8=favorScale(1/100).

## Deferred / divergences
- `StatDumpNpcAttributes` 7th `%i` field reads uninitialized stack (`[ebp-Ch]`) in the
  original; indeterminate value cannot be faithfully reproduced -> emit 0 (documented;
  field is never consumed downstream).
- `StatPanelShowCompareChart` form/widget/modal-loop body (Form/Object/Widget/Text/
  GameLogic_RunFrameLoop) is a UI leaf -> inert `StatPanelCompareSink`; only the pure
  slider color-index clamp (`CompareSliderColor`, palette dword_552704) is reproduced.

## Wiring (xrefs_to)
- The dump tree roots at `StatDumpNpcRecord` (0x5952dc), which fans out to identity/
  attributes/needs/traits/skills/inventory — fully wired internally.
- Callers of 0x594fd0/0x5953bc/0x5952dc (the per-round dump driver) and 0x55fa88 (panel
  menu entry) live outside this cluster; once their owners are reconstructed they bind
  to these symbols + supply concrete env/sink implementations.

## Tests
36 golden checks: header/footer text, needs/traits/identity/attributes(bitfield)/skills/
inventory formatting, chart single-official golden (needs/religion/office/favor/ratio/
composite cross-checked bit-exact), selection cap at 8, slider color clamp, panel hook.
