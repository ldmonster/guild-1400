# Harden report — chunk world_03 (src/world: locations, market, mission, money, mood, office)

Method: every provenance-tagged function decompiled via the IDA MCP (plus raw `disasm`
where Hex-Rays collapsed registers), every `@0x…` table diffed programmatically with
`get_bytes`. Fixes applied ONLY for divergences proven against the binary; addresses
cited per finding.

## Fixed (4 proven divergences)

| Where | Address | Divergence | Binary evidence |
|---|---|---|---|
| `market_stall.cpp` `TradeBuildSortedItemList` | 0x51b26c | Sort direction inverted: reimpl swapped when `keyJ > keyI` (descending, "ZZZZ" first); binary swaps when `keyI > keyJ` (ascending, home "AAAA" first) | `VIBE_Util_StrCmp` @0x5d3f10: `0x5d3f89 cmp al,cl` (al = eax-side byte) + `0x5d3faa sbb eax,eax / or al,1` → returns 1 iff the **eax** operand is greater. Call site 0x51b3bc: `mov edx,esp` (key(j) buffer) / `lea eax,[esp+0x80]` (key(i) buffer); 0x51b3ca `cmp eax,1` gates the swap. Golden pins updated (old descending order PROVEN wrong): `tests/unit/world_trade_player_test.cpp` (WorldMarketStall.SortedItemList → home 5, Mark 7, Silber 6, none 0), `tests/integration/gui_trade_item_panel_itest.cpp` (ReorderUsesRealWorldSort → currency 3 "Acur" first). |
| `mission_recon3_evaluate.{h,cpp}` cases 44/46 | 0x5398c4 | Dispatcher passed `person` where the binary passes the **objective record** in EDX | Case 44 @0x539bb9 `mov edx,esi` (esi = objective since 0x5398cc); leaf 0x53963c `0x539642 mov ecx,edx` uses it as the AccumulateTimer record (eax @0x5396cb/0x5396a4), ebx=row. Case 46 @0x539bd9 `mov edx,esi`; leaf 0x539728 `0x53972c mov ecx,edx` → AccumulateTimer eax on both branches (0x539753/0x539769). Hook signatures retyped `(ObjectiveRecord*, const ReqTableRow*)`; all hooks were still inert (nullptr) so no downstream churn. |
| `money_format.cpp` `MoneyFormatWithSeparators` | 0x58f798 | Magnitude modeled as unsigned widen (`abs((long long))`); binary computes abs32 (`0x58f7a8 cdq; xor eax,edx; sub eax,edx` — INT_MIN wraps to INT_MIN) and reloads it with **signed** `fild` @0x58f7dd | For INT_MIN the rounded value is negative → `< 1000` "%i" path → `-%i%c` doubles the sign. Golden pins updated in `tests/unit/world_money_format_test.cpp` (ExtremeMagnitudes): `-2.147.483.648<G>` → `--2147483647<G>`; rate-1000 case `-2.147.484<G>` → `--2147483<G>`. Zero-case "0%c" confirmed via bytes @0x6269A4. |
| `office_assign.cpp` `OfficeApplyForCandidacy` / `OfficeAddEntryForCharacter` | 0x47e1b8 / 0x47dfec | (a) LABEL_20 single path used the FIRST-scan slot index; binary reuses the **advanced** cursor (`0x47e226 if (++v4 < 30)` + rescan → second-slot index or 30) — for a two-holder office with only one 0..29 slot the original checks record 30. (b) `GetHolderEntryByCity` @0x47dfec bounds its city scan at **30** records (`v8 += 6; v8 >= 180 break`), reimpl scanned 37 | Both loops now match the binary bounds/cursor exactly. |

## VERIFIED-1:1 (no change)

### locations / market
- 0x516b78 `TavernComputeCardGame` — gate (`rand >= willingness || hour <= 0xB || wealth < 10000`), decline path skips the write-back when hour <= 11; floats @0x621C5C: 0x3f28f5c3 (0.66f), 0xbca3d70a (−0.02f), 0xbdcccccd (−0.1f) byte-confirmed.
- 0x524074 / 0x5242d4 / 0x5258bc / 0x524740 thief burglary dialog + start guard, kidnap chain (types 60/61, hostage +433), spy selector (`589*type` byte == 19 → 589840 else 134038452).
- 0x5259f8 `ThiefComputeRansom` — cuts 0.02/0.04/0.06/default 0.1 on +433; two independent RNG draws; `(rand*0.5 + 0.75) * 1600000` doubles @0x622848 byte-confirmed; `(int)` truncation matches ConvertX.
- 0x4bad28 `TableFindEntrySlotById` — stride 67 dwords, limit 4288, clear-on-hit, returns byte offset (`result*4`).
- 0x58f6b8 `MarketLookupCachedPrice` — 7952 block / 128 entry / 7936 bound; key `dword@+2 >> 16` signed; fallback ComputeMarketPrice(goodId, 100).
- 0x58f3d0 `BuildingComputeMarketPrice` — cached path float rounds, `v22` unsigned dword numerator, need loop weights, cat-23 early return, recursive components (0xFFFF skip; divide by RAW divisor word), cache write `(int)(v28*2.2*0.03125)`. All 9 FP consts @0x626948..0x626978 byte-confirmed (0.01f/1.5/28/32/1÷12/1÷60/0.5/2.2f/0.03125).
- 0x519918 `MarketStallRouteContact` — all 8 status strings confirmed from `.rdata` (`ob_MARKTSTAND_*`, `contact_ob_SCHWARZES_BRETT` @0x519a5a, `ob_SCHWARZES_BRETT`, `ob_TRIBUENE`); stall ids 146..151.
- 0x49d2e8 `MarketStallUpsertTradeEntry` — field copy, u8 stock accumulate with `<= 0x64` clamp, status 2→1.
- 0x51b26c `TradeSortableCount` — back-scan `!slot[3] || slot[0]==-1`, count = idx+1.

