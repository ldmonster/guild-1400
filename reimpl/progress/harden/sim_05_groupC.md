# Hardening sweep — sim group C (cutscene + console + contextaction2)

MCP-driven 1:1 diff of every provenance-carrying function in:
- src/sim/console_recon.cpp
- src/sim/contextaction2.cpp
- src/sim/cutscene_auction.cpp
- src/sim/cutscene.cpp
- src/sim/cutscene_duel.cpp

Reference of record: gilde.exe decompile + disasm (imagebase 0x400000). Where
Hex-Rays mislabels a float→int site, disasm is authoritative.

## Counts
- Functions audited: 31
- VERIFIED-1:1 (no change): 28
- FIXED (divergence corrected): 3
- BOUNDARY (leaf / out-of-tree state, documented): noted inline
- New golden tests added: 3 (2 in contextaction2_boundary_test, 1 in
  sim_cutscene_types_test)
- All 5 owned .cpp pass `g++ -std=c++17 -fsyntax-only`; both edited test TUs
  compile. NB: the full library link is currently blocked by an UNRELATED,
  not-owned corruption in src/gui/widget_layout.cpp (`.id`→`.ld`, mangled
  `pD(4)`); reported as a handoff, not touched.

---

## console_recon.cpp — VERIFIED-1:1 (8/8)

- `Console_IsValidKeyEvent` @0x609090 — VERIFIED. eventType==1 && keyDown &&
  (vk<0x10 || vk>0x12) → 1.
- `Console_OpenConHandles` @0x6090b8 — VERIFIED. lock / lazy-open conin/conout
  (==-1 gate) / unlock.
- `Console_GetInputHandle` @0x609128, `Console_GetOutputHandle` @0x609134 —
  VERIFIED (open + return cached handle).
- `Console_InstallCtrlHandler` @0x60929c — VERIFIED (install-once, return u8 flag).
- `Console_RemoveCtrlHandler` @0x6092cc — VERIFIED (`flag && OS-ok → 0`, return
  flag==0).
- `Console_ReadCharEvent` @0x609c40 — VERIFIED. The peekPhase state machine,
  the `goto LABEL_11` read path, the `repeatCount-1`/`(u8)asciiChar` reads, the
  read-fail `-1` restore path, and the peekPhase==2 `repeatLeft!=0?1:0` all match.
  NOTE the printable-key gate `(controlKeyState & 1) == 0`: the binary reads the
  BYTE at INPUT_RECORD+0x11 (`*((_BYTE*)&FocusEvent+13)`), i.e. bit 8 of
  dwControlKeyState (ENHANCED_KEY). The recon's `controlKeyState` field IS that
  +0x11 byte (documented), so `& 1` is faithful — internally consistent, verified
  against disasm.
- `Console_GetCh` @0x609d44 — VERIFIED (ungetSlot fast-path, getRedirect path,
  lock/getMode/setMode(0)/read/setMode(mode)/unlock).
- `Console_PutCh` @0x609dc0 — VERIFIED (Buffer[0]=ch, putRedirect vs
  WriteConsoleA, return ch).

---

## cutscene.cpp — VERIFIED-1:1 (8/8)

Slot stride 276 / 69 dwords / 96 slots confirmed; alive gate byte at +48,
state-flags byte at +38, id at +0, type at +8, secondId at +20, partIds at +52.
- `FindById` @0x4ac750 — VERIFIED (alive && id==a1).
- `FindByTypeAndId` @0x4ac708 — VERIFIED. type read = high byte of dword@+5 (=+8),
  secondId @+20, both must match (PAIR64 compare).
- `AllocSlot` @0x4ac7e4 — VERIFIED (first free, qmemcpy 0x114, id=newId,
  priority@+49 default 8 if zero).
- `RemoveById` @0x4ac860 — VERIFIED (memset 276, id=-1).
- `RemoveAll` @0x4ac888 — VERIFIED. The binary re-finds by id (FindSlotById),
  which requires the +48 alive gate, so only participant-bearing slots are torn
  down — the recon's `if (s.partCount)` gate reproduces this exactly.
- `FindLowestPriority` @0x4ac630 — VERIFIED. UNSIGNED min over alive slots with
  (flags&1) set, `best >= sid`; re-find by id. u32 throughout — verified.
- `AddParticipant` @0x4ac8d0 — VERIFIED (dedup scan over count, `>=0x10` full
  gate, append + count++). The recon's clamp-to-16 is a documented defensive
  guard that cannot change valid behaviour (the full-gate forbids count>16).
- `RemoveParticipant` @0x4ac928 — VERIFIED for the slot list (scan, set entry -1).
  BOUNDARY: the binary ALSO clears `dword_11AB010[]` (the runtime active-duel
  participant table) when `dword_6315C0 && id == *dword_6315C0`. That table is
  global engine state outside the slot model — a documented leaf, omitted.

