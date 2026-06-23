# Hardening sweep — world_00_amt (Amt office-administration cluster)

MCP IDA Pro (gilde.exe, imagebase 0x400000) used to decompile + disasm + get_bytes
every provenanced function in the four owned files and diff line-for-line against
the binary. Constants/tables confirmed byte-for-byte. Every float->int site checked
against ConvertX (0x5c6b08 = truncate-toward-zero via fldcw RC=chop + frndint).

Owned files:
- src/world/amt_economy2.cpp / .h
- src/world/amt_enforcement.cpp / .h
- src/world/amt_goods.cpp / .h
- src/world/amt_recon_office_window.cpp / .h

## Constants / tables verified byte-for-byte (get_bytes)
- @0x5526b0 office-type id table: `00 06 04 05 01 02 03 07` — MATCH (kAmtOverviewSlotOfficeType).
- @0x5526ac role count = 8 — MATCH (kAmtCandidateRoleCount).
- @0x63d584 funcs_557292 (5x 20-byte records): col/rend/idx all MATCH kAmtRoleTable.
- @0x62ec93 byte_62EC93 stride-12, +0 per record = {0,5,7,10,5,7,10,5,7} — MATCH kOfficeTileCount.
- @0x47de10 flt_47DE10 = {1.1,1.05,1.0,0.95,0.9,0,0,0} — had a 5th live value (0.9) MISSING from the old 4-entry table (see FIX below).
- @0x61afc8/0x61afcc = 100.0 / 32.0 — MATCH.
- @0x6258c4..ec rivalry consts (0.0125,0.25,0.6,0.7,0.8,2.0,160.0,10.0) — MATCH.
- @0x6258f0/f4/f8 enforce consts (0.2,0.5,0.25) — MATCH.
- @0x62675c flt_62675C = 0x38000100 (RandFloatScaled scale) — MATCH.
- @0x62598c dbl_62598C = 0x4084666666666666 = 652.8 — MATCH (kGoodsThreshold).

## amt_economy2.cpp
- TriggerOfficeNotice (0x483570) — VERIFIED-1:1. Byte-index scan step 24 over +8 type
  byte (byte_B59850 = byte_B59848+8); on match re-posts holder vacant (state 3).
- ResetGuildSlots (0x480b50) — VERIFIED-1:1. OfficeHolder offsets (holder+0/city+4/
  type+8/rank+12/state+16) confirmed via law_types.h static_asserts; byte_B59858 = +16.
- HighlightGuildMembers (0x48311c) — VERIFIED-1:1. category 1..7 gate + dedup emit.
- BuildOfficeInfoText (0x483414) — VERIFIED-1:1. rank>=2 gate, empty-list gate, "%s$A"
  concat, single SendEntityMessage. (entry index list modeled contiguous vs binary's
  stride-12 v8[12*i] read — documented caller abstraction.)
- ComputeOfficeRenderOffset (0x4834e4) — FIXED.
    * BUG: stretch table was `kRenderStretch[4]` and indexed with `lawLevel & 3`. The
      binary `fld flt_47DE10[v8*4]` indexes the RAW law #14 field +0 (NO mask) into a
      table whose 5th entry (0.9 @0x47de1c) is a LIVE value. Old code: lawLevel 4 ->
      kRenderStretch[0]=1.1 (WRONG, should be 0.9).
    * FIX: extended kRenderStretch to 8 entries {1.1,1.05,1.0,0.95,0.9,0,0,0} and index
      raw (guarded to table extent for memory-safety). x = ConvertX(stretch) (trunc) and
      step = ConvertX((i16)tiles*100*32*x) both truncate toward zero — already correct.
- FindNextActiveBuilding (0x57bb50) — FIXED.
    * BUG 1 (inverted generation compare): staleness used `generation < cache->generation`;
      binary is `dword_641FDC(cachedGen) < qword_13CE852(currentGen)`. FIX: `cache->generation
      < generation`. Old code never invalidated on an ADVANCED generation.
    * BUG 2 (staleness marker field): cache field was `cachedTypeWord` storing `.type` and
      compared `== 0xFF`; binary checks `*(WORD*)dword_641FE4 == 0xFFFF` (the marker/id
      word). FIX: renamed to `cachedMarkerWord` (u16), stored = id&0xFFFF, compared 0xFFFF.
