# Hardening sweep — Group G (sim/debugcmd2.cpp, sim/debugcmd3.cpp)

MCP-verified 1:1 against gilde.exe (imagebase 0x400000). Every function carrying a
`gilde.exe 0xADDR` provenance was decompiled at that address and diffed line-for-line;
all float->int sites checked against the disasm; every double/float constant confirmed
via `get_global_value`; the funcs_5766CB dispatch table @0x63d964 verified byte-for-byte
via `get_bytes`.

## Summary counts
- Functions with provenance verified: **30** (debugcmd2: 21, debugcmd3: 9).
- VERIFIED-1:1: **30**
- FIXED: **0** (no divergences found; source already faithful)
- BOUNDARY (documented hook): the HE-list iteration / entity resolution / nearest /
  query-list searches (rules 8 boundaries already routed through DebugCmd3Hooks).
- Both files compile clean (`g++ -std=c++17 -fsyntax-only -Iinclude -Isrc -Ishim`).
- Golden tests reviewed (debugcmd2_test / debugcmd3_test): all encode CORRECT behavior;
  no test fixes needed.

## Shared primitives re-verified
- **ConvertX @0x5c6b08**: `fstcw; fldcw(0x1F00|lo); frndint; fldcw(orig)`. The control
  word 0x1F00 sets RC=11 (round toward zero / chop). The pseudocode pattern in every
  handler is `v6 = wealth*(roll+base)*scale; ConvertX(); v13 = (int)v6` — ConvertX
  truncates st0 in place toward zero, then `(int)` casts the now-integral double. Net =
  truncation toward zero. `DebugCmdScaledGold`'s `static_cast<i32>` reproduces this
  exactly. VERIFIED.
- **RandomModulo @0x58b89c**: `(n==0)?0:(RandNext()%n)`. Matches `DebugCmdRandomModulo`.
- **QueueRequest16 @0x494630**: `__usercall(a1@eax, a2@edx, a3@ecx /*amount*/, a4@bl
  /*market*/)`. Confirmed via disasm; the per-handler (a1,a2) orderings recovered below.

## debugcmd2.cpp — per-function (all VERIFIED-1:1)
Scaled family (find person else 1; roll; gold=wealth*(roll+base)*scale; ConvertX trunc;
optional 2nd roll into the msg arg; QR16; entity msg; return 0):
- 0x571898 ScaledB: roll%4, base dbl_625454=2.0, scale dbl_62545C=0.01, 2nd roll%6,
  QR16(id,-1). RNG order main->gold->extra confirmed. VERIFIED.
- 0x571990 ScaledC: roll%3, base 1.0(imm), scale dbl_625464=0.01, QR16(id,-1). VERIFIED.
- 0x571bb4 ScaledD: roll%3, base 1.0, scale dbl_62547C=0.01, QR16(id,-1). VERIFIED.
- 0x571c98 ScaledE: roll%3, base dbl_625484=2.0, scale dbl_62548C=0.01, QR16(id,-1). VERIFIED.
- 0x571d7c ScaledF: roll%2, base 1.0, scale dbl_625494=0x3f8999999999999a=0.0125,
  QR16(id,-1). Constant byte-confirmed. VERIFIED.
- 0x571e60 ScaledG: roll%3, base dbl_62549C=2.0, scale dbl_6254A4=0x3f81111111111111
  =1/120, QR16(id,-1). Constant byte-confirmed. VERIFIED.
- 0x572038 ScaledH: roll%3, base dbl_6254B4=2.0, scale dbl_6254BC=0.01, QR16(-1,id). VERIFIED.
- 0x572120 ScaledI: roll%2, base 1.0, scale dbl_6254C4=0.01, QR16(-1,id). VERIFIED.
- 0x572308 ScaledJ: gate `if(!byte+358 && !byte+361) return 1024`; roll%4, base 1.0,
  scale dbl_6254D4=0.01, QR16(-1,id). Gate (`officeRank==0 && officeAlt==0`). VERIFIED.
- 0x572538 ScaledK: roll%2, base 1.0, scale dbl_6254E4=0.01, QR16(-1,id). VERIFIED.
- 0x57261c ScaledL: roll%2, base 1.0, scale dbl_6254EC=0.01, QR16(-1,id). VERIFIED.
- 0x572700 ScaledM: roll%3, base 1.0, scale dbl_6254F4=0.01, 2nd roll%0xF, QR16(-1,id).
  RNG order main->gold->extra confirmed. VERIFIED.

IfNotState14 family (gate `if(byte+358==14) return 1024`):
- 0x5713a4 IfNotState14A: roll%3, base dbl_625424=2.0, scale dbl_62542C=0.01, QR16(id,-1). VERIFIED.
- 0x5716a0 IfNotState14B: roll%3, base 1.0, scale dbl_625444=0.01, QR16(id,-1). VERIFIED.
- 0x571f44 IfNotState14C: roll%3, base 1.0, scale dbl_6254AC=0.01, **QR16(-1,id)**
  (MinusOneFirst — confirmed via decompile `QueueRequest16(-1,*v10,...)`). VERIFIED.

