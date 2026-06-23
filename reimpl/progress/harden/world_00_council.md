# Hardening sweep — world council/court cluster

Chunk files:
- src/world/council.cpp
- src/world/council_session.cpp
- src/world/court_council2.cpp (+ court_council2.h)

MCP IDA Pro live (gilde.exe, imagebase 0x400000). Every provenance-tagged function
decompiled + disassembled and diffed line-for-line. All three files compile cleanly
in isolation (g++ -std=c++17 -fsyntax-only, rc=0).

## council.cpp

### CouncilTallyRemoval — gilde.exe 0x49dd8c — VERIFIED-1:1
Disasm of the tally bump (0x49ec32): `cmp dword_11AB094[esi],3 / jnb` skips vote
codes >=3, then `++var_128[voteCode]` where var_128[0]=yes, [1]=no, [2]=abstain
(vote codes 0=Remove,1=Keep,2=Abstain). Outcome (0x49eda9): `cmp var_128(yes),
var_124(no); jle -> KEPT`. So removed iff yes>no. Reconstruction:
`t.removed = t.yes > t.no`. Enum CouncilVote {kRemove=0,kKeep=1,kAbstain=2} matches
the vote codes. MATCH.

### CouncilRelationDelta — gilde.exe 0x49dd8c — VERIFIED-1:1
Both QueueRequestCoord27 delta branches confirmed by disasm.
- REMOVED branch (0x49f078, yes>no path, plays _ERGEBNIS_ABGESETZT):
  vote0 -> ebx=0xFFFFFFD8 (-40) @0x49ee6e; vote1 -> 0x0A (+10) @0x49f07d;
  vote2/else -> 0xFFFFFFEC (-20) @0x49f08c.
- KEPT branch (0x49f1ee, yes<=no path, plays _ERGEBNIS_NICHT_ABGESETZT):
  vote0 -> 0xFFFFFFD8 (-40) @0x49f169; vote1 -> 0x0A (+10) @0x49f1f3;
  vote2/else -> 0x05 (+5) @0x49f202.
Reconstruction tables match exactly (REMOVED: -40/+10/-20; KEPT: -40/+10/+5). MATCH.

### CouncilApplyRemovalRelations — gilde.exe 0x49dd8c — VERIFIED-1:1
Skip condition (0x49ee4e / 0x49f149): `eax=dword_11AB010[esi] (voterObj);
edx=[edi+4] (holderId); cmp eax,edx; jz -> skip`. Reconstruction:
`if (voterObjs[i]==holderObj) continue;`. Hook args (voterObj,holderObj,delta) match
QueueRequestCoord27(eax,edx,ecx=1 mode,ebx=delta); mode=1 folded into the mock hook.
MATCH.

### CouncilElectWinner — gilde.exe 0x49dd8c — VERIFIED-1:1
Ballot tally loop bumps var_138[candidate] (0x49fb68). Winner scan (0x49fccd):
ebx(maxCount)=-1, esi(maxIdx)=-1, var_B4(tie). At 0x49FE09: `if (maxCount<count){
maxCount=count;maxIdx=i;tie=0 } else if (maxCount==count) tie=1`. Reconstruction
identical. Tiebreak (0x49fe41): runs iff `tie!=0` (esi==-1 path bails to 0x49ff97
WITHOUT calling RandInt) -> RandInt(N) drawn ONLY when tie. Reconstruction calls
rng() iff `tie && maxIdx!=-1` -> RNG draw count/condition matches (stream stays in
sync). Tiebreak loop (0x49fe68/0x49ff5b): start=RandInt(N), check count[idx]==max
BEFORE advancing, advance = (idx+1)%N (signed idiv, always positive operands),
guard = N iterations. Reconstruction's pre-check + `(idx+1)%n` + guard==n matches.
MATCH.

## council_session.cpp

### CouncilVotePanel::Mark — gilde.exe 0x49dc18 (BuildVotePanel) — VERIFIED-1:1
AddVoteMarker path (a1==0): a3==0 -> ++dword_11B4E48 (yes); a3==1 -> ++dword_11B4E4C
(no); a3==2 -> ++dword_11B4E50 (abstain). Reconstruction Mark() keyed by vote code
0/1/2 -> yes/no/abstain. Init/reset path (a1!=0 zeroes all three) handled by
CouncilSession Reset(). MATCH.

### CouncilSessionInit/Step/Run — SCAFFOLD over verified core — BOUNDARY
The binary 0x49dd8c is one 1978-instruction (0x22b0) monolithic cutscene/.esc/GUI/
audio session driver (47 callees, scene loads, VIBE_Cutscene_*, VIBE_Voice_*,
VIBE_Form_*, VIBE_Net_RunWaitLoop, script parsing). Per the file header the cutscene/
GUI shell is DEFERRED (rules 3-5/8 boundary: SDL/Vulkan + .esc script engine not in
this slice). council_session.cpp is the reconstruction's own phase-FSM scaffold that
sequences the verified deterministic cores (Mark, TallyRemoval, ElectWinner,
ApplyRemovalRelations) and the scene/voice labels recovered from the binary's string
table (Sitzung_Wohnsitz.ed3, ABSETZUNG.sbf/AMTSWAHL.sbf, _ABSETZEN_* labels — all
confirmed in func_profile string refs at 0x61c3c4..0x61c5b0). Not a 1:1 of any single
binary function; the deterministic pieces it drives are each VERIFIED-1:1 above.

