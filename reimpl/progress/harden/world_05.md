# world_05 — 1:1 hardening report (chunk: src/world, trade/trial/tutorial/wire slice)

Scope: the 22 files in `/tmp/guild_harden/chunks/world_05.chunk`. Every function
with a `// gilde.exe 0x…` provenance was decompiled via the IDA MCP and diffed
line-for-line; every `@0x…` const table was byte-diffed via `get_bytes`.
Evidence for every FIXED item is the cited binary address.

## Verdict summary

| File | Status |
|---|---|
| straftat_table.cpp | VERIFIED-1:1 (prior wave + spot-check) |
| tavern_cards_location_recon.cpp | VERIFIED-1:1 (prior wave findings) |
| tax.cpp | VERIFIED-1:1 (header prose corrected) |
| townhall_location_recon.cpp | VERIFIED-1:1 |
| trade_player.cpp | VERIFIED-1:1 |
| trade_recon_dragslots.cpp | FIXED (16-bit sum wraparound) |
| trade_recon_transport_thunks.cpp | VERIFIED-1:1 |
| trade_route.cpp | VERIFIED-1:1 |
| tradetransport.cpp | FIXED (float precision + panel-mode test) |
| treasury.cpp | VERIFIED-1:1 |
| trial.cpp | FIXED (float score accumulator) |
| trial_session.cpp | FIXED (FSM phase semantics, 5 divergences) |
| tutorial.cpp | VERIFIED-1:1 |
| tutorial_steps.cpp | FIXED (2 outro-kind table values) |
| tutorial_mission.cpp | VERIFIED-1:1 |
| tutorial_chapters345.cpp | FIXED (outro kind + anim-table tails) |
| wanted_level.cpp | VERIFIED-1:1 |
| wire_building.cpp | VERIFIED (glue; 0x496888 call shape confirmed) |
| wire_dialogtut.cpp | VERIFIED (glue; 0x4c2e74 leaf confirmed) |
| wire_economy2.cpp | VERIFIED (glue) |
| wire_election.cpp | VERIFIED (glue; 0x481bcc offsets confirmed) |
| wire_event.cpp | VERIFIED (glue) |

## Per-function detail

### straftat_table.cpp / tavern_cards_location_recon.cpp
Verified by the earlier (interrupted) wave — see the findings fragment
`scratchpad/world05_g01.md`: 0x53846c, 0x5384a0, 0x538524, 0x539054,
0x5160f0, 0x51618c, 0x5162c0, 0x516794 all VERIFIED-1:1, zero edits.
Spot-checked 0x53846c this pass (36-byte stride, 128-record bound) — matches.

