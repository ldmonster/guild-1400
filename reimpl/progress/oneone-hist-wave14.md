# Wave-14 1:1 fidelity audit — world history/statistics cluster (W14-HIST)

MCP DOWN → no live binary diff. This is an in-tree 1:1 audit of the world
history/statistics cluster: inventory every reconstructed function with its
gilde.exe provenance address, pin the recovered 1:1 values (record grammar,
keyword tables, stats tables, cart table, dispatch table) with golden tests,
verify source matches the provenance comments + progress docs, and produce a
confidence map + a precise binary-diff target queue for when MCP returns.

Scope: `src/world/` history*.{h,cpp}, world_history2.{h,cpp},
statistics*.{h,cpp}, statistic_recon_*.{h,cpp}, lookup_table_save_recon.{h,cpp}
plus their tests.

## What changed this wave (TEST additions only — no source edits)
- `tests/unit/world_history_full_test.cpp`: `RoleNameTableFullByteExact` (all 15
  role names by index + stride 64), `PrefixTableFullByteExact` (the 5-entry
  prefix table), and recipient-end/kind constant pins. Added `<string>` include.
- `tests/unit/world_history_cmdline_pass_test.cpp`: `CommandTableFullByteExact`
  (all 27 command keywords by index + stride 64).
- `tests/unit/history_cart_table_test.cpp`: `LayoutConstants` (group/slot/dword
  counts + the ChronicleState/FreeFiles 4-slot asymmetry).
- All values traced to the source's own recovery comments / progress docs — none
  invented. Existing goldens kept byte-identical. Builds green.

Test counts after this wave: world_history_full_test 106→147,
world_history_cmdline_pass_test 74→103, history_cart_table_test 117→121.

---

## INVENTORY (function → address → file → confidence)

### history.{h,cpp} — ParseDate / scan classifier / Notify batch 1
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fe2d4 | VIBE_History_ParseDate (parse half)   | HistoryParseDate            | GOLDEN-PINNED |
| 0x4fe694 | VIBE_History_ScanNextEventForward (cls)| HistoryClassifyEntry        | GOLDEN-PINNED |
| (shared) | VIBE_History_Notify* kind gate         | HistoryNotifyKindIsImportant| GOLDEN-PINNED |
| 0x535780 | VIBE_History_NotifyArrestTarget        | HistoryNotifyArrestTextId   | GOLDEN-PINNED (7313/7314/7315, day<117) |
| 0x5357f8 | VIBE_History_NotifyUseItemEvent        | HistoryNotifyUseItemTextId  | GOLDEN-PINNED (7316) |
| 0x535844 | VIBE_History_NotifyBuildingLinkRemoved | …BuildingLinkRemovedTextId  | GOLDEN-PINNED (7318) |
| 0x535890 | VIBE_History_NotifyOfficeTransfer      | …OfficeTransferTextId       | GOLDEN-PINNED (7317) |
| 0x535d88 | VIBE_History_NotifyCrimeAdded          | …CrimeAddedTextId           | GOLDEN-PINNED (7328) |
| 0x536070 | VIBE_History_NotifyRivalEvent          | …RivalEventTextId           | GOLDEN-PINNED (4954, ungated) |
| 0x535d20 | VIBE_History_NotifyLawChangeToMaster   | …LawChangeTextId            | GOLDEN-PINNED (7327) |
| 0x535f64 | VIBE_History_NotifyOfficeSwap          | …OfficeSwapTextId           | GOLDEN-PINNED (6604/6605) |

Record grammar pinned: date is fixed-width `DD.MM.YYYY`, day=chars[0:2],
month=chars[3:5], year=chars[6:10]; strlen==10 gate; zero day→1 (mode 1), zero
month→1 (mode 2); yearOffset = year-1400 (kChronicleBaseYear). Scan diff =
currentDay-entryDay: ==1 emit, <1 stop, >1 continue.

### history_parse.{h,cpp} — formatter + target Notify
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fe2d4 | ParseDate (format inverse) | HistoryFormatDate / HistoryRoundtripDate | GOLDEN-PINNED |
| 0x535afc | VIBE_History_NotifyTargetFound    | …TargetFoundTextId (3953, voice 3960) | GOLDEN-PINNED |
| 0x535bb0 | VIBE_History_NotifyTargetReachedA | …ReachedA (3963; voice 3964/3967; lead 3970) | GOLDEN-PINNED |
| 0x535c68 | VIBE_History_NotifyTargetReachedB | …ReachedB (3973; voice 3974/3977; lead 3980) | GOLDEN-PINNED |

