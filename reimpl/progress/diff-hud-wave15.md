# Wave-15 TRUE 1:1 binary diff — HUD / panels / input / selection (W15-HUD)

MCP was LIVE. Each function on the playable HUD/input/selection flow was
`decompile`d (and `disasm`/`get_bytes` where rounding/constants/tables mattered)
and compared LINE-FOR-LINE against the reconstruction in `src/`. The wave-13
NEEDS-LIVE-MCP queue was resolved. One real divergence was found and FIXED 1:1
(plus its golden corrected to the binary). All segment suites are green.

Owned files only were edited:
`src/play/input_recon_select.cpp`, `tests/unit/input_recon_select_test.cpp`,
this report. No bind-site / cross-segment source touched.

---

## FIXED (real divergence corrected to the binary)

### `Coord_ConvertX` (VIBE_Coord_ConvertX @0x5c6b08) — rounding mode was WRONG

`src/play/input_recon_select.cpp` implemented `Coord_ConvertX` as
`std::nearbyint` (round-to-nearest, ties-to-even), with a comment asserting the
original masked RC to 00. The **disassembly proves otherwise**:

```
5c6b08  fstcw  [cw]                ; save control word
5c6b11  mov    byte [cw+1], 1Fh    ; force HIGH byte -> CW = 0x1F7F
5c6b16  fldcw  [cw]
5c6b19  frndint                    ; RC = bits 11:10 of 0x1F7F = 0b11
5c6b1b  fldcw  [saved]
```

RC = `0b11` is **round toward zero (truncate)**, not round-to-nearest. So
`ConvertX(x) == trunc(x)`; the callers' subsequent `(int)` cast is a no-op.

- **Source fix:** `Coord_ConvertX` now returns `std::trunc(v)` with a disasm-
  cited comment. This makes it identical to the canonical `util::ConvertX`
  (`src/util/coord.cpp`, already `std::trunc`, the @0x5c6b08 recon with 1101
  xrefs) — the local copy had silently diverged.
- **Golden fix (the golden was wrong, per brief):**
  `tests/unit/input_recon_select_test.cpp` `RoundToNearestTiesEven` →
  `TruncateTowardZero`. Corrected the pins that diverge between the two modes:
  `ConvertX(2.6)=2` (was 3), `ConvertX(-2.6)=-2` (was -3), `ConvertX(1.5)=1`
  (was 2), `ConvertX(3.5)=3` (was 4); added `-0.9→0`, `-1.999→-1`. The values
  that agree under both modes (2.4, -2.4, 0.5, 2.5) are unchanged.
- **Blast radius:** `SelectEntity_ComputeResult` projects v31/v32 through
  `Coord_ConvertX`. The SelectEntity projection goldens use integer-valued
  u/v (30/40, 5), so they are unaffected. `world/money_format.cpp` already used
  `std::trunc` (its trunc model was correct all along; verified `fadd 0.5;
  ConvertX` == round-half-up via truncate). No money golden changed.

input_recon_select_test: 40 → 42 checks (the +2 toward-zero pins), 0 failures.

---

## VERIFIED-1:1 (decompile matches the reconstruction)

Money / clock (constants confirmed by `get_bytes`):
| Fn | Addr | Source | Notes |
|---|---|---|---|
| MoneyFormatWithSeparators | 0x58f798 | world/money_format.cpp | `dbl_6269BC`=0x3FE0…=0.5, `flt_6269C4`=0x3EAAAAAB=0.333333343f, `off_6269A4`="0%c", glyph 0x11. v24==0 prints "0%c" in BOTH sign branches (unified check). `fadd 0.5; ConvertX(=trunc)` == round-half-up. |
| MoneyGroupThousands | inside 0x58f798 | world/money_format.cpp | back-to-front fill, `.` every 3rd, `(v8+1)%3` cadence. Bounds guards are no-ops for valid input. |
| Clock_ComputeTimeOfDay | 0x527778 | gui/hud.cpp | `flt_622958`=0x3BCCCCCD=0.00625f, `dbl_622960`=0.5, `dword_63CC60`=0x64=100. `(tick*0.00625+0.5)*100`, ConvertX(=trunc), then /3600,%3600/60,%3600%60. The guard + GameTime_Advance are the caller's edge (pure arithmetic core is what's pinned). |