### tax.cpp
- 0x57aa88 `VIBE_Tax_CollectTradeIncome` — **VERIFIED-1:1** (arithmetic core).
  flt_625884 = 0x3C23D70A (0.01f) confirmed via get_bytes; per-step float
  stores match the `fstp` chain (disasm 0x57aad4..0x57ab0c); the `fistp` after
  `VIBE_Coord_ConvertX` = truncation = `static_cast<i32>` ✓; clamp `v21<=0 -> 0`
  ✓; guard `edx>=6 -> return 0` ✓; `test ebx,ebx; jle` skips writes but still
  returns 1 ✓ (code was right; the tax.h return-code PROSE said "0 … or the
  account was non-positive" — corrected, doc-only).
- Commit-side effects behind the hook abstraction (law-record slot writes
  `byte[rec+idx]=1`, `+0x20=threshold`, `+8=amount`, `++dword_641DA4`) are
  engine-side and documented as the TaxCommitToCommandQueue seam.

### townhall_location_recon.cpp — all VERIFIED-1:1
- 0x51f70c contact loop: flags 512/1024/2048, stat overlay `byte_67225C`
  47→General / 20→Tax, dispatch order buy→agenda(VariantB)→officeInfo(VariantA).
- 0x52036c law loop: flag 4096 (3 law books, order tax/criminal/constitution →
  0x112/0x111/0x113), 0x8000 apply, citizenship gate
  `(dword_12CEAD8[134*p] & 0x4000) && byte_12CE91D[536*p]==1 && flag 0x2000`;
  dispatch order tax, criminal, constitution, citizenship, apply.
- 0x51f81c citizenship: wealth < 16000 → msg 5647 abort; confirm 1210 +
  CheckResourceAmount(16000); months = Tutorial inactive ? rand%3+1 : 5;
  buyer grant does NOT bump v48; family loop `byte_63CC1D <= v48` break at top,
  status 6/7, other-person, v20 < 768; QueueRequest16(office,buyer,16000,0);
  QueueRequestArgs25(buyer,456,0,4,0x4000); panel event 0x2E. All match.
- 0x51f2e8 building info: row filter `(Begin[90] & 2)`, buy gate
  `*(u16*)(bldg+39) != word_63CC5C` + afford + confirm → EnqueueBuyBuilding.

### trade_player.cpp — all VERIFIED-1:1
- 0x58f14c: `(double)a1/rate + dbl_6268DC(=0.5, byte-verified)`, ConvertX trunc.
- 0x58f1dc: movsx dx + idiv — C++ `/` truncation identical.
- 0x51bae8: 28-byte-stride slot scan (`a1 += 7` dwords), `*a1 != -1 &&
  clicked == *a1`, bound 16, gate dword_672228.
- 0x519b14: structure `v1 >= v19 ? Cmd15(player, bldg, v1-v19) :
  Cmd15(bldg, player, v19-v1)` matches WineCellarBuy exactly. (Note: the
  binary's compared operand v1 is `GameObject_SumChildMoney(residence+20)`;
  the reimpl's parameter name `playerCash` is the caller's slot for it.)
- 0x50c5d4: foreign-city gate `word_63CC5C != *(a1+39)`; probe order
  EINKAUF, _METALL, _SKRIPTE, _HOLZ, _STEIN with short-circuit; click →
  OpenPanelMode1_Thunk.

### trade_recon_dragslots.cpp — **FIXED**
- 0x50b350 / 0x50c140: all constants verified in disasm (stride 5 dwords ×4
  cols, threshold 2, `+1715` template base, `84*` template stride, `+78>>16`
  width, `+0x233B4>>16` row stride, pitches 28/35, slider pitch 68, wide-mode
  words 475/476, commit loops 224/112 ×14).
- **DIVERGENCE FIXED**: widget origin math. Binary does a 16-bit add of the
  form-origin word and the offset low word, then sign-extends the SUM
  (0x50b48b `mov ax, word(yOff); add ax, [edx+6]; movsx edx, ax`, 0x50b49d
  `…; add ax, [ecx+4]; cwde`; same at 0x50c282/0x50c294). The reimpl
  sign-extended only the offset and added full-width formX. Replaced
  `loword()` with `addWord16(origin, off) = (i16)((u16)origin + (u16)off)`.
  Existing golden pins unaffected (no 16-bit overflow in test vectors);
  header doc updated.
- Bounded inner column walk (OOB guard on malformed input) retained — layout
  identical on valid input, documented.

### trade_recon_transport_thunks.cpp — VERIFIED-1:1
0x54012c → dispatcher(dl=2), 0x54013c → dispatcher(dl=4) (disasm: `mov edx,2/4;
call VIBE_TradeTransport_PanelDispatcher`); 0x54011c (mode 1) noted.

### trade_route.cpp — VERIFIED-1:1
- 0x592220: rates byte-verified — flt_626A7C=0.05f, flt_626A78=0.1f,
  flt_626A74=0.15f, dbl_626A84=0.5; clamp [32000,256000] only when
  `(double)a1 > 0.0`; double multiplies + ConvertX truncations match.
- 0x53f404: route-clear walk + `InvokeHandlerSlot60(36)!=1 → 0` gate engine-side;
  +1-day GameTime_Advance(…,0,1,0); charge only when `*(cart+40)`;
  QueueRequest16(-1, owner, cost, currency). Match.
- 0x53f5c0: find kind-15 handler by `+43 == cartId`, re-stamp +82..+94,
  QueueRequestEntity29(-1, rec) — modeled emit ✓.
- 0x53f610: tolerance-1350 fast path + owner-chain walk via
  ResolveEntityById/+6 — abstraction faithful.
- RouteDecodeOpen/RoutePanelStep: dispatcher (0x54014c) bits confirmed —
  `a2 & 8` route-edit, HIBYTE 1(&1)=market / ==2→contor 475 / ==4→contor 476,
  confirm `dword_75BF38 == 1228` + ShowMessageBox, qty>0→QueueRequest17 /
  qty<0→QueueRequest20. Documented high-level FSM (explicit abstraction).

### tradetransport.cpp — **FIXED** (0x53ff3c)
- 0x592220 clone + 0x53f404: verified (same constants as trade_route).
- 0x53ff3c `ComputeCargoValue` — **two proven divergences fixed**:
  1. PRECISION: the unit price (v15 @esp+8) and the running total (v13 @esp+0)
     are 32-bit float stack slots — every product is `fstp dword`-rounded per
     iteration. Reimpl accumulated in double. Now: `float value` accumulator,
     `float unit`, per-slot `value = (float)((double)qty * unit + value)`
     (80-bit intermediate modeled with double, float store).
  2. MODE DOMAIN: the sell-at-contor test is `a3 == 2 || a3 == 4` where a3 is
     the PANEL dispatcher mode byte (1 market / 2 import / 4 export), not the
     cart TransportMode — mode 4 was unrepresentable. Signature changed to
     `int panelMode`; condition now `panelMode == 2 || panelMode == 4`.
     kMarketPriceFactor dbl_623F30 = 1.1 confirmed. Tests updated to pass raw
     panel modes (`world_amt_test.cpp`, `world_amt_e2e_test.cpp`); goldens
     unchanged (values within tolerance under float accumulation).

### treasury.cpp — VERIFIED-1:1
0x51ca40 fee commit: field order 105 (rate widget) then 109 (courier widget),
BeginDeltaPacket → 2× AppendDeltaField(4,1,…) → QueueRequestState22 — matches
ExchangeSetFees' hook writes. Deposit/Withdraw/Transfer are documented
dialog-rule cores (no address provenance).

### trial.cpp — **FIXED** (0x4a0eb8 rules core)
- Wanted-weight table @0x49D644 byte-verified: {1.4f,1.2f,1.0f,0.8f,0.6f};
  clamp `<=0→0, >=4→4` ✓.
- Fine constants byte-verified @0x61CCF4..0x61CD00: 0.2f, 0.01f, 1.1f, 0.8f ✓.
- Verdict threshold `v313 >= 2 → NICHT_SCHULDIG` ✓ (line `if (v313 >= 2)`).
- Fine commit: THREE QueueRequest16 (v311/v329/v312 seats) with amount
  `wealth/3` computed once ✓.
- **DIVERGENCE FIXED**: the evidence-score accumulator v318 is a 32-bit FLOAT
  stack slot ([esp+B4Ch]); each `v318 = (double)v304 * v319 + v318` is fstp'd
  to float per iteration. Reimpl accumulated in double with one final narrow.
  Now: `float score` with `(double)penalty * (double)weight + score` cast to
  float each iteration.
- TrialApplyGuiltyFine (`v280 = v318 - v77*v340*flt_61CCF8`, v77 double, v340
  float) and TrialApplyTortureFine (`v280 * flt_61CCFC/61CD00`) — VERIFIED.

### trial_session.cpp — **FIXED** (FSM semantics vs 0x4a0eb8)
Torture-instrument .esc table @0x49D658 byte-verified (7 × 16-byte entries) ✓;
scene scale 1000.0f (447A0000h) ✓. Five proven structural divergences fixed:
1. PROZESS_3 branches on the DEFENDANT's panel value == 1 (the plea):
   `mov ecx, dword_11AB094[edx]` @0x4a1adb, `cmp ecx,1` @0x4a1b24 — not the
   jury tally. Phase renamed kJuryVerdict → kPlea; driven by new
   `TrialSetup.defendantPleadsGuilty`.
2. The favorability fine-adjust runs INSIDE the guilty-plea branch
   (`fmul flt_61CCF4` @0x4a1b3e), i.e. BEFORE torture — it was at kSentence.
3. Torture is entered from the not-guilty branch when the torturer person
   (ctx+72, v327) resolves AND evidence count > 2 (`if (v327 && v314 > 2)
   v279[0] = 1`) — it was "convicted && st.torture". New setup fields
   `torturerPresent`; gate `!plea && torturerPresent && evidenceCount > 2`.
4. When torture proceeds, the torture-cost fine (v292 =
   AiMethod_ComputeWealthScoreB, /3 to each of the three seats via the
   QueueRequest16 triple) is committed immediately, BEFORE the instrument
   scene. Added `TrialSetup.wealthScoreB` + `TrialSession.tortureFine`.
5. The jury tally (v313 = sum of the three panel rows, present seats only;
   `>= 2` → acquit) happens at PROZESS_6_ABSTIMMUNG → moved TrialTallyVerdict
   to kVoteAnnounce; the PROZESS_7 sentence fine uses v291 =
   AiMethod_ComputeWealthScoreA (`v246 = v291 / 3`), NOT the evidence score —
   added `TrialSetup.wealthScoreA`; kSentence no longer applies the favor
   adjust.
Also: instrument index clamp now mirrors the binary's v330 clamp
(`(panel & 0xF)`, `<=0→0, >=6→6`). Header phase map rewritten with the
addresses. Tests updated: `world_court_session_test.cpp`,
`world_court_session_e2e_test.cpp`, `world_trial_test.cpp`
(SessionTortureInstrumentClamped now satisfies the evidence>2 gate).

### tutorial.cpp — VERIFIED-1:1
- 0x597b94 IsInactive, 0x597b5c SetActiveChapter (-4 gate, chapter store) ✓.
- 0x5978d8 AdvanceStepForms classification: no-chapter −4, stepCount 0 → 0,
  end-of-chapter destroy → 0, phase byte (@struct+64 vs step+4, >>24 signed)
  equal → reuse; changed → formPos switch 0..3 (left/right/top/bottom), >=4 →
  −4. All match. NOTE (not code-fixable sanely): when the end-of-chapter path
  finds NO open form (idx17 == −1) the binary falls through into the
  phase-compare reading step[stepIndex] past the array — an OOB-dependent
  edge; the reimpl classifies kEndChapter. Documented here.
- 0x597e30 FreeStepChain: recursion via +112, frees +88/+100 ✓ (chain-length
  traversal abstraction).

### tutorial_steps.cpp — **FIXED** (full field-by-field table diff)
- 0x597e80 InitMainIntro / 0x59b318 InitMainOutro: every field verified
  (kind 0/7, "RTNI"/"RTUO", byte8 4/5, text 7356/7359, coord 142/0, form 11,
  mask 0/−9) ✓.
- 0x597f5c InitChapter1Steps: all 11 nodes verified field-by-field (names
  "1rtn","As1c".."Js1c","1rtu"; ids 7362..7401; arrows 22/6/8/1/3/12/19/20/13;
  masks 0/1/3/7/7/23/2071/6167/6199; extra19 7/9; aux2 8520/71/355; forms
  11/9/10; cb1 = HandlePickupDropTransition (E), HandleEvent13SetState11 (J);
  cb2 = loc_595E70 everywhere incl. intro+outro).
  **DIVERGENCE FIXED**: the outro node kind is **1** (`*(_DWORD *)v73 = 1` at
  the 0x597f5c tail) — the chapter number, not 7. Table 7 → 1.
- 0x598874 InitChapter2Steps: all 9 nodes verified (ids 7403..7432, coords,
  cb0/cb1/cb2 pointers incl. BeginQueryThenSetField270 / Event25Code270 /
  MatchObjectDropTarget etc., masks 6199/22583, word54 0/3584/7680, F-node
  aux2 = 1065). **DIVERGENCE FIXED**: outro kind is **2**
  (`*(_DWORD *)v65 = 2`), was 7. Test pin updated:
  `world_history_full_test.cpp` chapter-1 outro kind 7 → 1 (documented).

### tutorial_chapters345.cpp — **FIXED**
- 0x598fc4 InitChapter3Steps: all 10 nodes verified, including the script
  sub-tables (3A: (710,1,7438)(710,0,7439)(710,2,7440); 3B ×2; 3D ×3; 3E ×3)
  and the full resolution-keyed anim float blocks (3B 1 frame, 3E 2 frames,
  3G 5 frames, 3I 1 frame — every coordinate decoded from the builder's
  dword stores and diffed).
  **DIVERGENCES FIXED**:
  1. outro kind is **3** (`*(_DWORD *)v87 = 3`), was 7. Test pin updated
     (`world_tutorial_chapters345_test.cpp`, old 7 → 3, documented).
  2. `kAnim3G_800` frame-1 and frame-2 tails: the 3-float tail is written
     ONCE, outside the resolution branches (anim+32..40 = 35,142,71 and
     +76..84 = 35,142,71) — the _800 rows wrongly had 35,170,106.
     (3I's tail 35,170,106 and 5C's 35,177,106 are correct — re-verified.)
- 0x59a608 InitChapter4Steps: VERIFIED-1:1 (intro kind 3 as documented, inner
  nodes kind 1, outro kind 4, masks 23095/23415/24439/6199).
- 0x59aa20 InitChapter5Steps: VERIFIED-1:1 (all 7 nodes incl. kAnim5C both
  frames ×3 resolutions, word54 16248/−16520, final node kind 6).
- 0x4db8ac AdvanceChapterOrFree: VERIFIED (SetActiveChapter; nonzero →
  He_FreeHandlerEntry; armed → `*(node+112) = 0`).

### tutorial_mission.cpp — all VERIFIED-1:1
0x597b80 (idx6=0, [7]=−1), 0x597da8 (reset tail subset), 0x5971ac (−4/−1/0
guards), 0x597270, 0x59729c, 0x59744c (0x87 screen gate, clicked==off[7],
byte_67225C==28), 0x597700 (w/2−200, 400-wide), 0x5977a8 (elapsed select +
clamp, `(double)(dur−v9)/(double)dur * flt_626DE4(=400.0 byte-verified)`,
trunc; active gate = chapter && phaseByte==10), 0x59b450 (phase 2→3→4→2
boundaries at frame float +32/+36/+40, fall-through on same tick, bezier
fraction `(double)(now−tick)/travelDur`), 0x596fa4 (guard chain), 0x539fd8
(`*(v42+4)+2`, `"_AUFTRAEGE_ERFOLG_HS_%.2d"`, deadline +250, skip
`75BF38 != −1 && child == 62D22C`), 0x538b28 (cancel −1, opts 0..5, 6th opt
gated by byte_63CC1D>1), 0x538db4 (back/cancel −1, opts 0..4, disable count).

### wanted_level.cpp — VERIFIED-1:1
0x4c2ba0: 256 slots × 169 stride, alive byte, owner word +39, type
`dword_13CE294 + 589*type == 7`, SumWorkstationByCategory(b,10,1), ==75 →
0.75 short-circuit, max, tail `fild; fmul flt_61E588(=0x3C23D70A verified);
fstp float` → float-narrowed return. All match.

### wire_*.cpp — VERIFIED (integration glue, no algorithm bodies)
- wire_building.cpp: 0x496888 → TransferEstateOwnership(*(a1+16), *(a1+20))
  call shape confirmed in the decompile; bindings only.
- wire_dialogtut.cpp: 0x4c2e74 leaf confirmed (`v16 = rec ? *rec : -2`).
  NOTE: the binary keeps v16 as u16 (−2 → 65534 when promoted to varargs);
  the hook contract/normalization lives in office_law3 (outside this chunk).
- wire_election.cpp: 0x481bcc field offsets confirmed (+361 rank u8 in
  [0x1E,0x21], +459 & 4, +404 int >= 3); extraction in WeGuildEligibility
  matches. Other bindings glue-only.
- wire_economy2.cpp / wire_event.cpp: hook-table bindings onto reconstructed
  leaves owned by other modules; internally consistent, nothing to byte-diff.

## Tests (built per-target, all green)
world_trial_test 163 · world_court_session_test 93 · world_court_session_e2e_test 45 ·
world_amt_test 99 · world_amt_e2e_test 16 · world_economy_test 156 ·
world_economy_e2e_test 17 · trade_recon_dragslots_test 147 ·
world_tutorial_chapters345_test 132 · world_tutorial_chapters345_e2e_test 91 ·
world_tutorial_chapters345_itest 20 · world_history_full_test 148 ·
world_history_full_e2e_test 35 · townhall_location_recon_test 68 ·
world_trade_player_test 111 · world_trade_player_e2e_test 26 ·
world_trade_route_test 126 · world_trade_route_e2e_test 27 ·
bank_treasury_e2e_test 18 · tutorial_mission_test 81 · tutorial_mission_e2e_test 33 ·
tutorial_mission_itest 39 · world_law_test 1324 · wire_building_test 10 ·
wire_election_test 87 · wire_economy2_test 10 · wire_dialogtut_test 10 ·
wire_event_test 40 · straftat_table_test 123 · straftat_table_e2e_test 55 ·
straftat_table_itest 119 · tavern_cards_location_recon_test 99 ·
player_finance_itest 16 · gui_contact_loops_test 106 · gui_contact_loops_e2e_test 3 ·
interaction2_test 140 · buy_dialog_test 53 — **0 failures.**

## Updated golden pins (old → new, with binary evidence)
- chapter-1 outro node kind: 7 → 1 (`*(_DWORD *)v73 = 1`, 0x597f5c tail).
- chapter-3 outro node kind: 7 → 3 (`*(_DWORD *)v87 = 3`, 0x598fc4 tail).
- (chapter-2 outro kind 7 → 2 had no direct test pin; table fixed.)
- kAnim3G_800 frame1/2 tails 35,170,106 → 35,142,71 (common writes at
  anim+32..40 / +76..84 in 0x598fc4, outside the resolution branches).
- Trial-session tests rewritten to the binary-verified phase semantics
  (plea/torture gate/tally placement/wealth-score fines) — see trial_session
  section for each address.