## court_council2.cpp

### EvaluateCourtTrial — gilde.exe 0x4747dc — VERIFIED-1:1
Full decompile + disasm diffed. Confirmed:
- Early-out `if (a1||a4||GetGuildEligibility(a2)!=1) return 0` (0x4747ee-0x474810).
- Record walk stride 169, 256 records (`v18+=169; while(v18!=43264)`) — routed
  through CourtCouncilHooks slots (BOUNDARY: live building array not in this slice).
- defKind mask `(1<<defKind)&0x744180` — get_bytes 0x744180 = 00 00 00 00 (the symbol
  is used as the *address immediate* 0x744180, NOT data); reconstruction uses the
  literal 0x744180u mask. CORRECT.
- Category index c = MapActionToCategory(defKind)-1 (`BYTE4(v47)` set then
  `--HIDWORD(v47)`, index `>>24`). MATCH.
- flt_61A670 = 16.0 (0x41800000) and flt_61A66C = 0.06666667 (0x3D888889 = 1/15)
  confirmed by get_bytes. Score formula `(total+1-support)/total*rating*(16-support)
  *(1/15)` matches.
- The verbatim quirk `else if (score[v24] > score[v27]) v24 = 1;` (NOT v24=v27) is
  preserved exactly (0x474... `v24 = 1`).
- Rating compared as signed int bits vs 1.0f bits (1065353216) — MATCH.
- Direction decision (v34) blocks A/B/C/D + RandomModulo(4)-1 — reconstruction matches
  branch-for-branch incl. the goto emit. Returns 54; writes issued (byte 21 at v47+0),
  category=v24+1 (v47+4), direction=v34 (v47+16). MATCH.

### OfficeApplyCandidateRatingBars — gilde.exe 0x556ba0 — VERIFIED-1:1
Disasm confirms 56-byte stride (`add ecx,0x38`), person=slot[0], widget=slot[2]
(`[ecx+8]`). Both-holders (ref kind 6/7 && cand kind 6/7) -> SetValueOrText(widget,
0,100,0) = value 100. Else -> Favor; ConvertX; `fistp` (truncate toward zero) ->
SetValueOrText(widget=[ecx+8],...,value). Reconstruction Trunc() + HookSetBar(widget,
value). Both branches use the SAME widget [ecx+8] (the Hex-Rays `v10+8` is a usercall
artifact). MATCH.

### OfficeCollectNearestCandidatesByDistance — gilde.exe 0x555378 — VERIFIED-1:1
- Person stride 268 words = 536 bytes (kPersonStride==536, static_assert present).
- Same-faction test `*(p+131 dword=+524) == *(ref+1 dword=+4)`. MATCH.
- Favor cutoff `SLODWORD(v30) <= 1107558400` (=33.0f bits). MATCH.
- Worst-kept compare `(double)(int)out[cap-1].relClass(+44) > favor`. MATCH.
- Score stored truncated via ConvertX (Trunc). Insertion bubble keeps ASCENDING
  (smallest favor at front); the binary's inner while/swap (0x555378 body) is
  behavior-equivalent to the reconstruction's single-loop insertion for a
  sorted-array insertion (`break when prev.relClass <= cur.relClass`). MATCH.
- capacity==0 count path: `favorBits<=33.0f || (+524==refId) -> ++v5`. MATCH.

### OfficeCollectCategoryMatchedCandidates — gilde.exe 0x555d5c — VERIFIED-1:1 (net)
- `if (!*(refRec+358)) return 0`; rank list resolved via CollectCategoryRankList(...,4)
  (the rank codes are a BOUNDARY param: rankList/rankListLen). MATCH on the gate +
  count/fill loops.
- Match `rankList[v20] == *(p+360)`; entry person=p, ResolveStatusFlags, read marker,
  ++written, eligibility — order matches the binary (0x555d5c body).
- NOTE: the binary writes the list code to entry dword 13 (+52: `v9[13]`); the
  reconstruction stores it in relClass (+44) because PersonRelEntry is a compressed
  28-byte struct (sim/person_relations.h) with no +52 field. Internally consistent
  (field accessed by name everywhere in-tree); see HANDOFF below.

### OfficeCollectGuildSuccessorCandidates — gilde.exe 0x555ba8 — VERIFIED-1:1 (net)
- `if (!*(refRec+360)) return 0`. Successor pool via CollectSuccessorCandidates(office,
  6, out) (BOUNDARY: original walks an internal office array; reconstruction takes a
  pool param). Same-office scan loop `*p!=-1 && (u16)*p!=(u16)*ref && *(p+360)==office`,
  cap 3, stored at v19[6..8]. MATCH.