Tooltips (UI-bound builders; the consuming control flow + emission order + all
text-bias/icon-bias immediates verified against the decompile):
| Fn | Addr | Source | Notes |
|---|---|---|---|
| Tooltip_Dispatch | 0x4f7424 | gui/tooltip_dispatch.cpp | type resolve (object/upgrade/building/contact/person) + lifecycle. Object class 32/23/37 → BuildObject else BuildUpgrade; 206..1009 → object id; 1010..1081 → building; etc. |
| Tooltip_BuildObjectContent | 0x4f7a10 | gui/tooltip_content.cpp | name/desc bias 2151/2152, icon 206, producer table @0x6496A9 walk, id-461 suppression, weapon "used-by" codes, +market row gate, value-ratio (`dbl_6206D8`*`dbl_6206E0`). |
| Tooltip_BuildUpgradeContent | 0x4f8154 | gui/tooltip_content.cpp | class-29 → -1 gate, 64-slot owner scan (`&0x7FFF`), effect bias 3203, kind==3 → "+%a" else "%i%% %s". |
| Tooltip_BuildBuildingContent | 0x4f78e4 | gui/tooltip_content.cpp | color selector `v10[rec[583]]`, name stride/bias 14/1078, icon 1010, sale price. |
| Tooltip_BuildPersonContent | 0x4f84ac | gui/tooltip_content.cpp | head "$Z$[%1N3$]", job 294/370, traits 525/560, religion 272/279, class 1070, 5 skill rows (base 4810, 15px pitch, bar gfx 1162), `DispatchPanelEvent(7,…)` edge. |
| Tooltip_BuildContactContent | 0x4f83e8 | gui/tooltip_content.cpp | string-copy + uppercase + `_HILFE_%s+0` lookup is the host boundary; the found branch (`$Z$[%s$]` + text+1) is 1:1. |

InfoPanel / selection / input:
| Fn | Addr | Source | Notes |
|---|---|---|---|
| InfoPanel_Update | 0x4b84c0 | gui/infopanel.cpp | the (A) person/standard + (B) object/building dominance order + the 8-field change-snapshot OR-chain (0x4b85e3). `playerbar==-1` (dword_631768) gate documented; the dominant-builder model resolves the last-builder-wins order. |
| InfoPanel builders | 0x4b6930/0x4b64b0/0x4b6c80/0x4b6db8/0x4b7104 | gui/infopanel_build.cpp | form load + window select + icon/slider/action-sprite placement orders match. |
| SelectEntity_ComputeResult | 0x4147cc | input_recon_select.cpp | the goto graph (LABEL_6/27/29, the `++v6` → else-while re-entry), Y-bias `flt_610EBC`=0x40A00000=5.0f, half-tile +256 on the v13==1 flag, type-64 keep-scanning, ConvertX(=trunc) on v31/v32. |
| Selection_Reset | 0x4b9444 | input_recon_select.cpp | the four anchor zeroes, QueryBegin/QueryFind iter, type-29 `&= ~2` clear. |
| Selection_CommitContact | 0x4b950c | session_select.cpp | per-frame stale-worker drop (+392), QuickJump dual resolve, the long gate predicate, door-like `v14`, highlight pulse (+529 &1), worker selection (`v6=flags>>8`, &8), all anchor stores, type-29 teardown. |
| Selection_ClearAll | 0x4b94d8 | session_select.cpp | worker-mark sweep + 11BC270/631740/6317B0 zero, returns 411648. |
| Input_LatchMouseState | 0x40dab8 | session_input.cpp | soft-cursor mirror gate, 6-field edge reset, ring-row drain (field order incl. 672254-before-672250), keyboard tail. |
| Input_ProcessMouseClicks | 0x40cdd0 | session_input.cpp | L/R/M click + double-click windows (15/40 ticks, ±3px), the `v2<31` ring-spill guard, the 12-pair packet compare, 0x4C latch. |
| Input_PollKeyboardLatch | 0x40d920 (pure tail) | session_input.cpp | press/release tables, auto-repeat (+9 / +5 unsigned), sentinel `dword_672260`=1316134911, `al=-1` on key-up. |
| Input_PollMouseAndKeyboard | 0x40da88 | input_recon_select.cpp | mouse-poll → packet-copy(0x4C) → keyboard-poll order. |
| ComputeSelectionVolumeSolve | 0x5b7134 | picksel_recon.cpp | the bone-extent volume solver (two-triangle barycentric solve, `dbl_628708` epsilon, `flt_628710`). |

---

## RESOLVED — wave-13 NEEDS-LIVE-MCP queue