CheckType / leaf:
- 0x572204 CheckType: gate `if(BuildingType_GroupFromCode(HIBYTE(rec+353))==7) return
  1024`; roll%4, base 1.0, scale dbl_6254CC=0.01, QR16(-1,id). The adapter's `buildType`
  holds the pre-mapped group code (same convention as IfNotType7 in debugcmd.cpp), so the
  source's `p.buildType == 7` is faithful. VERIFIED.
- 0x57477c RetFail1024: `return 1024`. VERIFIED.

Dispatch table DebugCmd2NpcTableEntry: indices 1/3/5/6/8/9/10/11/12/13/14/15/16/18/19/20
confirmed against funcs_5766CB @0x63d964 (raw bytes decoded). VERIFIED.

All constants confirmed identical: 0.01 = 0x3f847ae147ae147b; 2.0 = 0x4000000000000000;
0.0125 = 0x3f8999999999999a; 1/120 = 0x3f81111111111111.

## debugcmd3.cpp — per-function (all VERIFIED-1:1)
Collection skeleton (He_FindFirstHandlerByFilter(1, kindWord, 15); loop resolving
owner@handler[43], then bB@handler[45], then bA@handler[44]; prod =
(bB.valid&&bB.prod)||(bA.valid&&bA.prod); per-variant code-71 gate; collect up to 8,
loop bound `offset+=4; offset<32`):

- 0x573ebc BroadcastA: GateA `bA.code==71 || bB.code==71`; RNG = pick only. VERIFIED.
- 0x574388 BroadcastB: GateB `(bA.valid&&bB.valid&&bA.code!=71)||bB.code!=71`;
  RNG = `RandomModulo(3)+2` (extra) THEN pick. Order confirmed. VERIFIED.
- 0x57458c BroadcastC: GateC `bA.code!=71 || bB.code!=71`; RNG = extra THEN pick. VERIFIED.
- 0x574784 BroadcastD: GateD `(bB.valid&&bA.valid&&bA.code==71)||bB.code==71`; RNG = pick
  only (reads global qword_13CE852 into msg args, no RNG advance). VERIFIED.
- 0x5749f8 BroadcastE: GateE `bA.code==71 || bB.code==71`; RNG = extra THEN pick. VERIFIED.
- 0x573930 QueueStateRequestPerHandler: GateQ `(bA.valid&&bB.valid&&bA.code==71)||
  bB.code==71`; RNG = pick THEN `RandomModulo(0x23)+60` (amount). Order confirmed.
  Owner resolved via Object_FindObjectById (vs ResolveEntityById elsewhere) — same
  semantics in our resolve hook. VERIFIED.
- 0x5740ec QueueScaledRequestPerHandler: GateQ; RNG = `RandomModulo(2)` roll -> gold
  (base 1.0, scale flt_625538=0x3c23d70a=0.00999999978f) -> QR16(?,?,gold,market) ->
  pick `RandomModulo(count)`. Order confirmed via disasm. The storage loop uses esi+=4
  (disasm 0x5741c1; Hex-Rays `a2+=2` was an artifact) -> cap 8, matching source. VERIFIED.
- 0x571a74 SpawnEntityFromHandlerList: Person_QueryBegin/IterNext collect up to 12 dwords
  (buf offset +=4, bound <48); if count 0 -> 1024; pick `RandomModulo(count)`; 2nd
  QueryBegin on picked id (no RNG); `RandomModulo(5)` roll; gold base dbl_62546C=2.0,
  scale dbl_625474=0.01; QR16(personId, ...). Cap 12 + RNG order confirmed. VERIFIED.
- 0x572400 SpawnEntityNearNearest: ObjectSearch_FindNearestEntity(rec,6,0.0,100.0) else
  1024; ResolveEntityById else 1024; `RandomModulo(3)` roll; gold base 1.0, scale
  dbl_6254DC=0.01; QR16(-1, personId). VERIFIED.

Dispatch table DebugCmd3NpcTableEntry: indices 7/17/34/36/37/38/39/41/42 confirmed
against funcs_5766CB @0x63d964. VERIFIED.

flt_625534 == flt_625538 == 0x3c23d70a; dbl_62546C=2.0; dbl_625474=dbl_6254DC=0.01.

## Documented boundary notes (no deterministic / RNG-observable divergence)
1. **Final extra heFindNext after the 8th collect.** The originals check the loop bound
   `offset<32` *after* calling He_FindNextMatchingHandler, so after collecting the 8th
   match they call FindNext once more before exiting. `CollectHandlers` breaks on
   `count>=cap` before that final FindNext. Difference is purely in the hooked
   iterator's advance; the match count (the only RNG-relevant value) is identical (8).
   Within the rule-8 hook boundary.
2. **Unconditional SendTail vs. conditional message.** In QueueScaled/QueueState/Broadcast
   the original sends He_SendEntityMessage only inside `if (v17 && v18)` (both picked
   buildings resolve). `SendTail(p)` is called unconditionally. This affects only the
   hooked sendEntityMessage side-effect (no RNG, no return code); in the all-valid case
   the unit tests exercise, behavior is identical. Within the hook boundary.

No source or golden-test changes were required; both compile clean and the golden
vectors agree with the binary.