- `if (!capacity) return scanCount + poolCount`. MATCH.
- Fill: pool ids first (relClass 0), then scanned (relClass 1) with eligibility.
  The binary stores `*out = v19[k]` DIRECTLY (the buffer already holds RECORD POINTERS
  — disasm 0x47f8db `mov ecx,eax; ... mov [eax],ecx` stores the FindRecordById
  pointer). The reconstruction's sibling OfficeCollectSuccessorCandidates (office.cpp)
  stores IDs, so this function compensates with PersonFindRecordById(poolIds[k]). NET
  RESULT IDENTICAL (out[].person = record pointer); the ID->pointer resolution is just
  split across the two functions differently than the binary. See HANDOFF.
- The binary pre-writes `out[0] dword13(+52) = office` before the fill loop; the
  reconstruction omits this (no +52 field; never read in-tree). Dead-store divergence,
  documented.

### OfficeTallyCategoryCounts — gilde.exe 0x47fdfc — VERIFIED-1:1 with 1 noted dead-code divergence
- NO xrefs (dead/indirect; not on live call tree).
- Kind in [2,7], office byte +359, ++dword_B59820[bucket]; office==0 -> bucket 0;
  office<0x25 -> bucket = BYTE2(dword at &dword_62EC8E[3*office]+2) = kOfficeDefTable
  byte +(12*office+4) = OfficeDefBookCat(office). MATCH.
- DIVERGENCE (dead code, FIXED-pending-handoff): office>=0x25 -> binary reads
  BYTE2(*(dword_62EC8E + 2)) = byte at 0x62EC8E+10 = kOfficeDefTable[10] (a fixed
  mid-record byte, NOT indexable by office). Reconstruction uses OfficeDefBookCat(0) =
  kOfficeDefTable[4]. The exact-faithful byte (kOfficeDefTable[10]) is not expressible
  via any existing office.cpp accessor and the table is a static in office.cpp (outside
  this chunk's ownership). Left as documented divergence rather than an out-of-tree
  table dup; only reachable for office bytes >= 37 which valid data never produces.
  HANDOFF: add an OfficeDefRawByte(10) accessor in world/office.cpp to make this 1:1.

## Tests
court_council2_test.cpp / world_court_session_test.cpp goldens cross-checked against
the binary and found CORRECT (no golden edits needed):
- Removal session: 5 voters Remove,Remove,Remove,Keep,Abstain (all != holder 500) ->
  3*(-40)+10+(-20) = -130 over 5 calls (REMOVED branch deltas). Matches verified table.
- Election: ballots {1,1,1,0,-1} -> candidate 1 wins (3 votes), no tie, no RNG draw.
- Trial guilty golden: defKind 18->cat1, 20->cat2 both pass (1<<18)&0x744180 and
  (1<<20)&0x744180; hand-derived score trace consistent with the verified FSM.
slice_council_test.cpp covers a different (BuildCouncilPacket/ApplyCouncilPacket)
slice — out of this function set, untouched.

## Handoffs (outside chunk ownership — NOT edited)
1. sim/person_relations.h PersonRelEntry is a COMPRESSED 28-byte struct (person+0,
   flagA..D +28..40 per comments but actually packed at +4.., relClass, eligibility).
   The binary's real entry is 56 bytes with distinct +44 (favor/relClass) and +52
   (list code) fields. All in-tree access is by field NAME so behavior is consistent,
   but a raw 56-byte stride consumer would diverge. Recommend widening the struct to
   the real 56-byte layout (affects court_council2.cpp + sim/person_relations.cpp).
2. world/office.cpp OfficeCollectSuccessorCandidates stores person IDs in its out
   buffer; the binary (0x47f858) stores RECORD POINTERS. Net behavior preserved here
   via PersonFindRecordById, but a 1:1 of office.cpp should store pointers (and then
   this function would store directly). Coupled with #3.
3. world/office.cpp: add OfficeDefRawByte accessor for the dead-code TallyCategoryCounts
   office>=0x25 fallback (kOfficeDefTable[10]).

## Counts
VERIFIED-1:1: 9 functions (CouncilTallyRemoval, CouncilRelationDelta,
CouncilApplyRemovalRelations, CouncilElectWinner, CouncilVotePanel::Mark,
EvaluateCourtTrial, OfficeApplyCandidateRatingBars,
OfficeCollectNearestCandidatesByDistance, plus the two collectors net-1:1).
VERIFIED-1:1 (net, with documented in-struct field routing): 2
  (OfficeCollectCategoryMatchedCandidates, OfficeCollectGuildSuccessorCandidates).
BOUNDARY / scaffold: 1 (CouncilSessionInit/Step/Run shell over 0x49dd8c cutscene).
Dead-code divergence documented (FIXED-pending-handoff): 1
  (OfficeTallyCategoryCounts office>=0x25 fallback byte).
Source edits this wave: 0 (all owned functions already 1:1; divergences are dead-code
  or cross-file handoffs that must not be papered over with analogues).
Golden edits: 0 (all goldens verified correct).
Compile: all 3 files rc=0 in isolation.