### Economy / person leaves the tooltips read (decompiled; confirmed consuming flow)
These leaves live in **src/sim** / **src/world** (other segments). I decompiled
each to confirm the tooltips' CONSUMING control flow is byte-exact; the leaf
bodies are reconstructed in their own segments.

- `Building_ComputeMarketPrice` @0x58f3d0 — confirmed: cached `(v3+56)` fast
  path (`32*v4*currency*flt_626948`, *flt_62694C if class 3); else the
  ingredient-fold compute, class-23 short-circuit, recursive sub-product
  divide. Tooltip consumes its result as `env.priceNow` (truncated via ConvertX).
- `Building_LookupCachedMarketPrice` @0x58f6b8 — confirmed: 62-row scan over
  `dword_13C3B5C` (stride 128, 7952/currency), falls back to ComputeMarketPrice
  on miss. Tooltip consumes as `env.priceCached`.
- `Person_GetCashAmount` @0x58bc9c — confirmed leaf: `(double)*(u16*)(&dword_12CE919[134*id]+1)`.
- `BuildingType_ComputeRankWithinGroup` @0x58a560 — confirmed: 12 contiguous
  6-wide group ranges, `hi - a1 + 1`, else 0. Tooltip consumes as `env.rank`.
- `Object_ComputeMarketValue` @0x594df0, `Person_ComputeTotalWealth` @0x591f7c,
  `Person_ResolveStatusFlags` @0x553ce8, `Hud_BuildPersonCardSimple` @0x55433c,
  `Interaction_DispatchPanelEvent` @0x595b98 — the tooltip's CALL ORDER and the
  fields each result feeds (`env.marketValue`, `env.wealth`, `env.statusCard`,
  the person-card x/y 166/5, the panel-event 7/15/18 edges) all match the
  decompile. Bodies belong to the sim/interaction segments.

**Status:** consuming flow VERIFIED-1:1; leaf bodies confirmed reachable and
owned by sibling segments (no change here, by ownership).

### DirectInput device bodies (rule-4 boundary)
- Keyboard ring drain `GetDeviceData` @0x40d920 — the OS read is the vtable call
  `(*(...)dword_672164 + 40)(…,16,&v3,v5,0)`; replaced by the shim key-event
  feed. The **pure repeat tail** (latch / +9 / +5 / sentinel / al=-1) is 1:1
  and golden. CONFIRMED 1:1 for the non-device logic.
- Relative-mickey decode `dword_62D0B8 * flt_610C5C` @0x40d588 — stays in the
  DirectInput decode (platform boundary). The absolute-cursor double-clamp
  branch (0x40d74c..0x40d786) is reconstructed 1:1 in `installPollHooks`.

---

## Still-documented gaps (named, not regressions)
- InfoPanel `playerbar==-1` (dword_631768) gate — modelled as panel-free (the
  common detail-build path); exact rare-occupied form deferred.
- `Text_RenderRichString` @0x59d6e8 — projected to plain glyph emission via the
  host (rich-text renderer is the text cluster's).
- `StatusText_Register` @0x4bcc80 — `src/gui/hud.cpp` carries a VALUE-MODEL
  (int-key de-dup → slot index) instead of the binary's name-string de-dup →
  object-handle return + `entry+536=1`. The stride-50 / 32-slot / append /
  overflow STRUCTURE is faithful; the de-dup KEY semantics differ. `gui/hud.cpp`
  is the shared gui-hud file (not in this segment's edit set) — flagged for the
  gui-hud segment to byte-align if desired. Not edited (ownership).
- `Object_ComputeBoneScreenExtents` corner fill (provider-supplied),
  `unk_75BA38` hotspot-strip scan @0x421793 (→ -1, gate-neutral) — unchanged.

---

## Build / test status (all green)

| suite | checks | Δ |
|---|---|---|
| world_money_format_test | 32 | — |
| gui_hud_test | 271 | — |
| session_panels_test | 250 | — |
| input_recon_select_test | 42 | +2 (corrected ConvertX golden) |
| session_select_test | 106 | — |
| session_input_test | 119 | — |
| picksel_recon_test | 86 | — |
| gui_tooltip_dispatch_test | 53 | — |
| gui_infopanel_build_test | 57 | — |
| play_input_command_test | 18 | — |
| session_hud_test | 58 | — |

One source fix (`Coord_ConvertX` nearbyint → trunc), one golden corrected to the
binary. Everything else VERIFIED-1:1.