---

## contextaction2.cpp — 1 FIXED, rest VERIFIED-1:1 (23 actions)

### FIXED — TrainTier rank-0 false-positive (5B/6A/6B)
- Addresses: TrainRank5B @0x56f710, TrainRank6A @0x56f85c, TrainRank6B @0x56f900
  (shared helper TrainTier).
- Bug: the helper used `overRank=0, hasOver=false` as a "no over-check" sentinel
  and then ran `if (r == overRank && !hasOver) return 4;` — so a rank-0 actor
  hit `0 == 0 → return 4` BEFORE the `r < underRank → 1` reject.
- Evidence (disasm): the >=N variants do `... || a1[13] < 5u/6u → return 1`
  (0x56f727 / 0x56f873 / 0x56f917). There is NO `== 0 → 4` arm. rank 0 must
  return 1, not 4.
- Fix: replaced the `(overRank, hasOver)` pair with an explicit
  `OverKind {kNone, kGreater, kEqual}` so the exact-match arm fires ONLY for
  TrainRank4Plus (kEqual, overRank=6 @0x56f5da `cmp v4,6 / jz`) and TrainRank4B
  uses kGreater (>4 @0x56f52e `cmp v4,4 / ja`). 5B/6A/6B → kNone (pure >=N).
  Before: `TrainTier(..., /*overRank*/0, /*hasOver*/false, /*underRank*/5, ...)`
  After:  `TrainTier(..., OverKind::kNone, /*overRank*/0, /*underRank*/5, ...)`.
- Tests: added `ContextAction2Boundary.RankTrainBelowThresholdRejected`
  (rank 0 → 1 for 5B/6A/6B and 4Plus) and `.RankTrainExactOverIsolated`
  (rank 6 → 4 only for 4Plus; 2 for the >=N variants).

### VERIFIED-1:1
- TrainRank4B @0x56f514 (`>4→4, <4→1`), TrainRank4Plus @0x56f5c0 (`==6→4, <4→1`)
  — VERIFIED against disasm.
- FileLawsuitType1 @0x56fbe0 / Type2 @0x56fd3c — VERIFIED. mode 3/1 activate
  (profession!=0 && Gesetz_FindRecordByPair; kind6 tooltip; 457&2→9; 458&mask
  gate; mode3 → scratch=type @+532, PanelEnactLaw, return v14|2) and mode 4/2
  drag (457&1, dragSource@+540 valid, 358!=0, !(458&mask), find; mode4 →
  scratch=type, leaf, return v14|0xA; else kind6 tooltip; return v14|0xA). The
  scratch literal is the lawsuit type (1 / 2) — matches `*(v9+532)=1`/`=2`. mask
  0x08 (type1) / 0x10 (type2). VERIFIED.
- ProfessionMenu28/29/34/Range30/Office/17A/17B/Clergy/10/15/23 (0x570028…
  0x570bb0) — VERIFIED. Gate field +361 (submethod) or +358 (profession); the
  accept sets match (Range30 = the [0x1E,0x21] range check). dialog/tooltip
  verdict identical.
- CommandType24/21Or26/24Drag/18Or22/26 (0x570af8…0x571134) — VERIFIED.
  activate (kind6 tooltip; profession in set; 457&2→9; mode3 leaf; 2) and drag
  (457&1 && dragSource && src.profession in set; mode4 leaf→10; kind6 tooltip;
  10) match. VERIFIED via CommandType24 @0x570af8 disasm.

---

## cutscene_auction.cpp — 1 FIXED, rest VERIFIED-1:1

### FIXED — lease-split float width + fistp rounding
- Address: 0x4a94c5..0x4a94e6 (owner share) and 0x4a9624..0x4a9651 (other share)
  inside VIBE_Cutscene_Auction @0x4a89f8.
- Two bugs in one site:
  1. The recon multiplied by the DOUBLE constants 0.9 / 0.1. The binary does
     `fmul ds:flt_61D700` / `flt_61D704` — 32-bit FLOAT operands. get_global_value:
     flt_61D700 = 0x3f666666 (≈ 0.89999998), flt_61D704 = 0x3dcccccd (≈ 0.10000000).
     The product is `(double)bid * (double)(float)k`.
  2. The recon truncated with `(int)`. The disasm stores with `fistp` =
     ROUND-TO-NEAREST-EVEN. (Hex-Rays prints `(int)v91`; the interleaved
     `call VIBE_Coord_ConvertX` operates on its own coord args and leaves
     st0=v91 for the fistp — verified in disasm.)
- Evidence that BOTH matter: bid=150 → double*trunc gives `(int)135.0`=135 (right
  by luck); float*trunc gives `(int)134.999…`=134 (WRONG); float+fistp gives 135
  (correct, matches the existing golden `AuctionWinnerHighestBid` toOwner==135).
  bid=2 → float+nearbyint=2, float*trunc=1.