### history_full.{h,cpp} — FirstPass validator, token classifier, scan step, tables
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fd220 | VIBE_History_ParseTextFirstPass | HistoryParseTextFirstPassValid | GOLDEN-PINNED |
| 0x4fd44c | ParseContext (prefix-mode part) | HistoryClassifyTokenMode       | GOLDEN-PINNED |
| 0x4fd44c | ParseContext (slot digit part)  | HistoryTokenSlot               | UNDER-VERIFIED — see DRIFT |
| 0x4fe694 | ScanNextEventForward (step)     | HistoryScanStep                | GOLDEN-PINNED (codes 1/2/3/4/6) |
| 0x6343B8 | dword_6343B8 prefix table       | kHistoryPrefixTable[5]         | GOLDEN-PINNED (byte-exact, NEW this wave) |
| 0x633FF8 | aBuergermeister_3 role table    | kHistoryRoleNames[15] / HistoryRoleNameIndex | GOLDEN-PINNED (full 15, NEW this wave) |

### history_scan.{h,cpp} — real-day scanner classifier
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fe9cc | VIBE_History_ScanNextEventReal (cls) | HistoryScanRealStep / …Continues | GOLDEN-PINNED (codes 1/2/4 terminate; 0/5/6 loop) |

### history_chronicle.{h,cpp} — Notify batch 2 + plague RNG + in-memory chronicle
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x5358e0 | NotifyWanderEventA   | ChronicleWanderATextId (7319)  | GOLDEN-PINNED |
| 0x535928 | NotifyWanderEventB   | ChronicleWanderBTextId (7320)  | GOLDEN-PINNED |
| 0x535970 | NotifyWanderPairEvent| ChronicleWanderPairLines (7321/7322, kind!=7) | GOLDEN-PINNED |
| 0x535a04 | BroadcastAttackEvent | ChronicleAttackDefenderTextId (7323) + Suffix (7325/7326) | GOLDEN-PINNED |
| 0x535dd8 | BroadcastPlagueOutbreak | ChroniclePlagueOutbreakTextId (7329+RandomModulo(3)) | GOLDEN-PINNED |
| 0x535e64 | NotifyPlagueSpreadStep  | ChroniclePlagueSpreadStepTextId (7332+r, draw-before-gate) | GOLDEN-PINNED |
| 0x535edc | BroadcastPlagueSpread   | ChroniclePlagueSpreadTextId (7335+RandomModulo(2)) | GOLDEN-PINNED |
| 0x58b89c | VIBE_Math_RandomModulo  | RandomModulo (n==0→0)          | GOLDEN-PINNED |
| (model)  | in-memory chronicle add/scan/format | Chronicle::Add/ScanNextForward/FormatDate/At | GOLDEN-PINNED (+ wave-12 OOB guards) |