### mission
- 0x53872c / 0x538668 / 0x53846c / 0x539054 (`SlotRegister`, `PickAndRegisterRandom`, `FindBySource`, `TrackCrimeProgress` type gate 11/19/23/28/40 + unconditional return 1 after gate).
- 0x538638 / 0x53b148 / 0x53b25c (`SlotSetSingle`, save/load: mode + 128×{1,4,14,4,4,1} stride 36) — re-verified; GameTime_Set @0x5831f0 write pattern covers all 14 deadline bytes.
- 0x53b0dc dispatcher; 0x53aea8 owned-slot scan + descriptor seed (`a2[1]+1`); 0x538db4 history-reward selection seed and option→mode map (opt1..5 → 0..4, opt0/6 → −1); 0x53ac34 completion switch (1→fail,2→info,3→load); 0x53adc8 result code `(dword_63CC30!=0)+1`, reload iff 2; 0x539e48 `FinishByOwner` scan.
- 0x539cbc `MissionFindGuildMemberState` — kinds 6/7, linked word +39, −1 on break.
- 0x5398c4 `MissionReqEvaluate` — 49-case switch polarity per case re-checked against full disasm; special gate 0x539928 `test dh,dh/jl + cmp dh,5/jle` is SIGNED [0,5] (reimpl right, Hex-Rays `(u8)<6` wrong); case 47 `(double)thr * 0.01f <= avg` (flt_623D68 = 0x3c23d70a confirmed); cases 38/39 take eax=person (0x539b89/0x539b79 set only edx).
- mission_requirement.cpp: 0x539c44 AccumulateTimer (fresh-stamp + DiffMinutes ≥, clear on unmet); 0x539160 StatThreshold (5 bytes @+128, ×flt_623D50, count ≥ row+16); 0x539380 OwnPersonRatio (768 records, kind<10, `dword@+9 >> 24` both sides, ×100.0); 0x539d14/0x539da0 count cores (sum in ecx: owner-word match / kind 6-or-7 via 536-stride; count incl. Begin; `fild` signed; float bits returned); 0x5393ec/0x53945c/0x539534/0x53963c/0x5396dc combo checkers — cumulative ecx accumulation re-traced instruction-by-instruction (adds at 0x53957c/0x539594/0x5395c1/0x5395e4/0x5395fd/0x53961e = Σ sum over all six states); MemberStats met = `3 >= row+0x10` via the inc-ebx success counter. FP consts @0x623D50..0x623D68 byte-confirmed.
- mission_requirement_event_recon.cpp: all nine checkers re-checked against decompile (BloodLevel 0x539138, ObjectCount 0x5391d8, BuildingEquip 0x539230, SkillAbove 0x5392f0, GuildMemberCount 0x5394d4, ZeroValue 0x539708, TimeElapsed 0x539728, MinThresholds 0x539778 [snap order 3,1,2; 0.1 / 0x3E99999A confirmed], NoActiveCombat 0x5397bc [esi = objective at the dispatcher call → reimpl arg correct]).

### money / mood
- 0x58f798 grouping core: `trunc(len + (len-1)*0.333333343f)` (flt_6269C4 = 0x3eaaaaab confirmed), back-to-front copy with `'.'` every `(v12+1)%3`, dbl_6269BC = 0.5.
- 0x467a50 ComputeMoodLevel (0.25/2.0/6.0 @0x61a224 confirmed); 0x594afc ClampMoodDelta (two sequential ifs, signed >>24); 0x4664d8 mood-color tiers (3×5 threshold table @0x46640C byte-diffed: match; row select `(v0==1)?0:((v0!=2)+1)`; draw order d0, RandomModulo(3|2), d1); 0x4fbf70 ParseSetReligion (names @0x4f8d3c "KATHOLISCH\0…EVANGELISCH\0" stride 16 confirmed; ranges [1,80]/[1,8]; cost ×flt_6207C0 = 0x3c23d70a confirmed).