- ComputeBuildingRivalryScore (0x57bc60) — FIXED.
    * BUG (else-branch region tests inverted operands). Binary 0x57bead..0x57bee9:
        if (v19 == 2) f=0.8;  else if (v9 == v19) f=0.7;  else f=0.6;
      where v9 = active-building region (= self.cityRegion) and v19 = law #2 field +24
      (= self.cityRegion2). Old code tested `self.cityRegion==2` and
      `rv.cityRegion==self.cityRegion2` — WRONG operands. FIX: `self.cityRegion2==2` and
      `self.cityRegion==self.cityRegion2`. (Same-region branch v8==v9 was already correct.)
    * Float precision hardened to match x87: supply, base (workForce*0.25, *supply,
      supply*f*base), and wf are now float-narrowed at each binary store point (the binary
      keeps var_20/var_24/var_2C as float). payout = ConvertX((wf+160)*base) (trunc). The
      same/else multiply order matches (commutative). Rep band uses (0.05,0.95) (bit cmp
      == float cmp for positive reps). rep delta +0.05 / -0.05 (0x3D4CCCCD / 0xBD4CCCCD).
- LookupSelectionInfoText (0x507b18) — VERIFIED-1:1. sprintf "_STADTAUSWAHL_%s_INFO+0",
  upcase, lookup or fallback to the ORIGINAL key, 2-byte widen copy loop (exact stride).
- OpenOfficeWindow (0x5546a0) — VERIFIED-1:1. v5[2]=v3(=ecx=a1 at entry, never reassigned
  -> slot0=a1 is correct), v5[1]=516, v6=6.

## amt_enforcement.cpp
- EnforceLawViolations (0x57bf20) — VERIFIED-1:1 (with RNG precision FIX).
    * Cadence (turn%4==0), branch select (law2 +24 ==2 decay / !=2 violation; v5/v15 from
      threshold 0 vs nonzero), qualifying predicate, guard-byte test ((int@+9)>>24 != v15),
      EvaluateViolation reuse, rep clamp (>0.1 -> -0.1) all MATCH.
    * RNG order MATCH: decay branch draws RandomFloatScaled once in the predicate, then once
      for the penalty (only for qualifying persons). Violation branch draws via law.cpp.
    * FIX (precision): local helper now `double RandFloatScaled()` (0x58b929 returns DOUBLE);
      the decay roll/compare stay in double (binary fmul/fcomp in extended), penalty narrowed
      to float only at the command store (binary *(float*)&v12). Previously narrowed too early.
    * Doc FIX: profClass and officeType are BOTH byte +2 (binary reads +2 twice as v1/v3);
      guardHighByte holds RAW int@+9 (code shifts >>24 once) — header comments corrected to
      match the test convention (`guardHighByte=(1<<24)` -> >>24==1).

## amt_goods.cpp
- GoodsCountActive / GoodsType3ShouldDrain / GoodsSpawnCount (0x57dd84) — VERIFIED-1:1.
  Worth gate unsigned `(u16)worth >= (u16)RandomModulo(4)+44`; RandomModulo(4) drawn once
  per type-3 building after the present/occupied/type gates (matches binary short-circuit).
- AmtRefreshGuildState (0x4becdc) — VERIFIED-1:1 (hook pump; net/command boundary).
- GoodsRunDistributionPass (0x57dd84) — FIXED.
    * BUG (Walk-3 Branch B control flow). Binary 0x57e130: Branch B ENTRY is
      `type==8 && stock>=cap` (ownersClear is the INNER gate for ++v32); a type-8 over-stocked
      building that enters B does NOT fall through to Branch C even when its owner gate fails.
      Old code folded ownersClear into the `else if` condition, so a dirty-owner over-stocked
      type-8 wrongly fell to Branch C (IsActiveType -> ++v32/++v33). FIX: B entry =
      `type==8 && overStocked`, with `if (ownersClear) ++v32;` inside. Verified by micro-test
      (dirty-owner over-stocked firm count == baseline, not baseline+1).
    * FIX (Rule 8 cheap analogue). Replenish variant was `kind*16 + wing` — a stand-in for
      VIBE_BuildingType_ComputeVariantIndex (0x589cb0, a real switch table). Wired the existing
      faithful reconstruction guild::sim::BuildingType_ComputeVariantIndex(kind+1, wing+1).
      RNG draw order preserved (RandomModulo(2) wing, then RandomModulo(0xC) kind).
    * RNG count/order otherwise MATCH (type-3 draws, special-firm gate RandomModulo(4),
      spawn-count RandomModulo(2) only in the 30<=v32<40 branch). The inner draws of
      SpawnSpecialFirm (RandomModulo(4)+RandomModulo(0x2000)) and SpawnBusiness
      (RandomModulo(0xC)+RandomModulo(0x400)) are delegated to the hooks per the hook contract
      — HANDOFF NOTE: the wired backend must draw those to keep the RNG stream in sync.