### history_second_pass.{h,cpp} — bracket collapser + label orchestrator
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fe0ec | VIBE_History_ParseTextReal      | HistoryCollapseLabel  | GOLDEN-PINNED ([keep#drop]→keep, two MemMove) |
| 0x4fe0b0 | VIBE_History_ParseLabelPasses   | HistoryRunLabelPasses | GOLDEN-PINNED (3-stage gate) |
| 0x5d9310 | VIBE_Util_MemMove               | HistoryMemMove        | GOLDEN-PINNED |

### history_text_pass.{h,cpp} — text ("fake") second pass
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fdcec | VIBE_History_ParseTextSecondPass | HistoryParseTextSecondPass | GOLDEN-PINNED (token grammar: '_', ≤2 '-', digit-class run via byte_64A208) |

### history_commandline_pass.{h,cpp} — commandline 2nd pass + command table
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x633938 | aFest command-keyword table     | kHistoryCommandNames[27] | GOLDEN-PINNED (full 27, NEW this wave) |
| (scan)   | command-keyword match           | HistoryCommandIndex      | GOLDEN-PINNED (leading-prefix, 27 sentinel) |
| 0x6343C7/CC | group prefix dwords "_SET"/"_USE" | HistoryClassifyGroupRef | GOLDEN-PINNED (slot 0..3 gate) |
| 0x4fd8ac | VIBE_History_ParseCommandlineSecondPass | HistoryParseCommandlineSecondPass | GOLDEN-PINNED |

### history_cart_table.{h,cpp} — group-slot table lifecycle
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4fd140 | VIBE_History_ResetGroupSlot      | HistoryResetGroupSlot (all 8 slots, id-1/kind0xFF, flag1) | GOLDEN-PINNED |
| 0x4fd090 | VIBE_History_ResetChronicleState | HistoryResetAllGroups (first 4 slots only) | GOLDEN-PINNED (asymmetry pinned) |
| 0x4fd194 | VIBE_History_FreeChronicleFiles  | (same reset core)        | GOLDEN-PINNED |
| 0x4fd6ac | ParseCommandlineFirstPass (group core) | HistoryParseCommandlineGroupIndex | GOLDEN-PINNED (<4 gate, _SET resets) |

Table grammar pinned: 4 groups, 17-dword (68-byte) stride, 8 {id@+4 step8 dword,
kind@+8 step8 byte} slots, active flag @+0. Free = id -1, kind 0xFF.

### history_mission.{h,cpp} — mission dialog frame loops
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x5387c8 | VIBE_Mission_RunSpecialDialog       | MissionSpecialStep / MissionRunSpecialDialog | GOLDEN-PINNED (1210 accept / 1155 decline) |
| 0x539e8c | VIBE_Mission_RunFailureDialog       | MissionAckStep / MissionRunFailureDialog | GOLDEN-PINNED |
| 0x53a41c | VIBE_Mission_RunInfoDialog          | MissionAckStep / MissionRunInfoDialog | GOLDEN-PINNED |
| 0x538950 | VIBE_Mission_RunChooseMissionDialog | MissionChooseStep | GOLDEN-PINNED |
| 0x53ac34 | VIBE_Mission_RunCompletionDialog    | MissionCompletionStep | GOLDEN-PINNED |
| 0x53a854 | VIBE_Mission_RunOfferDialog         | MissionOfferStep / …TriggersReload | GOLDEN-PINNED |
| 0x539fd8 | VIBE_Mission_RunRewardSummary       | (declared in scope; not in this .cpp) | NEEDS-LIVE-MCP (see queue) |

### world_history2.{h,cpp} — VIBE_He_* engine tail
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4c45e4 | VIBE_He_ValidatePunishmentType | He_ValidatePunishmentType (-1/-3/-4; +358 type5, +13 type7) | GOLDEN-PINNED |
| 0x4c5244 | VIBE_He_NullHandler            | He_NullHandler        | GOLDEN-PINNED (empty) |
| 0x4c51b4 | VIBE_He_LogInvalidAllocType    | He_LogInvalidAllocType| UNDER-VERIFIED (string text) |
| 0x4c51fc | VIBE_He_LogInvalidRunType      | He_LogInvalidRunType  | UNDER-VERIFIED (string text) |
| 0x4c5018 | VIBE_He_ProcessAllPlayerNews   | He_ProcessAllPlayerNews (0..767, 0x300) | GOLDEN-PINNED |
| 0x4c68d8 | VIBE_He_DestroyIconGfx         | He_DestroyIconGfx     | GOLDEN-PINNED (node/no-node paths) |
| 0x4c6c50 | VIBE_He_DestroyStaleIcons      | He_DestroyStaleIcons  | GOLDEN-PINNED |
| 0x4c6ca4 | VIBE_He_DestroyIconsForEntity  | He_DestroyIconsForEntity | UNDER-VERIFIED (matches +8/mesh field — see DRIFT note) |
| 0x4c67e0 | VIBE_He_CreateGfxInfo          | He_CreateGfxInfo      | GOLDEN-PINNED (64-slot scan, parent+136) |

Icon-pool grammar pinned: 64 slots, 16-byte stride: entityId@+0 (-1 free),
parent@+4, mesh@+8, node@+12 (dword_11C6160).

### statistics.{h,cpp} / statistics_report.{h,cpp} / statistics_full.{h,cpp}
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x579ad0 | BuildEconomyReport (float reduce) | StatisticsAccumulateCategoryTotals / StatisticsCategoryTotal | GOLDEN-PINNED (total[k]=(a[k]+a[k+5]+a[k+10]+a[k+15])*0.25) |
| 0x57a900 | ShowTaxWindow (row→round)        | StatisticsTaxRowRound (currentRound-16+row, 17 rows) | GOLDEN-PINNED |
| 0x579af5 | report cadence gate              | EconomyReportShouldRun (≥8 && %4==0) | GOLDEN-PINNED |
| 0x579bac | trend ids                       | EconomyReportTrendIds (6174..6181, 0.15 thr) | GOLDEN-PINNED |
| 0x579c1a | weakest argmin                  | EconomyReportWeakest (exact short-circuit chain) | GOLDEN-PINNED |
| 0x579d03 | weak-line gate                  | EconomyReportEmitsWeakLine (>0.20) | GOLDEN-PINNED |
| 0x579d28 | severity tiers                  | EconomyReportWeakSeverity (0.30/0.50) | GOLDEN-PINNED |
| 0x579d5b | luxury branch                   | EconomyReportLuxuryBranch (>0.55/<0.45) | GOLDEN-PINNED |
| 0x579e3c | recipient broadcast walk        | ReportEligibleRecipientCount (stride 134, end 102912, kind 6) | GOLDEN-PINNED |

Thresholds pinned (dbl_625714..62573C): 0.15/0.20/0.30/0.50/0.55/0.45.
Scale flt_62570C = 0.25 (0x3E800000).

### statistic_recon_chart.{h,cpp} — official-comparison aggregation
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x58beb8 | VIBE_StatChart_BuildOfficialComparison | StatChartBuildOfficialComparison | GOLDEN-PINNED (selection {5,6,7} cap 8, 5-term composite) |
| 0x55fa88 | VIBE_StatPanel_ShowCompareChart  | StatPanelShowCompareChart + CompareSliderColor | GOLDEN-PINNED (slider palette clamp [0,7]) |

Float consts pinned by IEEE bits (flt_6267CC..6267E8) and the slider palette
dword_552704 {0x7E,0x84,0x8A,0x90,0x96,0x9C,0xA2,0xA8}.

### statistic_recon_dump.{h,cpp} — round-dump text formatters
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x594fd0 | DumpRoundHeader   | StatDumpRoundHeader   | GOLDEN-PINNED |
| 0x5953bc | DumpRoundFooter   | StatDumpRoundFooter   | GOLDEN-PINNED |
| 0x594ff8 | DumpNpcIdentity   | StatDumpNpcIdentity   | GOLDEN-PINNED (12-field, offsets +4/+0/+48/+8/+9/+10/+12/+13/+356/+357) |
| 0x5950a4 | DumpNpcAttributes | StatDumpNpcAttributes | UNDER-VERIFIED — 7th %i junk field divergence (documented) |
| 0x595168 | DumpNpcNeeds      | StatDumpNpcNeeds      | GOLDEN-PINNED (+128..+132) |
| 0x5951ac | DumpNpcTraits     | StatDumpNpcTraits     | GOLDEN-PINNED (+358..+361) |
| 0x595208 | DumpNpcSkills     | StatDumpNpcSkills     | GOLDEN-PINNED (+0x190..+0x1A4, wealth-then-currency eval order) |
| 0x595260 | DumpNpcInventory  | StatDumpNpcInventory  | GOLDEN-PINNED (8 ids @+92 step4; NIEMAND/-1) |
| 0x5952dc | DumpNpcRecord     | StatDumpNpcRecord     | GOLDEN-PINNED (marker!=0xFFFF gate, 8120 zero-fill) |

### lookup_table_save_recon.{h,cpp}
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| 0x4bad28 | VIBE_Table_FindEntrySlotById | TableFindEntrySlotById | GOLDEN-PINNED (stride 67, limit 4288, 64 entries, zero-on-hit, return idx*4) |

### wire_history_amt.{h,cpp}
| addr | symbol | reimpl | conf |
|------|--------|--------|------|
| (rule-13 wiring) | InstallRealHistoryAmtWiring | InstallRealHistoryAmtWiring | GOLDEN-PINNED (binds AmtEconomy2 + MissionReqEvent hook fields) |

---

## ADDRESS-LESS / RED FLAGS
None. Every reconstructed function in the cluster carries a `gilde.exe 0xXXXX`
provenance header. The two "model" helpers (in-memory `Chronicle`, `HeIconPoolReset`)
are explicitly documented as engine-state models / test helpers, not 1:1 functions —
acceptable.

One DECLARED-but-not-in-this-cpp: `VIBE_Mission_RunRewardSummary 0x539fd8` /
`RunCompletionDialog 0x53ac34` / `RunOfferDialog 0x53a854` are named in
history_mission.h's header comment as part of the cluster, and their pure decode
helpers (MissionCompletionStep / MissionOfferStep) ARE present, but the full driver
BODIES for RewardSummary/Completion/Offer are not in history_mission.cpp (only
Special/Failure/Info bodies are). Their decode cores are pinned; the frame-loop
bodies are a NEEDS-LIVE-MCP item (not a red flag — the helpers are wired).

---

## INTERNAL CONSISTENCY / DRIFT

### DRIFT-1 (documented, NOT fixed) — HistoryTokenSlot slot-digit offset
`history_full.cpp HistoryTokenSlot` reads the slot digit at `token[4]`, while the
binary's ParseContext (0x4fd44c) and the live reconstruction
`sim/history_parse.cpp HistoryParseContext` read `token[5]`. This is already
documented in `progress/history-parse-context.md` ("Known sibling discrepancy").
`HistoryTokenSlot` is a STANDALONE classifier helper NOT on the dispatch path
(ParseContext does its own slot read), so the discrepancy is observationally inert
on the live chain. The existing golden `HistoryTokenSlot("_NEW3",kNew)==3` is
consistent with the current token[4] read but cannot be confirmed correct without
the binary. → NEEDS-LIVE-MCP to settle which offset is canonical for this helper.
Left unchanged (not a clear evidence-backed drift — the two siblings disagree and
the binary is the tiebreaker; MCP down).

### DRIFT-2 (documented, NOT fixed) — He_DestroyIconsForEntity match field
The header comment says match on the +8 field which it labels "the parent-object
pointer CreateIconMesh stores there"; the struct field at +8 is `mesh`. The
function matches `parent == g_iconPool[i].mesh`. The decompile pseudo
(`*(dword_11C6168 + i) == field +8`) and CreateGfxInfo (which stores the parent
handle into the mesh slot via createIconMesh) are consistent with this, but the
field NAME (`mesh`) vs the SEMANTIC ("parent-object pointer") is confusing. No
behavioral drift — the code matches the documented decompile. → NEEDS-LIVE-MCP to
confirm the +8 field semantics and rename if warranted. Left unchanged.

### Verified CONSISTENT (source ↔ provenance ↔ docs)
- All keyword/role/prefix table contents match the provenance comments AND the
  parse-context doc dispatch table (0x6343d8) keyword column, now byte-pinned.
- Cart-table stride/asymmetry, recipient stride/end/kind, stats thresholds/scale,
  lookup stride/limit, icon-pool stride — all match their source recovery comments
  and are now explicitly constant-pinned.
- wave-12 OOB guards (cmdline strncmp, chronicle bounds) verified still present and
  behavior-identical on valid inputs.

---

## NEEDS-LIVE-MCP QUEUE (exact decompile targets for the binary diff)
1. `decompile 0x4fd44c` (VIBE_History_ParseContext) — confirm the slot-digit byte
   offset (token[4] vs token[5]) to settle DRIFT-1 for the standalone
   HistoryTokenSlot helper.
2. `decompile 0x4c6ca4` (VIBE_He_DestroyIconsForEntity) + `get_bytes` of the icon
   pool globals (dword_11C6160/6164/6168) — confirm the +8 match-field semantics
   (DRIFT-2).
3. `decompile 0x539fd8 / 0x53ac34 / 0x53a854` (RewardSummary / Completion / Offer
   dialog BODIES) — translate the full frame-loop driver bodies (decode cores
   already pinned in history_mission.cpp).
4. `decompile 0x4c51b4 / 0x4c51fc` — confirm exact diagnostic format strings for
   He_LogInvalidAllocType / He_LogInvalidRunType (UNDER-VERIFIED string text) and
   the player-name index expression word_12CE910[268*rec[+8]+24].
5. `decompile 0x5950a4` (DumpNpcAttributes) — confirm the 7th %i uninitialized
   stack field (documented divergence; emit 0). Verify the 9 packed bitfield
   shift/mask sequence off the +44 dword byte-for-byte.
6. `get_bytes 0x633938` (aFest) / `0x633FF8` (aBuergermeister_3) / `0x6343B8`
   (prefix table) — re-confirm the now-fully-pinned 27 / 15 / 5 entries byte-for-byte
   against the live module (cross-check the golden literals added this wave).
7. `get_bytes 0x62570C` and the dbl_625714..62573C block — re-confirm the stats
   scale (0.25) and the six double thresholds.

## Status
All edited cluster test targets build and pass in the normal build:
world_history_full_test (147), world_history_cmdline_pass_test (103),
history_cart_table_test (121). No source files changed; only golden test
additions. build/ green.