### office
- office.cpp: **kOfficeDefTable[446] @0x62EC8E byte-diffed: 0 diffs. kPromotionCostBits[57] @0x62EBCC byte-diffed: 48-float matrix + overflow tail (0,0,0,0x40A00000,0x05010101,0x00000001,0x40A00000,0x07020102,0) exact.** 0x47f008 GetDefinition (+2 skew, fallback path), 0x47ef94 GetCategoryByRank, 0x47efb4/0x47ef28 entry scans (888/37), 0x47e0f8 CanPromoteRank (bookCat gates + flat stride-7 cost read to index 56), 0x47f6a4 IsNextRankInCategory (reqCode bytes + %3 residue triple), 0x47fb30 GetRankRequirements (all band constants), 0x47f858 CollectSuccessorCandidates (cap 6, filter loop).
- office_assign.cpp: 0x47e4e0 AssignToCandidate (incl. the original's quirk of re-testing slot A's state in the slot-B triple — `byte_B59858[v10*4]` both times), 0x47e750/0x47e6e4/0x47e72c add-entry trio, 0x47e870 TransferHoldership (occupant demote, >0x1B high-office path over records 30..34, vacate-first-match, bookCat `jb` rank write, partner rank decrement, install + state-3 rank reset; player-gate on the notify is a documented roster BOUNDARY), 0x47ec64 SwapHolders, 0x47ed68/0x47ee44 release/clear (vacant state = `(hour > 11) + 3` is caller-supplied per the documented boundary; Clear returns 888).
- office_forms.cpp: 0x49dc18/0x49dbe8 vote panel (headers 3861-3863 color 67; marker x {32,62,47}, y 10·n+80, icon 0x48A=1162; AddVoteMarker x = 68−10·count, y = 0x8C=140 — all read from raw disasm), 0x4a0610 election (seat texts 3634-3639, fail 1..5→3642..3646, 6→3649, 7→3648; success = all BuildSpeechPacket), 0x4a003c/0x4a01a4 successor dialogs (3848; 3871 iff collected ≥ 2 else 3870; ≤4 buttons pad −1; slider ((w−300)/2)−8), 0x4a3dc8 torture form (case texts 4321/4336/4352+4353/4360/4430/4439+4440/4407; cost table @0x49D7C4 = {8,10,12,15,18,21,24} byte-confirmed; case-5 default = tier+1 since button[1] id ≠ −1; case-2 RandInt(3) preselect remains the documented injected-order boundary).
- office_law3.cpp: 0x47de78 InitTable (37-record seed mapped from all 37 `byte_B59xxx` stores: match; return 7709), 0x57c184 FindRoleTemplate — **both 76-slot role id tables (stride 40 @0x63E338 / @0x63EF18) diffed dword-for-dword: match**; signed `cmp bl,4Ch/jge` gate reproduced; 0x562c9c AwaitPromoteResult (ECX-residue byte documented BOUNDARY), 0x4c2e74/0x4c2f58/0x4c3020/0x4c3278 Gesetz description selectors (ids 4158/4163/4168 (+flag), 4213/4223, 4228..4273 step 5; class gates <8/<16/≥26), 0x55a9bc/0x558bbc/0x558b30 dialog leaves.
- office_prosperity.cpp: 0x57b718 — average `(wealth + Σroom)/(nonzero+1)`; f32/f64 mixing exact (f32 operands, DOUBLE quotient slot, clamp, f32 re-round before blend); blend 0.5 / decay 0.95 @0x6258B4/0x6258BC byte-confirmed; +476 committed with ORIGINAL wealth.

## Notes (documented boundaries, no change)
- 0x53aea8: descriptor-miss path in the binary reads `((char*)0)[1]` (UB, unreachable — descriptor always found); reimpl maps it to −1.
- 0x538ec6: `completed == 0` seed reads a dead register in the binary (radio default); reimpl uses 0.
- money_format keeps the `rate==0 → 1` guard for an input the currency table never produces (binary would divide by zero).

## Tests
Targets built individually (`cmake --build build -j --target …`) and run with
`GUILD_GAME_DIR=$PWD/europe_guild_1400_original`, all green:

world_trade_player_test 111 · gui_trade_item_panel_{test 30, itest 10, e2e 9} ·
world_trade_player_e2e_test 26 · mission_recon3_evaluate_test 43 ·
wire_event_office_test 23 · mission_requirement_event_recon_test 31 ·
mission_requirement_{test 33, itest 13, e2e 14} · world_mission_save_{test 954, itest 23, e2e 18} ·
history_mission_{test 42, itest 20, e2e 19} · world_money_format_test 32 ·
player_finance_{itest 16, e2e 17} · hud_render_test 23 · world_stammbaum_e2e_test 19 ·
gui_dialogs4_itest 13 · world_office_flow_{test 129, e2e 30} · office_cluster_harden_test 66 ·
dialog_council_{itest 20, e2e 21} · gui_office_forms_{test 159, e2e 57} ·
mood_religion_{test 68, itest 139, e2e 23} · office_law3_{test 529, itest 12, e2e 22} ·
world_location_{test 111, e2e 29} · world_market_price_{test 11, model_test 19, e2e 4} ·
lookup_table_save_recon_test 15 · world_amt_e2e_test 16 · world_amt2_{test 286, e2e 36} ·
world_events_{test 111, e2e 27}.

Golden pins changed (with binary proof): world_trade_player SortedItemList order,
gui_trade_item_panel_itest reorder order, world_money_format ExtremeMagnitudes INT_MIN pins.