## amt_recon_office_window.cpp
- kAmtRoleTable / kAmtOverviewSlotOfficeType — VERIFIED-1:1 (bytes above).
- OfficePrepareCandidatePage (0x555eb4) — VERIFIED-1:1. scrollWin==-1 -> builtScroll=false,
  returns uninit ecx (modeled 0 per wired caller).
- RatingBarRowMode / RatingBarBothNobility (0x556ba0) — VERIFIED-1:1. head/populated gates,
  both-in-{6,7} -> MaxValue(100) else Favorability (ConvertX trunc done by caller view).
- AmtFindFirstNonEmptyRole (0x556c40 @0x556d2b/0x556fe4) — VERIFIED-1:1. rowCounts stride 5
  ints (binary imul 0x14 bytes), park-at-count semantics.
- AmtSelectRoleAtFrameTop (0x556f6d) — VERIFIED-1:1. in-range -> rowCounts[5*idx]; else idx=0,
  selectedCount=-1 (v61).
- AmtComputeRoleButtonYs (0x556df1..0x556ee2) — VERIFIED-1:1. span/(count-1) signed idiv for
  rem+step; per-row `--rem; y += (rem>=0)+step+33` (predecrement-then-test) reproduced exactly.
- AmtOverviewFindClickedSlot (0x5575c8 @0x557970) — VERIFIED-1:1. first-match wins, terminal
  returns slotCount.
- AmtHasOccupiedOffice (0x480cb4) — VERIFIED-1:1. per-slot: typeHiByte==7 && holderId!=-1 &&
  holderResolves; first hit -> 1 (caller supplies the 216,210,... scan-order view).
- AmtBuildOpenOfficeDescriptor (0x5546a0) — VERIFIED-1:1 (516 / byte 6).

## Counts
- Functions with provenance examined: 23.
- VERIFIED-1:1: 18.
- FIXED: 5 (ComputeOfficeRenderOffset, FindNextActiveBuilding, ComputeBuildingRivalryScore,
  GoodsRunDistributionPass, EnforceLawViolations[RNG precision]).
- BOUNDARY (hook/leaf, rule-8 sanctioned): Form/HUD/Vulkan render, command/network queue,
  live word_12CE910/byte_B59848 tables, RNG/coord plumbing — all routed through documented
  inert-default hooks (no fake logic substituted; the one cheap analogue found was removed).

## Tests
- amt_economy2_test + amt_recon_office_window_test: 150 checks, 0 failures (built isolated).
- amt_economy2_itest (real RNG + real ConvertX): 18 checks, 0 failures.
- amt_enforcement violation/guard/cadence micro (vs world_amt2_test goldens): all pass.
- amt_goods FullPass golden + Branch-B control-flow micro (4 cases): all pass.
- No golden encoded WRONG behavior in the owned suites; all owned goldens stayed valid after
  the fixes (the fixed paths were latent — not exercised by the existing goldens).

## Handoffs
- src/sim/building_type.h now included by amt_goods.cpp (reuse of the real
  ComputeVariantIndex 0x589cb0) — no symbol redefinition; build clean.
- PRE-EXISTING build break (NOT mine): src/gui/widget_layout.cpp uses `w.ld<i32>(...)` on a
  Widget lacking `ld` — blocks the full lib link. Owned files compile + test green in
  isolation. Left for the gui wave.