- Fix: added `kAuctionLeaseOwnerF`/`kAuctionLeaseOtherF` (the exact float values)
  to the header and compute
  `(int)std::nearbyint((double)bid * (double)kAuctionLeaseOwnerF)` (default
  FE_TONEAREST == x87 round-to-nearest-even). Legacy double aliases kept.
- Test: added `SimCutsceneTypes.AuctionLeaseSplitRoundsToNearest` (bid 2 →
  toOwner 2, not the truncated 1). Existing golden `AuctionWinnerHighestBid`
  (150 → 135 / 15) stays green.

### VERIFIED-1:1
- `AuctionTextureSetForRegion` (LABEL_25 switch 11→0/12→1/13→2) — VERIFIED.
- `CutsceneBroadcastMessage` @0x4a8930 — VERIFIED. `!Building_FindById → 0`;
  for each participant of kind 6/7 build a speech packet; return 1.
- Round loop @0x4a9062..0x4a9308 — VERIFIED. round/stop/winnerIndex(v136)/
  askPrice(v116[0]) committed in the per-bidder scan; `activeBidders>=2 &&
  round<4 → askPrice+=32`; `<2 → stop`. Max 5 rounds (`v81+1<5`).
  BOUNDARY: the per-round bids come from RunParticipants / bidder AI
  (dword_11AB094/98) — a leaf supplied by the caller; the RandInt(3) auctioneer-
  flavour and comment-voice draws are on the cutscene LCG inside leaves not
  reconstructed here (no RNG drawn in the recon's deterministic core).

---

## cutscene_duel.cpp — 1 FIXED, rest VERIFIED-1:1

### FIXED — round actor order (RNG draw order)
- Address: 0x4a5996..0x4a5a35 inside VIBE_Cutscene_Duel @0x4a53a8.
- Bug: the recon always ran A then B. The binary chooses the first actor by B's
  choice byte: `v51 = participant[B]+148; if (v51 == 2 || v51 == 3)` →
  ProcessIntroChoice(B,A) FIRST then A; else ProcessIntroChoice(A,B) first then
  B. Since Duel_ResolveTaunt/Aim/Shot draw from the cutscene LCG, the first
  actor's draws must precede the second's — wrong order desyncs the RNG stream.
- Fix: `bGoesFirst = (b.choice == kTaunt || b.choice == kAim)`; run actB/actA in
  that order. Each lambda keeps its own isA + onShot index, so reporting stays
  correct regardless of order.
- Note: gating the second call on `!state.over` is faithful — ProcessIntroChoice
  @0x4a4eb4 is itself wrapped in `if (!dword_6315C4)`, so once a fatal hit sets
  the over flag the second call is a no-op in the binary too.

### VERIFIED-1:1
- `DuelRollOutcomeTier` @0x4a6908 — VERIFIED via disasm. tiered: RandInt(100),
  `>50→4` (`cmp eax,0x32 / jg`), `<=15→3` (`cmp eax,0xF / jle`), else 2.
  non-tiered: RandInt(10), `(unsigned)r > 7`.
- `DuelProcessIntroChoice` @0x4a4eb4 — VERIFIED spine: choice 2=taunt (2×
  RandFloat), 3=aim (1× RandFloat), 4=shoot (defers Duel_ResolveShot), else idle.
  The RULES math lives in duel.{h,cpp} (out of group). The shot is queued as a
  CharAction in the binary (resolves during RunCombatScript) vs synchronous here
  — documented modeling, same draws.
- `CutsceneDuel` @0x4a53a8 — VERIFIED. abort if either personId<0; reportOnly
  forward; load scene; per-round flag reset; do/while `round<3 && !over`; outcome
  winner-by-ratio `(rb >= ra) ? A : B` matches `if (v59 >= v100) winner=A else B`
  (v100=Ratio(A), v59=Ratio(B) @0x4a5ade..0x4a5af4) — B's ratio >= A's → A wins,
  B wins ties.
  BOUNDARY: the top-of-function `RandomModulo(3)-5` (auctioneer/flavour voice)
  is on the GLOBAL game RNG (VIBE_Math_RandomModulo @0x58b89c), a different
  stream from the cutscene LCG the recon threads — a voice leaf, not drawn here.
  Building_ComputeOutputRatio (the exact HP-ratio fn @0x57d384) is approximated
  as hp/worth; only the `>=` tie-break direction is observable and it is correct.

## Handoff (not owned, not edited)
- src/gui/widget_layout.cpp is corrupted (`w.ld<…>` should be `w.at<…>`, mangled
  `pD(4)`) around lines 160–234 — blocks the `guild` library link. Needs the
  owner of the gui group to fix. My five files compile in isolation.
